#include "kraken/KrakenService.h"

#include "core/JsonStore.h"
#include "core/Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QUrl>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcKraken, "orkc.kraken")

namespace {
constexpr int kDefaultTimeoutMs = 8000;
constexpr int kLcdTimeoutMs = 180000;  // media processing (<=150 s) + upload
constexpr int kHeartbeatMs = 10000;
constexpr int kCrashWindowMs = 120000;
constexpr int kMaxCrashesInWindow = 5;
constexpr int kExitFatal = 3;  // service could not load any backend
constexpr int kHistory = 240;
}  // namespace

KrakenService::KrakenService(Settings* settings, QObject* parent) : QObject(parent), m_settings(settings) {
    m_clock.start();
    m_timeoutTimer.setInterval(500);
    connect(&m_timeoutTimer, &QTimer::timeout, this, &KrakenService::checkTimeouts);
    m_heartbeat.setInterval(kHeartbeatMs);
    connect(&m_heartbeat, &QTimer::timeout, this, [this] {
        if (!m_proc || m_proc->state() != QProcess::Running) return;
        if (m_missedPings >= 2) {
            appendLog(u"service unresponsive; restarting it"_s);
            m_proc->kill();  // onFinished schedules the restart
            return;
        }
        ++m_missedPings;
        send(u"ping"_s, {}, 5000, [this](bool ok, const QJsonObject&) {
            if (ok) m_missedPings = 0;
        });
    });
    m_restartTimer.setSingleShot(true);
    connect(&m_restartTimer, &QTimer::timeout, this, &KrakenService::launch);
    m_persistTimer.setSingleShot(true);
    m_persistTimer.setInterval(800);
    connect(&m_persistTimer, &QTimer::timeout, this, &KrakenService::persistDesired);
    loadDesired();
}

KrakenService::~KrakenService() { stop(); }

QString KrakenService::servicePath() const {
    const QString env = qEnvironmentVariable("ORKC_SERVICE_DIR");
    const QStringList candidates{
        env,
        QCoreApplication::applicationDirPath() + u"/../share/orkc/service"_s,
        QStringLiteral(ORKC_INSTALLED_SERVICE_DIR),
        QStringLiteral(ORKC_SOURCE_SERVICE_DIR),
    };
    for (const QString& dir : candidates)
        if (!dir.isEmpty() && QFileInfo::exists(dir + u"/kraken_service.py"_s))
            return QDir(dir).absoluteFilePath(u"kraken_service.py"_s);
    return {};
}

void KrakenService::start() {
    m_stopping = false;
    if (!m_settings->krakenEnabled()) {
        setState(u"disabled"_s, u"Kraken support is turned off in Settings."_s);
        return;
    }
    launch();
}

void KrakenService::launch() {
    if (m_proc) return;
    const QString script = servicePath();
    if (script.isEmpty()) {
        setState(u"error"_s, u"kraken_service.py not found (set ORKC_SERVICE_DIR)."_s);
        return;
    }
    const QString python = m_settings->pythonExecutable();
    if (!QFileInfo(python).isExecutable()) {
        setState(u"error"_s, u"Python interpreter %1 not found."_s.arg(python));
        return;
    }

    m_proc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char* var : {"PYTHONPATH", "PYTHONHOME", "PYTHONSTARTUP", "PYTHONUSERBASE", "VIRTUAL_ENV", "PYTHONSAFEPATH"})
        env.remove(QString::fromLatin1(var));
    env.insert(u"PYTHONUNBUFFERED"_s, u"1"_s);
    env.insert(u"PYTHONDONTWRITEBYTECODE"_s, u"1"_s);
    m_proc->setProcessEnvironment(env);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &KrakenService::onStdout);
    connect(m_proc, &QProcess::readyReadStandardError, this, &KrakenService::onStderr);
    connect(m_proc, &QProcess::finished, this, &KrakenService::onFinished);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            appendLog(u"failed to start: "_s + m_proc->errorString());
            onFinished(-1, QProcess::CrashExit);
        }
    });

    QStringList args{u"-I"_s, script};
    if (qEnvironmentVariableIntValue("ORKC_KRAKEN_MOCK") == 1) args << u"--mock"_s;
    if (qEnvironmentVariableIntValue("ORKC_KRAKEN_DEBUG") == 1) args << u"--debug"_s;
    m_outBuf.clear();
    m_errBuf.clear();
    m_missedPings = 0;
    setState(u"starting"_s, u"Starting Kraken service…"_s);
    appendLog(u"launch: %1 %2"_s.arg(python, args.join(u' ')));
    m_proc->start(python, args);
    m_timeoutTimer.start();
    m_heartbeat.start();
}

