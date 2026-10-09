#include "devices/NativeDevices.h"

#include "core/JsonStore.h"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcNative, "orkc.native")

namespace glorious {

namespace {
constexpr uint16_t kVendor = 0x093a;
struct Product { uint16_t pid; const char* name; };
// Receiver/dongle and wired PIDs of the PixArt Model O 2 / I 2 family.
constexpr Product kProducts[] = {
    {0x821d, "Glorious Model I 2 Wireless"},
    {0x822a, "Glorious Model O 2 Wireless"},
    {0x822b, "Glorious Model O 2 (Bluetooth)"},
    {0x822d, "Glorious Model O 2 Wireless"},
    {0x826a, "Glorious Model O 2 Mini Wireless"},
    {0x826d, "Glorious Model O 2 Mini Wireless"},
};
constexpr std::initializer_list<uint8_t> kSpeed = {0x05, 0x0a, 0x0f, 0x14};
const QList<QRgb> kRainbow = {qRgb(255, 0, 0), qRgb(255, 127, 0), qRgb(255, 255, 0), qRgb(0, 255, 0),
                              qRgb(0, 0, 255), qRgb(75, 0, 130), qRgb(148, 0, 211)};

int paletteUse(uint8_t effect) {
    if (effect == Rave) return 2;
    if (effect == Off || effect == Static || effect == Breathing) return 1;
    return kPaletteSize;
}
}  // namespace

uint8_t quantize(uint32_t value, std::initializer_list<uint8_t> levels) {
    uint8_t best = *levels.begin();
    for (uint8_t l : levels)
        if (std::abs(int(l) - int(value)) < std::abs(int(best) - int(value))) best = l;
    return best;
}

QList<QByteArray> lightingPayload(uint8_t effect, uint32_t brightness, uint32_t speed, const QList<QRgb>& colors) {
    QList<QByteArray> frags;
    for (int i = 0; i < 3; ++i) {
        QByteArray f(kPacketLength, '\0');
        f[0] = char(kReportId);
        f[1] = char(0x02);  // lighting command 02 FB
        f[2] = char(0xfb);
        f[3] = char(i);     // fragment index
        f[4] = char(0x01);
        f[5] = char(effect);
        frags.append(f);
    }
    QList<QRgb> pal = colors;
    for (int i = int(pal.size()); i < kPaletteSize; ++i) pal.append(kRainbow[i]);
    QByteArray& a = frags[0];
    // [6]/[7] are labelled wireless/wired brightness but have no visible effect;
    // 0 there makes the firmware fall back to its stored profile, so keep them full.
    a[6] = char(0x14);
    a[7] = char(0x14);
    a[8] = char(paletteUse(effect));
    a[9] = char(quantize(speed, kSpeed));
    // [10] is the real (master) brightness, 0x01..0x14; 0x00 also reverts to the stored profile.
    a[10] = char(std::clamp<uint32_t>(brightness, 0x01, 0x14));
    a[11] = char(qRed(pal[0]));
    a[12] = char(qGreen(pal[0]));
    a[13] = char(qBlue(pal[0]));
    for (int i = 1; i < kPaletteSize; ++i) {
        frags[1][3 + i * 3] = char(qRed(pal[i]));
        frags[1][4 + i * 3] = char(qGreen(pal[i]));
        frags[1][5 + i * 3] = char(qBlue(pal[i]));
    }
    return frags;
}

QList<orgb::Mode> modes() {
    struct Def { const char* name; Effect fx; bool speed; int cmin, cmax; };
    // Named so the app's generic effects map onto them ("rainbow", "breathing", "off").
    const Def defs[] = {
        {"Static", Static, false, 1, 1},       {"Breathing", Breathing, true, 1, 1},
        {"Breathing Cycle", BreathingCycle, true, 1, 7}, {"Seamless Breathing", SeamlessBreathing, true, 0, 0},
        {"Rainbow", Rainbow, true, 0, 0},      {"Wave", Wave, true, 0, 0},
        {"Tail", Tail, true, 1, 7},            {"Rave", Rave, true, 2, 2},
        {"Off", Off, false, 0, 0},
    };
    QList<orgb::Mode> out;
    for (const Def& d : defs) {
        orgb::Mode m;
        m.name = QString::fromLatin1(d.name);
        m.value = d.fx;
        // No HasSpeed: payload byte 9 ("speed") has no visible effect on this firmware
        // (gloriousctl-linux notes + our hardware test), so don't offer a dead slider.
        m.flags = (d.fx == Off ? 0u : uint32_t(orgb::HasBrightness)) |
                  (d.cmax > 0 ? uint32_t(orgb::HasModeSpecificColor) : 0u);
        m.brightnessMin = 0x00; m.brightnessMax = 0x14; m.brightness = 0x14;
        m.speedMin = 0x05;      m.speedMax = 0x14;      m.speed = 0x0a;
        m.colorsMin = uint32_t(d.cmin); m.colorsMax = uint32_t(d.cmax);
        m.colorMode = d.cmax > 0 ? orgb::ColorModeSpecific : orgb::ColorNone;
        for (int i = 0; i < d.cmin; ++i) m.colors.append(i == 0 ? qRgb(255, 255, 255) : kRainbow[i]);
        out.append(m);
    }
    return out;
}

}  // namespace glorious

