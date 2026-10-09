#include "openrgb/RgbService.h"

#include "core/JsonStore.h"
#include "core/Settings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>
#include <QtMath>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcRgb, "orkc.rgb")

namespace {

bool processRunning(const QString& name) {
    const QDir proc(u"/proc"_s);
    for (const QString& pid : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!pid.front().isDigit()) continue;
        QFile f(u"/proc/"_s + pid + u"/comm"_s);
        if (f.open(QIODevice::ReadOnly) && QString::fromUtf8(f.readAll()).trimmed() == name) return true;
    }
    return false;
}

uint32_t fromPct(int pct, uint32_t lo, uint32_t hi) {
    // Works for inverted ranges too (some devices use min > max for speed).
    const double v = double(lo) + (double(hi) - double(lo)) * qBound(0, pct, 100) / 100.0;
    return uint32_t(std::lround(v));
}

QList<QRgb> fitTo(const QList<QRgb>& src, int n) {
    QList<QRgb> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i) out.append(src.isEmpty() ? qRgb(0, 0, 0) : src[i % src.size()]);
    return out;
}

QRgb scaled(QRgb c, int pct) {
    return qRgb(qRed(c) * pct / 100, qGreen(c) * pct / 100, qBlue(c) * pct / 100);
}

QString openRgbConfigPath() {
    const QString v = qEnvironmentVariable("XDG_CONFIG_HOME");
    const QString base = (!v.isEmpty() && QDir::isAbsolutePath(v)) ? v : QDir::homePath() + u"/.config"_s;
    return base + u"/OpenRGB/OpenRGB.json"_s;
}

const QList<QRegularExpression>& effectPatterns(const QString& effect) {
    static const QHash<QString, QList<QRegularExpression>> table = [] {
        auto re = [](const char* p) { return QRegularExpression(QString::fromLatin1(p), QRegularExpression::CaseInsensitiveOption); };
        QHash<QString, QList<QRegularExpression>> t;
        t[u"rainbow"_s] = {re("^rainbow wave$"), re("rainbow"), re("spectrum cycle"), re("spectrum"), re("colou?r ?wave"), re("^wave$"), re("cycle")};
        t[u"breathing"_s] = {re("^breathing$"), re("breath")};
        t[u"off"_s] = {re("^off$")};
        return t;
    }();
    static const QList<QRegularExpression> none;
    auto it = table.constFind(effect);
    return it == table.cend() ? none : *it;
}

}  // namespace

RgbService::RgbService(Settings* settings, QObject* parent)
    : QObject(parent), m_settings(settings), m_client(this), m_native(this), m_model(&m_all, this) {
    connect(&m_client, &OpenRgbClient::stateChanged, this, [this] {
        updateStatus();
        Q_EMIT stateChanged();
    });
    auto reset = [this] {
        rebuild();
        m_model.reset();
        updateStatus();
        Q_EMIT devicesChanged();
    };
    connect(&m_client, &OpenRgbClient::controllersReset, this, reset);
    connect(&m_native, &NativeDevices::controllersReset, this, reset);
    connect(&m_client, &OpenRgbClient::controllerChanged, this, [this](int dev) {
        if (dev < orgbCount() && dev < m_all.size()) m_all[dev] = m_client.controllers()[dev];
        m_model.refreshRow(dev);
    });
    connect(&m_native, &NativeDevices::controllerChanged, this, [this](int i) {
        const int dev = orgbCount() + i;
        if (dev < m_all.size()) m_all[dev] = m_native.controllers()[i];
        m_model.refreshRow(dev);
    });
    connect(&m_native, &NativeDevices::error, this, [this](const QString& m) { Q_EMIT notify(m, true); });
    m_fxTimer.setInterval(33);
    connect(&m_fxTimer, &QTimer::timeout, this, &RgbService::tickSoftFx);
    connect(&m_client, &OpenRgbClient::errorOccurred, this, [this](const QString&) {
        refreshProcessState();
        const bool local = m_settings->openRgbHost() == u"127.0.0.1"_s || m_settings->openRgbHost() == u"localhost"_s;
        if (!m_openRgbRunning && local && m_settings->autoStartOpenRgb() && !m_autoStartTried) {
            m_autoStartTried = true;
            const QString msg = startServer();
            qCInfo(lcRgb) << msg;
        }
        updateStatus();
    });
    // Track whether an OpenRGB process exists (cheap /proc scan) so the UI can
    // explain *why* we can't connect. Only while disconnected.
    m_procTimer.setInterval(4000);
    connect(&m_procTimer, &QTimer::timeout, this, [this] {
        if (connected()) return;
        refreshProcessState();
        updateStatus();
    });
}