void KrakenService::stop() {
    if (m_persistTimer.isActive()) persistDesired();
    m_stopping = true;
    m_restartTimer.stop();
    m_heartbeat.stop();
    if (!m_proc) return;
    QProcess* p = m_proc;
    if (p->state() == QProcess::Running) {
        p->write("{\"id\":0,\"cmd\":\"shutdown\"}\n");
        p->closeWriteChannel();
        if (!p->waitForFinished(2500)) {
            p->terminate();
            if (!p->waitForFinished(1500)) p->kill();
        }
    }
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->deleteLater();
        m_proc = nullptr;
    }
}

void KrakenService::retry() {
    m_crashTimes.clear();
    stop();
    m_stopping = false;
    start();
}

void KrakenService::onFinished(int code, QProcess::ExitStatus status) {
    if (!m_proc) return;
    m_proc->deleteLater();
    m_proc = nullptr;
    m_heartbeat.stop();
    // Fail everything in flight.
    const auto pending = std::exchange(m_pending, {});
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        if (it->cb) it->cb(false, {{u"code"_s, u"service_exit"_s}, {u"message"_s, u"Kraken service stopped"_s}});
    m_status.clear();
    m_lightInFlight.clear();
    m_lightPending.clear();
    Q_EMIT telemetryChanged();
    if (m_lcdBusy) {
        m_lcdBusy = false;
        Q_EMIT lcdChanged();
    }
    if (m_stopping) {
        setState(u"stopped"_s);
        return;
    }
    appendLog(u"service exited (code %1, %2)"_s.arg(code).arg(status == QProcess::CrashExit ? u"crash"_s : u"normal"_s));
    if (code == kExitFatal && status == QProcess::NormalExit) {
        if (m_state != u"error"_s) setState(u"error"_s, u"The Kraken service could not load its hardware backend."_s);
        return;  // configuration problem: restarting won't help
    }
    const qint64 now = m_clock.elapsed();
    m_crashTimes.append(now);
    m_crashTimes.removeIf([now](qint64 t) { return now - t > kCrashWindowMs; });
    if (m_crashTimes.size() >= kMaxCrashesInWindow) {
        setState(u"failed"_s, u"The Kraken service keeps crashing; automatic restarts paused. See Settings → Diagnostics."_s);
        return;
    }
    ++m_restarts;
    const int delay = 1000 << qMin(int(m_crashTimes.size()) - 1, 4);  // 1,2,4,8,16 s
    setState(u"restarting"_s, u"Kraken service stopped unexpectedly; restarting in %1 s"_s.arg(delay / 1000));
    m_restartTimer.start(delay);
}

void KrakenService::onStdout() {
    m_outBuf.append(m_proc->readAllStandardOutput());
    qsizetype nl;
    while ((nl = m_outBuf.indexOf('\n')) >= 0) {
        const QByteArray line = m_outBuf.left(nl).trimmed();
        m_outBuf.remove(0, nl + 1);
        if (line.isEmpty()) continue;
        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &pe);
        if (!doc.isObject()) {
            appendLog(u"protocol error: "_s + pe.errorString() + u": "_s + QString::fromUtf8(line.left(200)));
            continue;
        }
        handleMessage(doc.object());
    }
    if (m_outBuf.size() > 8 * 1024 * 1024) {  // runaway line: protect ourselves
        appendLog(u"protocol error: oversized message; restarting service"_s);
        m_outBuf.clear();
        m_proc->kill();
    }
}