// ---------------------------------------------------------------- NativeDevices

namespace {
// Each lighting command makes the mouse's wireless firmware store the settings.
// While it stores, it drops further fragments AND stalls cursor reports (felt as
// skipping). So: 120 ms between the three fragments (gloriousctl-linux's measured
// value) and at least ~1 s between whole commands, newest colour wins.
constexpr int kFragmentGapMs = 120;
constexpr int kCommandGapMs = 1000;
QString readSys(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}
QString statePath() { return JsonStore::stateDir() + u"/native-devices.json"_s; }
}  // namespace

NativeDevices::NativeDevices(QObject* parent) : QObject(parent) {
    // Latest-wins write coalescing: a colour-wheel drag never queues up radio traffic.
    m_flush.setSingleShot(true);
    m_fragTimer.setSingleShot(true);
    connect(&m_fragTimer, &QTimer::timeout, this, &NativeDevices::sendNextFragment);
    connect(&m_flush, &QTimer::timeout, this, &NativeDevices::flush);
    // Hot-plug: sysfs reads only, cheap.
    m_scan.setInterval(5000);
    connect(&m_scan, &QTimer::timeout, this, &NativeDevices::rescan);
}

void NativeDevices::start() {
    if (qEnvironmentVariableIntValue("ORKC_NO_NATIVE_DEVICES") == 1) return;  // tests: never touch real hardware
    rescan();
    m_scan.start();
}

void NativeDevices::rescan() {
    QList<Hw> found;
    QList<QString> names;
    const QDir dir(u"/sys/class/hidraw"_s);
    for (const QString& h : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name)) {
        const QString base = dir.absoluteFilePath(h) + u"/device/"_s;
        // HID_ID=0003:0000093A:0000826D
        const QString uevent = readSys(base + u"uevent"_s);
        const auto idLine = uevent.section(u"HID_ID="_s, 1, 1).section(u'\n', 0, 0);
        const QStringList ids = idLine.split(u':');
        if (ids.size() != 3 || ids[1].toUInt(nullptr, 16) != glorious::kVendor) continue;
        const uint16_t pid = uint16_t(ids[2].toUInt(nullptr, 16));
        const auto* prod = std::find_if(std::begin(glorious::kProducts), std::end(glorious::kProducts),
                                        [pid](const auto& p) { return p.pid == pid; });
        if (prod == std::end(glorious::kProducts)) continue;  // other PixArt devices share this VID
        // Only the vendor interface (usage page 0xFF00 with feature report 3) takes lighting.
        QFile rd(base + u"report_descriptor"_s);
        if (!rd.open(QIODevice::ReadOnly)) continue;
        const QByteArray desc = rd.readAll();
        if (!desc.contains(QByteArray("\x06\x00\xff", 3)) || !desc.contains(QByteArray("\x85\x03", 2))) continue;
        found.append({u"/dev/"_s + h, pid});
        names.append(QString::fromLatin1(prod->name));
    }

    bool same = found.size() == m_hw.size();
    for (int i = 0; same && i < found.size(); ++i) same = found[i].node == m_hw[i].node && found[i].pid == m_hw[i].pid;
    if (same) return;

    m_hw = found;
    m_controllers.clear();
    m_pending.clear();
    m_lastSent.clear();
    m_outbox.clear();
    for (int i = 0; i < found.size(); ++i) {
        orgb::Controller c;
        c.type = 6;  // mouse
        c.name = names[i];
        c.vendor = u"Glorious"_s;
        c.description = u"Driven natively by ORKC (no OpenRGB driver for this model)"_s;
        c.serial = u"093a:%1"_s.arg(found[i].pid, 4, 16, QLatin1Char('0'));  // stable key across reboots
        c.location = u"HID: "_s + found[i].node;
        c.modes = glorious::modes();
        c.activeMode = 0;
        orgb::Zone z;
        z.name = u"Mouse"_s;
        z.ledsMin = z.ledsMax = z.ledsCount = 1;
        c.zones = {z};
        c.leds = {{u"Mouse"_s, 0}};
        c.colors = {c.modes[0].colors.value(0, qRgb(255, 255, 255))};
        m_controllers.append(c);
    }
    loadState();
    qCInfo(lcNative) << "native RGB devices:" << names;
    Q_EMIT controllersReset();
}