void RgbService::reconnect() {
    m_autoStartTried = false;
    m_client.connectToServer(m_settings->openRgbHost(), quint16(m_settings->openRgbPort()));
    updateStatus();
}

// ---------------------------------------------------------------- software effects

void RgbService::startSoftFx(int dev, const QString& effect, const QList<QRgb>& colors, int speedPct, int brightnessPct) {
    m_softFx.insert(RgbDeviceModel::deviceKey(m_all[dev]), {effect, colors, speedPct, brightnessPct});
    if (!m_fxTimer.isActive()) {
        m_fxClock.start();
        m_fxTimer.start();
    }
    tickSoftFx();
}

void RgbService::stopSoftFx(int dev) {
    if (dev >= 0 && dev < m_all.size()) m_softFx.remove(RgbDeviceModel::deviceKey(m_all[dev]));
    if (m_softFx.isEmpty()) m_fxTimer.stop();
}

void RgbService::tickSoftFx() {
    const double t = m_fxClock.elapsed() / 1000.0;
    for (int dev = 0; dev < m_all.size(); ++dev) {
        const auto it = m_softFx.constFind(RgbDeviceModel::deviceKey(m_all[dev]));
        if (it == m_softFx.cend() || !writable(dev)) continue;
        const orgb::Controller& c = m_all[dev];
        const SoftFx& fx = *it;
        const double period = 12.0 - 10.0 * qBound(0, fx.speedPct, 100) / 100.0;  // 12 s .. 2 s per cycle
        const double phase = std::fmod(t / period, 1.0);
        const float level = float(qBound(0, fx.brightnessPct, 100)) / 100.0f;
        QList<QRgb> frame(c.leds.size(), qRgb(0, 0, 0));
        if (fx.effect == u"rainbow"_s) {
            // Horizontal wave: use each zone's matrix column when it has one
            // (keyboards), otherwise the LED's position along the zone.
            for (int z = 0, base = 0; z < c.zones.size(); base += int(c.zones[z].ledsCount), ++z) {
                const auto& zone = c.zones[z];
                const int n = int(zone.ledsCount);
                QList<double> pos(n, 0.0);
                for (int i = 0; i < n; ++i) pos[i] = n > 1 ? double(i) / n : 0.0;
                if (!zone.matrix.isEmpty() && zone.matrixWidth > 1)
                    for (uint32_t r = 0; r < zone.matrixHeight; ++r)
                        for (uint32_t col = 0; col < zone.matrixWidth; ++col) {
                            const uint32_t led = zone.matrix.value(int(r * zone.matrixWidth + col), 0xFFFFFFFF);
                            if (led < uint32_t(n)) pos[int(led)] = double(col) / zone.matrixWidth;
                        }
                for (int i = 0; i < n && base + i < frame.size(); ++i)
                    frame[base + i] = QColor::fromHsvF(float(std::fmod(pos[i] + 1.0 - phase, 1.0)), 1.0f, level).rgb();
            }
        } else {  // breathing
            const QRgb col = fx.colors.value(0, qRgb(255, 255, 255));
            const double k = level * (0.08 + 0.92 * (0.5 - 0.5 * std::cos(2 * M_PI * phase)));
            frame.fill(qRgb(int(qRed(col) * k), int(qGreen(col) * k), int(qBlue(col) * k)));
        }
        doUpdateLeds(dev, frame);  // OpenRGB client coalesces to <= 30 Hz
    }
}