void KrakenService::onStderr() {
    m_errBuf.append(m_proc->readAllStandardError());
    qsizetype nl;
    while ((nl = m_errBuf.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_errBuf.left(nl));
        m_errBuf.remove(0, nl + 1);
        qCDebug(lcKraken).noquote() << line;
        appendLog(line);
    }
}

void KrakenService::appendLog(const QString& line) {
    m_log.append(line);
    if (m_log.size() > 300) m_log.remove(0, m_log.size() - 300);
    Q_EMIT logChanged();
}

int KrakenService::send(const QString& cmd, const QJsonObject& args, int timeoutMs, Callback cb) {
    const int id = m_nextId++;
    if (!m_proc || m_proc->state() != QProcess::Running) {
        QTimer::singleShot(0, this, [cb] {
            if (cb) cb(false, {{u"code"_s, u"not_running"_s}, {u"message"_s, u"Kraken service is not running"_s}});
        });
        return id;
    }
    QJsonObject msg{{u"id"_s, id}, {u"cmd"_s, cmd}};
    if (!args.isEmpty()) msg.insert(u"args"_s, args);
    m_pending.insert(id, {cmd, m_clock.elapsed() + timeoutMs, std::move(cb)});
    m_proc->write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n');
    return id;
}

void KrakenService::checkTimeouts() {
    const qint64 now = m_clock.elapsed();
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->deadline > now) {
            ++it;
            continue;
        }
        Callback cb = std::move(it->cb);
        const QString cmd = it->cmd;
        it = m_pending.erase(it);
        appendLog(u"request '%1' timed out"_s.arg(cmd));
        if (cb) cb(false, {{u"code"_s, u"timeout"_s}, {u"message"_s, u"Timed out waiting for the Kraken service (%1)"_s.arg(cmd)}});
    }
}

void KrakenService::handleMessage(const QJsonObject& msg) {
    if (msg.contains(u"event"_s)) {
        handleEvent(msg.value(u"event"_s).toString(), msg.value(u"data"_s).toObject());
        return;
    }
    const int id = msg.value(u"id"_s).toInt(-1);
    auto it = m_pending.find(id);
    if (it == m_pending.end()) return;  // late reply after timeout, or id 0 shutdown
    Callback cb = std::move(it->cb);
    m_pending.erase(it);
    const bool ok = msg.value(u"ok"_s).toBool();
    if (cb) cb(ok, ok ? msg.value(u"result"_s).toObject() : msg.value(u"error"_s).toObject());
}

void KrakenService::setState(const QString& state, const QString& message) {
    if (state == m_state && message == m_stateMessage) return;
    m_state = state;
    m_stateMessage = message;
    if (state != u"conflict"_s) m_holders.clear();
    Q_EMIT stateChanged();
}