bool NativeDevices::updateMode(int index, int modeIndex, const orgb::Mode& mode) {
    if (index < 0 || index >= m_controllers.size()) return false;
    auto& c = m_controllers[index];
    if (modeIndex < 0 || modeIndex >= c.modes.size()) return false;
    orgb::Mode m = mode;
    m.value = c.modes[modeIndex].value;  // the effect id is ours, never the caller's
    c.modes[modeIndex] = m;
    c.activeMode = modeIndex;
    c.colors = {m.colors.value(0, c.colors.value(0, qRgb(0, 0, 0)))};
    m_pending.insert(index, m);
    scheduleFlush();
    Q_EMIT controllerChanged(index);
    return true;
}

void NativeDevices::scheduleFlush() {
    if (m_flush.isActive() || !m_outbox.isEmpty()) return;  // a command is in flight; flush() runs after it
    const qint64 since = QDateTime::currentMSecsSinceEpoch() - m_lastWriteMs;
    m_flush.start(int(std::clamp<qint64>(qint64(kCommandGapMs) - since, 0, kCommandGapMs)));  // first change goes out at once
}

void NativeDevices::flush() {
    const auto pending = std::exchange(m_pending, {});
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        if (it.key() >= m_hw.size()) continue;
        const orgb::Mode& m = it.value();
        const auto frags = glorious::lightingPayload(uint8_t(m.value), m.brightness, m.speed, m.colors);
        const QByteArray joined = frags.join();
        if (m_lastSent.value(it.key()) == joined) continue;  // identical: don't make the mouse store it again
        m_lastSent.insert(it.key(), joined);
        for (const QByteArray& f : frags) m_outbox.append({it.key(), f});
    }
    if (!m_outbox.isEmpty()) sendNextFragment();
    saveState();
}

void NativeDevices::sendNextFragment() {
    if (m_outbox.isEmpty()) return;
    const auto [dev, frag] = m_outbox.takeFirst();
    QString err;
    if (dev < m_hw.size() && !write(m_hw[dev], {frag}, &err)) {
        m_outbox.removeIf([dev](const auto& o) { return o.first == dev; });  // abandon this command
        m_lastSent.remove(dev);                                              // so a retry is not deduped
        if (err != m_lastError) Q_EMIT error(err);
        m_lastError = err;
        qCWarning(lcNative) << err;
    } else {
        m_lastError.clear();
    }
    m_lastWriteMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_outbox.isEmpty()) m_fragTimer.start(kFragmentGapMs);
    else if (!m_pending.isEmpty()) scheduleFlush();
}

bool NativeDevices::write(const Hw& hw, const QList<QByteArray>& fragments, QString* err) const {
    const int fd = ::open(hw.node.toLocal8Bit().constData(), O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        *err = errno == EACCES
                   ? u"No permission to control the Glorious mouse (%1). Install ORKC's udev rule, then replug the receiver."_s.arg(hw.node)
                   : u"Cannot open %1: %2"_s.arg(hw.node, QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    for (const QByteArray& f : fragments) {
        QByteArray buf = f;
        if (::ioctl(fd, HIDIOCSFEATURE(buf.size()), buf.data()) < 0) {
            *err = u"Glorious mouse rejected lighting (%1): %2. Is the mouse awake and paired?"_s.arg(
                hw.node, QString::fromLocal8Bit(std::strerror(errno)));
            ::close(fd);
            return false;
        }
    }
    ::close(fd);
    return true;
}

// The firmware never reports its lighting, so remember what we last set for display.
void NativeDevices::saveState() const {
    QJsonObject root{{u"schema"_s, 1}};
    for (const auto& c : m_controllers) {
        const orgb::Mode* m = c.currentMode();
        if (!m) continue;
        QJsonArray cols;
        for (QRgb rgb : m->colors) cols.append(QColor(rgb).name());
        root.insert(c.serial, QJsonObject{{u"mode"_s, m->name}, {u"brightness"_s, int(m->brightness)},
                                          {u"speed"_s, int(m->speed)}, {u"colors"_s, cols}});
    }
    JsonStore::writeAtomic(statePath(), root);
}

void NativeDevices::loadState() {
    const QJsonObject root = JsonStore::read(statePath());
    for (auto& c : m_controllers) {
        const QJsonObject s = root.value(c.serial).toObject();
        if (s.isEmpty()) continue;
        for (int i = 0; i < c.modes.size(); ++i) {
            if (c.modes[i].name != s.value(u"mode"_s).toString()) continue;
            auto& m = c.modes[i];
            m.brightness = uint32_t(s.value(u"brightness"_s).toInt(int(m.brightness)));
            m.speed = uint32_t(s.value(u"speed"_s).toInt(int(m.speed)));
            QList<QRgb> cols;
            for (const QJsonValue& v : s.value(u"colors"_s).toArray()) cols.append(QColor(v.toString()).rgb());
            if (!cols.isEmpty()) m.colors = cols;
            c.activeMode = i;
            c.colors = {m.colors.value(0, qRgb(0, 0, 0))};
        }
    }
}