void RgbService::rebuild() {
    m_all = m_client.controllers() + m_native.controllers();
}

bool RgbService::doUpdateMode(int dev, int modeIndex, const orgb::Mode& mode) {
    return dev < orgbCount() ? m_client.updateMode(dev, modeIndex, mode) : m_native.updateMode(dev - orgbCount(), modeIndex, mode);
}

bool RgbService::doUpdateLeds(int dev, const QList<QRgb>& colors) {
    return dev < orgbCount() && m_client.updateLeds(dev, colors);  // native devices have no per-LED mode
}

bool RgbService::doUpdateZoneLeds(int dev, int zone, const QList<QRgb>& colors) {
    return dev < orgbCount() && m_client.updateZoneLeds(dev, zone, colors);
}

void RgbService::start() {
    m_native.start();
    refreshProcessState();
    m_procTimer.start();
    m_client.connectToServer(m_settings->openRgbHost(), quint16(m_settings->openRgbPort()));
    updateStatus();
}

void RgbService::refreshProcessState() {
    const bool running = processRunning(u"openrgb"_s);
    if (running != m_openRgbRunning) {
        m_openRgbRunning = running;
        Q_EMIT stateChanged();
    }
}

QString RgbService::state() const {
    switch (m_client.state()) {
    case OpenRgbClient::State::Disconnected: return u"disconnected"_s;
    case OpenRgbClient::State::Connecting: return u"connecting"_s;
    case OpenRgbClient::State::Syncing: return u"syncing"_s;
    case OpenRgbClient::State::Connected: return u"connected"_s;
    }
    return {};
}

QString RgbService::serverAddress() const {
    return m_settings->openRgbHost() + u':' + QString::number(m_settings->openRgbPort());
}

void RgbService::updateStatus() {
    QString s;
    switch (m_client.state()) {
    case OpenRgbClient::State::Connected: {
        const int n = controllableCount();
        s = n == 1 ? u"1 device"_s : u"%1 devices"_s.arg(n);
        break;
    }
    case OpenRgbClient::State::Connecting: s = u"Connecting to %1…"_s.arg(serverAddress()); break;
    case OpenRgbClient::State::Syncing: s = u"Reading devices…"_s; break;
    case OpenRgbClient::State::Disconnected:
        if (m_openRgbRunning)
            s = u"OpenRGB is running without its SDK server. In OpenRGB open the “SDK Server” tab and press "
                u"“Start Server”, or restart it with --server."_s;
        else if (m_autoStartTried)
            s = u"OpenRGB server not reachable at %1 (auto-start attempted). Is OpenRGB installed?"_s.arg(serverAddress());
        else
            s = u"OpenRGB server not reachable at %1."_s.arg(serverAddress());
        break;
    }
    if (s != m_statusText) {
        m_statusText = s;
        Q_EMIT stateChanged();
    }
}

QString RgbService::startServer() {
    refreshProcessState();
    if (m_openRgbRunning) return u"OpenRGB is already running; enable its SDK server from its window."_s;
    const QString exe = QStandardPaths::findExecutable(u"openrgb"_s);
    if (exe.isEmpty()) return u"openrgb executable not found in PATH."_s;
    // Bind to loopback only: the SDK has no authentication.
    const QStringList args{u"--server"_s, u"--server-host"_s, u"127.0.0.1"_s, u"--server-port"_s,
                           QString::number(m_settings->openRgbPort()), u"--noautoconnect"_s};
    qint64 pid = 0;
    if (!QProcess::startDetached(exe, args, QString(), &pid)) return u"Failed to launch OpenRGB."_s;
    m_autoStartTried = true;
    m_client.connectToServer(m_settings->openRgbHost(), quint16(m_settings->openRgbPort()));
    QTimer::singleShot(1500, this, [this] { refreshProcessState(); });
    return u"Started OpenRGB server (pid %1); it may take a few seconds to detect devices."_s.arg(pid);
}

