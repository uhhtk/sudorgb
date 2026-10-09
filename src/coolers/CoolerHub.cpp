#include "CoolerHub.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace Qt::StringLiterals;

CoolerHub::CoolerHub(QString python, QString script, QObject* parent)
    : QObject(parent), m_python(std::move(python)), m_script(std::move(script)) {
    m_restart.setSingleShot(true);
    connect(&m_restart, &QTimer::timeout, this, &CoolerHub::start);
}

CoolerHub::~CoolerHub() { stop(); }

void CoolerHub::start() {
    if (m_proc) return;
    m_stopping = false;
    if (!QFileInfo(m_script).isFile() || !QFileInfo(m_python).isExecutable()) {
        m_message = u"Cooler service not found (%1)."_s.arg(m_script);
        Q_EMIT devicesChanged();
        return;
    }
    m_proc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char* var : {"PYTHONPATH", "PYTHONHOME", "PYTHONSTARTUP", "PYTHONUSERBASE", "VIRTUAL_ENV"})
        env.remove(QString::fromLatin1(var));
    env.insert(u"PYTHONDONTWRITEBYTECODE"_s, u"1"_s);
    m_proc->setProcessEnvironment(env);
    m_proc->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &CoolerHub::onStdout);
    connect(m_proc, &QProcess::finished, this, &CoolerHub::onFinished);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) onFinished();
    });
    m_buf.clear();
    QStringList args{u"-I"_s, m_script};
    if (qEnvironmentVariableIntValue("ORKC_COOLERS_MOCK") == 1) args << u"--mock"_s;
    m_proc->start(m_python, args);
}

void CoolerHub::stop() {
    m_stopping = true;
    m_restart.stop();
    if (!m_proc) return;
    QProcess* p = m_proc;
    m_proc = nullptr;
    p->disconnect(this);
    p->closeWriteChannel();  // EOF -> the service releases every device (LCDs back to firmware screen)
    if (!p->waitForFinished(3000)) {
        p->terminate();
        if (!p->waitForFinished(1500)) p->kill();
    }
    p->deleteLater();
}

void CoolerHub::onFinished() {
    if (!m_proc) return;
    m_proc->deleteLater();
    m_proc = nullptr;
    if (m_stopping) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_crashes.append(now);
    while (!m_crashes.isEmpty() && now - m_crashes.first() > 120000) m_crashes.removeFirst();
    if (m_crashes.size() >= 5) {
        m_message = u"The cooler service keeps crashing; restart SudoRGB to try again."_s;
        m_devices.clear();
        Q_EMIT devicesChanged();
        return;
    }
    m_restart.start(1000 << m_crashes.size());
}

int CoolerHub::send(const QString& cmd, const QVariantMap& args) {
    const int id = m_nextId++;
    if (!m_proc || m_proc->state() != QProcess::Running) {
        QTimer::singleShot(0, this, [this, id] { Q_EMIT commandFinished(id, false, u"Cooler service is not running"_s); });
        return id;
    }
    const QJsonObject msg{{u"id"_s, id}, {u"cmd"_s, cmd}, {u"args"_s, QJsonObject::fromVariantMap(args)}};
    m_proc->write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n');
    return id;
}

int CoolerHub::setSpeed(const QString& device, const QString& channel, const QVariantMap& cfg) {
    QVariantMap args = cfg;
    args[u"device"_s] = device;
    args[u"channel"_s] = channel;
    return send(u"set_speed"_s, args);
}

int CoolerHub::setLcd(const QString& device, const QVariantMap& cfg) {
    QVariantMap args = cfg;
    args[u"device"_s] = device;
    return send(u"set_lcd"_s, args);
}

void CoolerHub::onStdout() {
    m_buf += m_proc->readAllStandardOutput();
    qsizetype nl;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        const QJsonObject msg = QJsonDocument::fromJson(m_buf.left(nl)).object();
        m_buf.remove(0, nl + 1);
        if (msg.value(u"event"_s).toString() == u"devices") {
            m_devices = msg.value(u"data"_s).toObject().value(u"devices"_s).toArray().toVariantList();
            m_message.clear();
            Q_EMIT devicesChanged();
        } else if (msg.contains(u"id"_s) && msg.contains(u"ok"_s)) {
            const bool ok = msg.value(u"ok"_s).toBool();
            Q_EMIT commandFinished(msg.value(u"id"_s).toInt(), ok,
                                   ok ? QString() : msg.value(u"error"_s).toObject().value(u"message"_s).toString());
        }
    }
}