void KrakenService::handleEvent(const QString& name, const QJsonObject& data) {
    m_simulated = m_simulated || data.value(u"simulated"_s).toBool();
    if (name == u"hello"_s) {
        m_backendLabel = data.value(u"backend_label"_s).toString();
        m_pythonInfo = u"Python %1 (%2), pid %3"_s.arg(data.value(u"python"_s).toString(), data.value(u"executable"_s).toString())
                           .arg(data.value(u"pid"_s).toInt());
        m_caps = data.value(u"capabilities"_s).toObject().toVariantMap();
        Q_EMIT stateChanged();
        setPollInterval(m_settings->telemetryInterval());
    } else if (name == u"state"_s) {
        const QString st = data.value(u"state"_s).toString();
        m_holders = data.value(u"holders"_s).toArray().toVariantList();
        if (data.contains(u"device"_s)) m_device = data.value(u"device"_s).toObject().toVariantMap();
        QString msg = data.value(u"message"_s).toString();
        if (st == u"searching"_s && msg.isEmpty()) msg = u"Looking for an NZXT Kraken…"_s;
        if (st == u"ready"_s) msg = m_device.value(u"description"_s).toString();
        const bool justReady = st == u"ready"_s && m_state != u"ready"_s;
        m_state = st;
        m_stateMessage = msg;
        Q_EMIT stateChanged();
        if (justReady) {
            // Restore everything we were asked to keep (incl. LCD media) on every (re)connect.
            if (!m_desired.isEmpty()) applyState(m_desired.toVariantMap());
            Q_EMIT becameReady();
        }
    } else if (name == u"status"_s) {
        m_status = data.toVariantMap();
        const QVariant lt = m_status.value(u"liquid_temp"_s);
        if (lt.isValid() && !lt.isNull()) {
            m_history.append(lt.toDouble());
            if (m_history.size() > kHistory) m_history.removeFirst();
        }
        Q_EMIT telemetryChanged();
    } else if (name == u"lcd"_s) {
        m_lcdMode = data.value(u"mode"_s).toString();
        const QString preview = data.value(u"preview"_s).toString();
        // Cache-bust: sensor frames reuse one file path.
        m_lcdPreview = preview.isEmpty() ? QString()
                                         : QUrl::fromLocalFile(preview).toString() + u"?t="_s + QString::number(m_clock.elapsed());
        m_lcdBusy = m_lcdMode == u"processing"_s;
        m_lcdMessage = m_lcdMode == u"error"_s ? data.value(u"message"_s).toString()
                       : m_lcdMode == u"processing"_s ? u"Optimising media…"_s
                       : data.contains(u"frames"_s)
                           ? u"%1 frame(s), %2 KB, uploaded in %3 s"_s.arg(data.value(u"frames"_s).toInt())
                                 .arg(data.value(u"bytes"_s).toInt() / 1024)
                                 .arg(data.value(u"upload_s"_s).toDouble())
                           : QString();
        Q_EMIT lcdChanged();
    }
}

// ---------------------------------------------------------------- commands

static QJsonObject toJson(const QVariantMap& m) { return QJsonObject::fromVariantMap(m); }

void KrakenService::mergeDesired(const QString& section, const QString& key, const QJsonObject& value) {
    if (key.isEmpty()) {
        m_desired.insert(section, value);
    } else {
        QJsonObject s = m_desired.value(section).toObject();
        s.insert(key, value);
        m_desired.insert(section, s);
    }
    m_persistTimer.start();  // debounced: a colour drag must not fsync on every move
    Q_EMIT desiredChanged();
}

int KrakenService::setLighting(const QString& channel, const QVariantMap& cfg) {
    QJsonObject args = toJson(cfg);
    args.insert(u"channel"_s, channel);
    QJsonObject stored = toJson(cfg);
    if (channel == u"all"_s) {
        QVariantList chans = m_caps.value(u"lighting"_s).toList();
        if (chans.isEmpty() && m_caps.isEmpty()) chans = {u"ring"_s, u"fans"_s};  // service not up yet
        for (const QVariant& ch : chans) mergeDesired(u"lighting"_s, ch.toString(), stored);
    } else {
        mergeDesired(u"lighting"_s, channel, stored);
    }
    sendLighting(channel, args);
    return 0;
}

int KrakenService::setCooling(const QString& channel, const QVariantMap& cfg) {
    QJsonObject args = toJson(cfg);
    args.insert(u"channel"_s, channel);
    mergeDesired(u"cooling"_s, channel, toJson(cfg));
    const int id = m_nextId;
    send(u"set_cooling"_s, args, kDefaultTimeoutMs, [this, id](bool ok, const QJsonObject& r) {
        Q_EMIT commandFinished(id, ok, ok ? QString() : r.value(u"message"_s).toString(), r.toVariantMap());
    });
    return id;
}

int KrakenService::setLcd(const QVariantMap& cfg) {
    mergeDesired(u"lcd"_s, {}, toJson(cfg));
    const int id = m_nextId;
    m_lcdBusy = true;
    m_lcdMessage = u"Applying…"_s;
    Q_EMIT lcdChanged();
    send(u"set_lcd"_s, toJson(cfg), kLcdTimeoutMs, [this, id](bool ok, const QJsonObject& r) {
        m_lcdBusy = false;
        if (!ok) {
            const QString code = r.value(u"code"_s).toString();
            if (code != u"superseded"_s) m_lcdMessage = r.value(u"message"_s).toString();
        }
        Q_EMIT lcdChanged();
        Q_EMIT commandFinished(id, ok, ok ? QString() : r.value(u"message"_s).toString(), r.toVariantMap());
    });
    return id;
}