void RgbService::rescan() {
    m_native.rescan();
    m_client.resync();
}

int RgbService::controllableCount() const {
    int n = 0;
    for (const auto& c : m_all)
        if (!RgbDeviceModel::isKraken(c)) ++n;
    return n;
}

bool RgbService::krakenInOpenRgb() const {
    for (const auto& c : m_all)
        if (RgbDeviceModel::isKraken(c)) return true;
    return false;
}

bool RgbService::writable(int dev) const {
    // Native devices work even while the OpenRGB server is down.
    return dev >= 0 && dev < m_all.size() && !RgbDeviceModel::isKraken(m_all[dev]) && (dev >= orgbCount() || connected());
}

int RgbService::findPerLedMode(const orgb::Controller& c) const {
    // OpenRGB's own SetCustomMode preference order: Direct, Custom, Static.
    for (const QString& want : {u"direct"_s, u"custom"_s, u"static"_s})
        for (int i = 0; i < c.modes.size(); ++i)
            if (c.modes[i].name.compare(want, Qt::CaseInsensitive) == 0 && c.modes[i].has(orgb::HasPerLedColor)) return i;
    for (int i = 0; i < c.modes.size(); ++i)
        if (c.modes[i].has(orgb::HasPerLedColor)) return i;
    return -1;
}

bool RgbService::ensurePerLed(int dev) {
    const auto& c = m_all[dev];
    const orgb::Mode* cur = c.currentMode();
    if (cur && cur->isPerLed()) return true;
    const int idx = findPerLedMode(c);
    if (idx < 0) return false;
    orgb::Mode m = c.modes[idx];
    m.colorMode = orgb::ColorPerLed;
    return doUpdateMode(dev, idx, m);
}

bool RgbService::writeLeds(int dev, const QList<QRgb>& base) {
    const auto& c = m_all[dev];
    m_baseColors.insert(RgbDeviceModel::deviceKey(c), base);
    const int pct = m_model.softBrightness(dev);
    QList<QRgb> out = base;
    if (pct < 100)
        for (QRgb& px : out) px = scaled(px, pct);
    return doUpdateLeds(dev, out);
}

bool RgbService::setDeviceColor(int dev, const QColor& color) {
    if (!writable(dev) || !color.isValid()) return false;
    stopSoftFx(dev);
    const auto& c = m_all[dev];
    const QRgb rgb = color.rgb();
    if (ensurePerLed(dev)) return writeLeds(dev, QList<QRgb>(c.leds.size(), rgb));

    // No per-LED mode: use a mode-specific-colour mode (prefer "Static").
    int best = -1;
    for (int i = 0; i < c.modes.size(); ++i) {
        const auto& m = c.modes[i];
        if (!m.has(orgb::HasModeSpecificColor) || m.colorsMax < 1) continue;
        if (best < 0 || m.name.compare(u"static"_s, Qt::CaseInsensitive) == 0) best = i;
    }
    if (best < 0) {
        Q_EMIT notify(u"%1 has no colour-settable mode."_s.arg(c.name), true);
        return false;
    }
    orgb::Mode m = c.modes[best];
    m.colorMode = orgb::ColorModeSpecific;
    m.colors = QList<QRgb>(qMax<int>(1, int(m.colorsMin)), rgb);
    return doUpdateMode(dev, best, m);
}

bool RgbService::setZoneColor(int dev, int zone, const QColor& color) {
    if (!writable(dev) || !color.isValid()) return false;
    stopSoftFx(dev);
    if (!ensurePerLed(dev)) return setDeviceColor(dev, color);
    const auto& c = m_all[dev];
    if (zone < 0 || zone >= c.zones.size()) return false;
    const QString key = RgbDeviceModel::deviceKey(c);
    QList<QRgb> base = m_baseColors.value(key, c.colors);
    base = fitTo(base, int(c.leds.size()));
    const int start = c.zoneStart(zone);
    for (uint32_t i = 0; i < c.zones[zone].ledsCount && start + int(i) < base.size(); ++i) base[start + int(i)] = color.rgb();
    m_baseColors.insert(key, base);
    const int pct = m_model.softBrightness(dev);
    QList<QRgb> zoneColors;
    for (uint32_t i = 0; i < c.zones[zone].ledsCount; ++i) zoneColors.append(scaled(color.rgb(), pct));
    return doUpdateZoneLeds(dev, zone, zoneColors);
}

int RgbService::setAllColor(const QColor& color) {
    int ok = 0;
    for (int i = 0; i < m_all.size(); ++i)
        if (writable(i) && setDeviceColor(i, color)) ++ok;
    return ok;
}

bool RgbService::setDeviceMode(int dev, int modeIndex, const QVariantMap& p) {
    if (!writable(dev)) return false;
    stopSoftFx(dev);
    const auto& c = m_all[dev];
    if (modeIndex < 0 || modeIndex >= c.modes.size()) return false;
    orgb::Mode m = c.modes[modeIndex];
    if (p.contains(u"speed"_s) && m.has(orgb::HasSpeed)) m.speed = fromPct(p.value(u"speed"_s).toInt(), m.speedMin, m.speedMax);
    if (p.contains(u"brightness"_s) && m.has(orgb::HasBrightness))
        m.brightness = fromPct(p.value(u"brightness"_s).toInt(), m.brightnessMin, m.brightnessMax);
    if (p.contains(u"direction"_s)) m.direction = uint32_t(p.value(u"direction"_s).toInt());
    if (p.contains(u"random"_s) && m.has(orgb::HasRandomColor) && p.value(u"random"_s).toBool()) m.colorMode = orgb::ColorRandom;
    if (p.contains(u"colors"_s) && m.has(orgb::HasModeSpecificColor)) {
        QList<QRgb> cols = rgbList(p.value(u"colors"_s).toList());
        if (!cols.isEmpty()) {
            const int n = qBound(int(m.colorsMin), int(cols.size()), int(qMax(m.colorsMax, m.colorsMin)));
            m.colors = fitTo(cols, n);
            m.colorMode = orgb::ColorModeSpecific;
        }
    }
    if (m.has(orgb::HasPerLedColor) && !m.has(orgb::HasModeSpecificColor) && m.colorMode != orgb::ColorRandom)
        m.colorMode = orgb::ColorPerLed;
    const bool ok = doUpdateMode(dev, modeIndex, m);
    if (ok && m.isPerLed()) {
        const QString key = RgbDeviceModel::deviceKey(c);
        if (m_baseColors.contains(key)) writeLeds(dev, m_baseColors.value(key));
    }
    return ok;
}

bool RgbService::setDeviceBrightness(int dev, int pct) {
    if (!writable(dev)) return false;
    if (auto it = m_softFx.find(RgbDeviceModel::deviceKey(m_all[dev])); it != m_softFx.end()) {
        it->brightnessPct = pct;
        m_model.setSoftBrightness(dev, pct);
        return true;
    }
    const auto& c = m_all[dev];
    const orgb::Mode* cur = c.currentMode();
    if (!cur) return false;
    if (cur->has(orgb::HasBrightness)) {
        orgb::Mode m = *cur;
        m.brightness = fromPct(pct, m.brightnessMin, m.brightnessMax);
        return doUpdateMode(dev, c.activeMode, m);
    }
    if (cur->isPerLed()) {  // software dimming of the stored, unscaled colours
        const QString key = RgbDeviceModel::deviceKey(c);
        const QList<QRgb> base = m_baseColors.value(key, c.colors);
        m_model.setSoftBrightness(dev, pct);
        return writeLeds(dev, base);
    }
    return false;
}