int KrakenService::setLcdBrightness(int value) {
    QJsonObject lcd = m_desired.value(u"lcd"_s).toObject();
    lcd.insert(u"brightness"_s, value);
    m_desired.insert(u"lcd"_s, lcd);
    m_persistTimer.start();
    Q_EMIT desiredChanged();
    const int id = m_nextId;
    send(u"lcd_brightness"_s, {{u"value"_s, value}}, kDefaultTimeoutMs, [this, id](bool ok, const QJsonObject& r) {
        Q_EMIT commandFinished(id, ok, ok ? QString() : r.value(u"message"_s).toString(), r.toVariantMap());
    });
    return id;
}

int KrakenService::clearLcdMedia() {
    const int id = m_nextId;
    send(u"lcd_clear_media"_s, {}, 30000, [this, id](bool ok, const QJsonObject& r) {
        Q_EMIT commandFinished(id, ok, ok ? u"LCD memory cleared"_s : r.value(u"message"_s).toString(), r.toVariantMap());
    });
    return id;
}

int KrakenService::applyState(const QVariantMap& kraken) {
    QJsonObject args = toJson(kraken);
    // Keep only sections this backend supports; remember everything we send.
    for (const QString& section : {u"lighting"_s, u"cooling"_s}) {
        const QJsonObject s = args.value(section).toObject();
        for (auto it = s.begin(); it != s.end(); ++it) mergeDesired(section, it.key(), it.value().toObject());
    }
    if (args.contains(u"lcd"_s)) mergeDesired(u"lcd"_s, {}, args.value(u"lcd"_s).toObject());
    if (m_caps.value(u"lighting"_s).toList().isEmpty()) args.remove(u"lighting"_s);  // e.g. liquidctl fallback
    const bool hasLcd = args.contains(u"lcd"_s);
    if (hasLcd) {
        m_lcdBusy = true;
        Q_EMIT lcdChanged();
    }
    const int id = m_nextId;
    send(u"apply"_s, args, hasLcd ? kLcdTimeoutMs : kDefaultTimeoutMs, [this, id, hasLcd](bool ok, const QJsonObject& r) {
        if (hasLcd) {
            m_lcdBusy = false;
            if (!ok && r.value(u"code"_s).toString() != u"superseded"_s) m_lcdMessage = r.value(u"message"_s).toString();
            Q_EMIT lcdChanged();
        }
        Q_EMIT commandFinished(id, ok, ok ? QString() : r.value(u"message"_s).toString(), r.toVariantMap());
    });
    return id;
}

void KrakenService::setPollInterval(double seconds) {
    send(u"set_poll_interval"_s, {{u"seconds"_s, seconds}}, kDefaultTimeoutMs);
}

// At most one set_lighting per channel is in flight; while it is, only the
// newest request is kept. A fast colour drag therefore never builds a queue.
void KrakenService::sendLighting(const QString& channel, const QJsonObject& args) {
    if (m_lightInFlight.contains(channel)) {
        m_lightPending.insert(channel, args);
        return;
    }
    m_lightInFlight.insert(channel);
    send(u"set_lighting"_s, args, kDefaultTimeoutMs, [this, channel](bool ok, const QJsonObject& r) {
        m_lightInFlight.remove(channel);
        if (!ok && r.value(u"code"_s).toString() != u"not_running"_s)
            Q_EMIT commandFinished(0, false, r.value(u"message"_s).toString(), r.toVariantMap());
        if (m_lightPending.contains(channel)) sendLighting(channel, m_lightPending.take(channel));
    });
}

void KrakenService::persistDesired() {
    QString err;
    if (!JsonStore::writeAtomic(JsonStore::stateDir() + u"/kraken.json"_s, QJsonObject{{u"schema"_s, 1}, {u"desired"_s, m_desired}}, &err))
        qCWarning(lcKraken) << "could not persist Kraken state:" << err;
}

void KrakenService::loadDesired() {
    m_desired = JsonStore::read(JsonStore::stateDir() + u"/kraken.json"_s).value(u"desired"_s).toObject();
}