bool RgbService::applyEffect(int dev, const QString& effect, const QVariantList& colors, int speedPct, int brightnessPct) {
    if (!writable(dev)) return false;
    stopSoftFx(dev);
    const auto& c = m_all[dev];
    const QList<QRgb> cols = rgbList(colors);

    if (effect == u"static"_s) {
        m_model.setSoftBrightness(dev, brightnessPct);
        const bool ok = setDeviceColor(dev, cols.isEmpty() ? QColor(Qt::white) : QColor(cols.first()));
        const orgb::Mode* cur = m_all[dev].currentMode();
        if (ok && cur && cur->has(orgb::HasBrightness)) setDeviceBrightness(dev, brightnessPct);
        return ok;
    }

    for (const auto& re : effectPatterns(effect)) {
        for (int i = 0; i < c.modes.size(); ++i) {
            if (!re.match(c.modes[i].name).hasMatch()) continue;
            QVariantMap p{{u"speed"_s, speedPct}, {u"brightness"_s, brightnessPct}};
            if (!cols.isEmpty()) p.insert(u"colors"_s, colors);
            return setDeviceMode(dev, i, p);
        }
    }

    // Device lacks a matching hardware effect: emulate statically where sensible.
    if (effect == u"off"_s) {
        if (ensurePerLed(dev)) return writeLeds(dev, QList<QRgb>(c.leds.size(), qRgb(0, 0, 0)));
        return setDeviceColor(dev, Qt::black);
    }
    if ((effect == u"rainbow"_s || effect == u"breathing"_s) && ensurePerLed(dev)) {
        startSoftFx(dev, effect, cols, speedPct, brightnessPct);
        return true;
    }
    if (!cols.isEmpty()) return setDeviceColor(dev, QColor(cols.first()));
    return false;
}

int RgbService::applyEffectAll(const QString& effect, const QVariantList& colors, int speedPct, int brightnessPct) {
    int ok = 0;
    for (int i = 0; i < m_all.size(); ++i)
        if (writable(i) && applyEffect(i, effect, colors, speedPct, brightnessPct)) ++ok;
    return ok;
}

QList<QRgb> RgbService::rgbList(const QVariantList& colors) {
    QList<QRgb> out;
    for (const QVariant& v : colors) {
        const QColor c = v.canConvert<QColor>() ? v.value<QColor>() : QColor(v.toString());
        if (c.isValid()) out.append(c.rgb());
    }
    return out;
}

// ---------------------------------------------------------------- profiles

QVariantMap RgbService::captureState() const {
    QVariantList devices;
    const auto& list = m_all;
    for (int i = 0; i < list.size(); ++i) {
        const auto& c = list[i];
        if (RgbDeviceModel::isKraken(c)) continue;
        const orgb::Mode* m = c.currentMode();
        if (!m) continue;
        QVariantList cols;
        const QList<QRgb> src = m->isPerLed() ? m_baseColors.value(RgbDeviceModel::deviceKey(c), c.colors) : m->colors;
        for (QRgb rgb : src) cols.append(QColor(rgb).name());
        QVariantMap e{{u"name"_s, c.name}, {u"serial"_s, c.serial}, {u"location"_s, c.location},
                      {u"mode"_s, m->name},  {u"colors"_s, cols},     {u"direction"_s, int(m->direction)}};
        if (const auto fx = m_softFx.constFind(RgbDeviceModel::deviceKey(c)); fx != m_softFx.cend()) {
            QVariantList fxCols;
            for (QRgb rgb : fx->colors) fxCols.append(QColor(rgb).name());
            e.insert(u"softEffect"_s, fx->effect);
            e.insert(u"softColors"_s, fxCols);
            e.insert(u"speed"_s, fx->speedPct);
            e.insert(u"brightness"_s, fx->brightnessPct);
            devices.append(e);
            continue;
        }
        if (m->has(orgb::HasSpeed)) e.insert(u"speed"_s, m->speedMin == m->speedMax ? 100 : int(std::lround(100.0 * (double(m->speed) - m->speedMin) / (double(m->speedMax) - m->speedMin))));
        if (m->has(orgb::HasBrightness))
            e.insert(u"brightness"_s, m->brightnessMin == m->brightnessMax ? 100 : int(std::lround(100.0 * (double(m->brightness) - m->brightnessMin) / (double(m->brightnessMax) - m->brightnessMin))));
        else if (m->isPerLed())
            e.insert(u"brightness"_s, m_model.softBrightness(i));
        devices.append(e);
    }
    return {{u"devices"_s, devices}};
}

int RgbService::findDevice(const QVariantMap& e) const {
    const auto& list = m_all;
    const QString name = e.value(u"name"_s).toString(), serial = e.value(u"serial"_s).toString(),
                  location = e.value(u"location"_s).toString();
    int byName = -1, nameHits = 0;
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].name != name) continue;
        if (!serial.isEmpty() && list[i].serial == serial) return i;
        if (serial.isEmpty() && list[i].location == location) return i;
        byName = i;
        ++nameHits;
    }
    return nameHits == 1 ? byName : -1;  // ambiguous name-only matches are skipped
}

QStringList RgbService::applyState(const QVariantMap& rgb) {
    QStringList warnings;
    if (!connected()) {
        warnings << u"OpenRGB not connected; only natively supported devices were updated."_s;
        if (m_native.controllers().isEmpty()) return warnings;
    }
    // 1) Global effect for every device (devices with explicit entries override below).
    const QVariantMap all = rgb.value(u"all"_s).toMap();
    QSet<int> explicitDevs;
    QList<QPair<int, QVariantMap>> entries;
    for (const QVariant& v : rgb.value(u"devices"_s).toList()) {
        const QVariantMap e = v.toMap();
        const int dev = findDevice(e);
        if (dev < 0) {
            warnings << u"Device “%1” from the profile is not connected."_s.arg(e.value(u"name"_s).toString());
            continue;
        }
        explicitDevs.insert(dev);
        entries.append({dev, e});
    }
    if (!all.isEmpty()) {
        for (int i = 0; i < m_all.size(); ++i) {
            if (explicitDevs.contains(i) || !writable(i)) continue;
            if (!applyEffect(i, all.value(u"effect"_s, u"static"_s).toString(), all.value(u"colors"_s).toList(),
                             all.value(u"speed"_s, 50).toInt(), all.value(u"brightness"_s, 100).toInt()))
                warnings << u"%1: could not apply “%2”."_s.arg(m_all[i].name, all.value(u"effect"_s).toString());
        }
    }
    // 2) Per-device entries.
    for (const auto& [dev, e] : entries) {
        if (!writable(dev)) continue;
        if (e.contains(u"softEffect"_s)) {
            applyEffect(dev, e.value(u"softEffect"_s).toString(), e.value(u"softColors"_s).toList(),
                        e.value(u"speed"_s, 50).toInt(), e.value(u"brightness"_s, 100).toInt());
            continue;
        }
        const auto& c = m_all[dev];
        const QString modeName = e.value(u"mode"_s).toString();
        int idx = -1;
        for (int i = 0; i < c.modes.size(); ++i)
            if (c.modes[i].name == modeName) idx = i;
        if (idx < 0) {
            warnings << u"%1 no longer offers mode “%2”."_s.arg(c.name, modeName);
            continue;
        }
        const orgb::Mode& m = c.modes[idx];
        const bool perLed = m.has(orgb::HasPerLedColor) && !(m.has(orgb::HasModeSpecificColor) && m.colorMode == orgb::ColorModeSpecific);
        QVariantMap p;
        if (e.contains(u"speed"_s)) p.insert(u"speed"_s, e.value(u"speed"_s));
        if (e.contains(u"brightness"_s) && m.has(orgb::HasBrightness)) p.insert(u"brightness"_s, e.value(u"brightness"_s));
        if (e.contains(u"direction"_s)) p.insert(u"direction"_s, e.value(u"direction"_s));
        if (!perLed) p.insert(u"colors"_s, e.value(u"colors"_s));
        if (perLed && e.contains(u"brightness"_s)) m_model.setSoftBrightness(dev, e.value(u"brightness"_s).toInt());
        setDeviceMode(dev, idx, p);
        if (perLed) writeLeds(dev, fitTo(rgbList(e.value(u"colors"_s).toList()), int(c.leds.size())));
    }
    return warnings;
}

// ---------------------------------------------------------------- OpenRGB config

bool RgbService::openRgbConfigReadable() const {
    QFile f(openRgbConfigPath());
    return f.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(f.readAll()).isObject();
}

QString RgbService::openRgbConfigFile() const { return openRgbConfigPath(); }

QVariantList RgbService::krakenDetectorCandidates(const QString& krakenDescription) const {
    QVariantList out;
    // Plain read: never "repair" another application's config file.
    QFile f(openRgbConfigPath());
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonObject cfg = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject det = cfg.value(u"Detectors"_s).toObject().value(u"detectors"_s).toObject();
    // Score detector names by shared tokens with the cooler's description
    // ("NZXT Kraken 2024 Elite RGB" vs "NZXT Kraken 2024 ELITE Series RGB").
    const QStringList tokens = krakenDescription.toLower().split(QRegularExpression(u"[^a-z0-9]+"_s), Qt::SkipEmptyParts);
    int bestScore = 0;
    QList<QPair<QString, int>> scored;
    for (auto it = det.begin(); it != det.end(); ++it) {
        if (!it.key().contains(u"NZXT Kraken"_s, Qt::CaseInsensitive)) continue;
        const QStringList dt = it.key().toLower().split(QRegularExpression(u"[^a-z0-9]+"_s), Qt::SkipEmptyParts);
        int score = 0;
        for (const QString& t : tokens)
            if (dt.contains(t)) ++score;
        scored.append({it.key(), score});
        bestScore = qMax(bestScore, score);
    }
    for (const auto& [name, score] : scored)
        out.append(QVariantMap{{u"name"_s, name},
                               {u"enabled"_s, det.value(name).toBool(true)},
                               {u"recommended"_s, bestScore > 2 && score == bestScore}});
    return out;
}

QVariantMap RgbService::disableOpenRgbDetectors(const QStringList& names) {
    refreshProcessState();
    if (m_openRgbRunning)
        return {{u"ok"_s, false}, {u"message"_s, u"Close OpenRGB first — it rewrites OpenRGB.json when it saves settings."_s}};
    const QString path = openRgbConfigPath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {{u"ok"_s, false}, {u"message"_s, u"Cannot read %1"_s.arg(path)}};
    const QByteArray original = f.readAll();
    f.close();
    QJsonParseError pe{};
    QJsonDocument doc = QJsonDocument::fromJson(original, &pe);
    if (!doc.isObject()) return {{u"ok"_s, false}, {u"message"_s, u"OpenRGB.json is not valid JSON: %1"_s.arg(pe.errorString())}};

    const QString backup = path + u".orkc-backup-"_s + QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss"_s);
    QString err;
    if (!JsonStore::writeAtomicBytes(backup, original, &err)) return {{u"ok"_s, false}, {u"message"_s, u"Backup failed: "_s + err}};

    QJsonObject root = doc.object();
    QJsonObject detectors = root.value(u"Detectors"_s).toObject();
    QJsonObject list = detectors.value(u"detectors"_s).toObject();
    for (const QString& n : names) list.insert(n, false);
    detectors.insert(u"detectors"_s, list);
    root.insert(u"Detectors"_s, detectors);
    if (!JsonStore::writeAtomic(path, root, &err)) return {{u"ok"_s, false}, {u"message"_s, u"Write failed: "_s + err}};
    return {{u"ok"_s, true},
            {u"backup"_s, backup},
            {u"message"_s, u"Disabled %1 in OpenRGB. Backup: %2. Start OpenRGB again (with its SDK server)."_s.arg(names.join(u", "_s), backup)}};
}
