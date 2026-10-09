#include "devices/DeviceCatalog.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSysInfo>

#include <unistd.h>

using namespace Qt::StringLiterals;

namespace catalog {

namespace {
QString readSys(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}
}  // namespace

QString backendForRulesFile(const QString& fileName) {
    const QString f = fileName.toLower();
    if (f.contains(u"openrgb"_s)) return u"OpenRGB"_s;
    if (f.contains(u"liquidctl"_s)) return u"liquidctl"_s;
    if (f.contains(u"razer"_s)) return u"OpenRazer"_s;
    if (f.contains(u"orkc"_s) || f.contains(u"sudorgb"_s)) return u"SudoRGB"_s;
    return {};
}

QList<Rule> parseRules(const QString& text, const QString& backend) {
    static const QRegularExpression vidRe(uR"(ATTRS\{idVendor\}==\"([0-9a-fA-F]{4})\")"_s);
    static const QRegularExpression pidRe(uR"(ATTRS\{idProduct\}==\"([0-9a-fA-F|]+)\")"_s);
    static const QRegularExpression tagRe(uR"(TAG\+=\"([^\"]+)\")"_s);
    QList<Rule> out;
    for (const QString& raw : text.split(u'\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) continue;
        const auto v = vidRe.match(line);
        if (!v.hasMatch()) continue;
        QString hint;
        for (auto it = tagRe.globalMatch(line); it.hasNext();) {
            const QString t = it.next().captured(1);
            if (t != u"uaccess"_s) hint = QString(t).replace(u'_', u' ');
        }
        const uint16_t vid = uint16_t(v.captured(1).toUInt(nullptr, 16));
        const auto p = pidRe.match(line);
        if (!p.hasMatch()) {
            out.append({backend, vid, -1, hint});
            continue;
        }
        for (const QString& pid : p.captured(1).split(u'|', Qt::SkipEmptyParts))  // "821d|822a" alternatives
            out.append({backend, vid, int(pid.toUInt(nullptr, 16)), hint});
    }
    return out;
}

void parseUsbIds(const QString& text, QHash<uint32_t, QString>* names) {
    uint32_t vid = 0;
    bool inVendors = true;
    for (const QString& line : text.split(u'\n')) {
        if (line.isEmpty() || line.startsWith(u'#')) continue;
        if (line.startsWith(u'C') && line.size() > 1 && line[1] == u' ') {  // class section follows the vendor list
            inVendors = false;
            continue;
        }
        if (!inVendors) continue;
        if (line[0] != u'\t') {
            vid = line.left(4).toUInt(nullptr, 16);
            names->insert(vid << 16 | 0xFFFF, line.mid(6).trimmed());
        } else if (line.size() > 6 && line[1] != u'\t') {
            names->insert(vid << 16 | line.mid(1, 4).toUInt(nullptr, 16), line.mid(7).trimmed());
        }
    }
}

bool isRgbVendor(uint16_t vid) {
    // Vendors that make RGB peripherals/components (ids we are confident about).
    static const QList<uint16_t> v{0x1b1c, 0x1532, 0x046d, 0x1038, 0x0b05, 0x1462, 0x1e71, 0x0951, 0x03f0, 0x2516,
                                   0x264a, 0x1e7d, 0x10f5, 0x3842, 0x0cf2, 0x1044, 0x26ce, 0x3434, 0x31e3, 0x320f,
                                   0x258a, 0x093a};
    return v.contains(vid);
}

bool usableBy(const QStringList& backends, uint16_t vid, uint16_t pid) {
    static const QList<uint16_t> krakenService{0x3008, 0x300c, 0x300e, 0x3012, 0x3014};  // liquidctl KrakenZ3 family
    return backends.contains(u"OpenRGB"_s) || backends.contains(u"SudoRGB"_s) ||
           (backends.contains(u"liquidctl"_s) && vid == 0x1e71 && krakenService.contains(pid));
}

QString describeStatus(const QStringList& backends, bool usable, bool hidWritable, bool rgbVendor) {
    if (usable) return hidWritable ? u"supported"_s : u"permissions"_s;  // permissions: missing/unreloaded udev rule
    if (!backends.isEmpty()) return u"known"_s;
    return rgbVendor ? u"unsupported"_s : u"other"_s;
}

void Registry::load() {
    rules.clear();
    rulesFiles.clear();
    for (const QString& dir : {u"/usr/lib/udev/rules.d"_s, u"/lib/udev/rules.d"_s, u"/usr/local/lib/udev/rules.d"_s,
                               u"/etc/udev/rules.d"_s}) {
        for (const QFileInfo& fi : QDir(dir).entryInfoList({u"*.rules"_s}, QDir::Files)) {
            const QString backend = backendForRulesFile(fi.fileName());
            if (backend.isEmpty() || rulesFiles.contains(fi.canonicalFilePath())) continue;
            QFile f(fi.filePath());
            if (!f.open(QIODevice::ReadOnly)) continue;
            rules += parseRules(QString::fromUtf8(f.readAll()), backend);
            rulesFiles << fi.canonicalFilePath();
        }
    }
    names.clear();
    for (const QString& p : {u"/usr/share/hwdata/usb.ids"_s, u"/usr/share/misc/usb.ids"_s, u"/usr/share/usb.ids"_s}) {
        QFile f(p);
        if (f.open(QIODevice::ReadOnly)) {
            parseUsbIds(QString::fromUtf8(f.readAll()), &names);
            break;
        }
    }
}

QStringList Registry::backendsFor(uint16_t vid, uint16_t pid, QString* productHint) const {
    QStringList out;
    for (const Rule& r : rules) {
        if (r.vid != vid || (r.pid != -1 && r.pid != pid)) continue;
        // Vendor-wide rules (no product id) only grant permissions; they do not prove support.
        if (r.pid == -1 && r.backend != u"OpenRazer"_s) continue;
        if (!out.contains(r.backend)) out << r.backend;
        if (productHint && productHint->isEmpty() && !r.product.isEmpty()) *productHint = r.product;
    }
    return out;
}

QString Registry::nameFor(uint16_t vid, uint16_t pid) const {
    return names.value(uint32_t(vid) << 16 | pid);
}

QList<UsbDevice> Registry::scanUsb() const {
    QList<UsbDevice> out;
    const QDir root(u"/sys/bus/usb/devices"_s);
    for (const QString& name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System)) {
        if (name.contains(u':') || name.startsWith(u"usb"_s)) continue;  // interfaces and root hubs
        const QString path = QFileInfo(root.absoluteFilePath(name)).canonicalFilePath();
        if (readSys(path + u"/bDeviceClass"_s) == u"09"_s) continue;     // hubs
        UsbDevice d;
        d.vid = uint16_t(readSys(path + u"/idVendor"_s).toUInt(nullptr, 16));
        d.pid = uint16_t(readSys(path + u"/idProduct"_s).toUInt(nullptr, 16));
        if (!d.vid) continue;
        d.manufacturer = readSys(path + u"/manufacturer"_s);
        d.product = readSys(path + u"/product"_s);
        d.sysPath = path;
        for (QDirIterator it(path, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories); it.hasNext();) {
            const QString p = it.next();
            if (QFileInfo(p).dir().dirName() != u"hidraw"_s) continue;
            const QString node = u"/dev/"_s + QFileInfo(p).fileName();
            d.hidraw << node;
            if (::access(node.toLocal8Bit().constData(), R_OK | W_OK) == 0) d.hidWritable = true;
        }
        out << d;
    }
    return out;
}

QVariantList Registry::devices(bool includeAll) const {
    QVariantList out;
    for (const UsbDevice& d : scanUsb()) {
        QString hint;
        const QStringList backends = backendsFor(d.vid, d.pid, &hint);
        const bool rgb = isRgbVendor(d.vid);
        // A device without any HID interface cannot be a HID lighting device unless a backend claims it.
        const QString status = describeStatus(backends, usableBy(backends, d.vid, d.pid), d.hidWritable || d.hidraw.isEmpty(),
                                              rgb && !d.hidraw.isEmpty());
        if (!includeAll && status == u"other"_s) continue;
        QString name = d.product;
        if (name.isEmpty()) name = nameFor(d.vid, d.pid);
        if (name.isEmpty()) name = hint;
        QString vendor = d.manufacturer.isEmpty() ? names.value(uint32_t(d.vid) << 16 | 0xFFFF) : d.manufacturer;
        out.append(QVariantMap{
            {u"name"_s, name.isEmpty() ? u"Unknown device"_s : name},
            {u"vendor"_s, vendor},
            {u"id"_s, u"%1:%2"_s.arg(d.vid, 4, 16, QLatin1Char('0')).arg(d.pid, 4, 16, QLatin1Char('0'))},
            {u"backends"_s, backends},
            {u"status"_s, status},
            {u"hidraw"_s, d.hidraw.join(u", "_s)},
            {u"hint"_s, hint},
        });
    }
    return out;
}

QVariantMap Registry::summary() const {
    QHash<QString, QSet<uint32_t>> perBackend;
    for (const Rule& r : rules)
        if (r.pid != -1) perBackend[r.backend].insert(uint32_t(r.vid) << 16 | uint32_t(r.pid));
    QVariantMap counts;
    for (auto it = perBackend.cbegin(); it != perBackend.cend(); ++it) counts.insert(it.key(), int(it.value().size()));
    return {{u"backends"_s, counts}, {u"rulesFiles"_s, rulesFiles}, {u"namesLoaded"_s, !names.isEmpty()}};
}

QString Registry::diagnosticsReport(const QVariantMap& extra) const {
    QString r;
    auto line = [&r](const QString& s) { r += s + u'\n'; };
    line(u"SudoRGB diagnostics — %1"_s.arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    line(u"Share this file when requesting support for a device. It contains no personal files,"_s);
    line(u"only hardware ids, system versions and which backends are installed."_s);
    line({});
    line(u"OS: %1 | kernel %2 | %3"_s.arg(QSysInfo::prettyProductName(), QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture()));
    line(u"Session: %1 / %2"_s.arg(qEnvironmentVariable("XDG_SESSION_TYPE"), qEnvironmentVariable("XDG_CURRENT_DESKTOP")));
    for (auto it = extra.cbegin(); it != extra.cend(); ++it) line(it.key() + u": "_s + it.value().toString());
    line(u"Backend rule files:"_s);
    for (const QString& f : rulesFiles) line(u"  "_s + f);
    const QVariantMap counts = summary().value(u"backends"_s).toMap();
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) line(u"  %1 knows %2 USB devices"_s.arg(it.key()).arg(it.value().toInt()));
    line({});
    line(u"Connected USB devices (status | id | name | backends | hidraw):"_s);
    for (const UsbDevice& d : scanUsb()) {
        const QStringList b = backendsFor(d.vid, d.pid);
        const QString status = describeStatus(b, usableBy(b, d.vid, d.pid), d.hidWritable || d.hidraw.isEmpty(),
                                              isRgbVendor(d.vid) && !d.hidraw.isEmpty());
        line(u"  %1 | %2:%3 | %4 %5 | %6 | %7%8"_s.arg(status, -11)
                 .arg(d.vid, 4, 16, QLatin1Char('0')).arg(d.pid, 4, 16, QLatin1Char('0'))
                 .arg(d.manufacturer, d.product.isEmpty() ? nameFor(d.vid, d.pid) : d.product)
                 .arg(b.isEmpty() ? u"-"_s : b.join(u','), d.hidraw.join(u' '))
                 .arg(d.hidraw.isEmpty() ? QString() : (d.hidWritable ? u" (rw)"_s : u" (no access)"_s)));
        // Unsupported RGB-vendor devices: include HID report descriptors, the most useful
        // information for anyone writing a driver.
        if (status == u"unsupported"_s) {
            for (QDirIterator it(d.sysPath, {u"report_descriptor"_s}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
                const QString p = it.next();
                QFile f(p);
                if (f.open(QIODevice::ReadOnly))
                    line(u"      %1: %2"_s.arg(QFileInfo(p).dir().dirName(), QString::fromLatin1(f.readAll().toHex(' '))));
            }
        }
    }
    return r;
}

}  // namespace catalog

QString HardwareCatalog::exportDiagnostics(const QVariantMap& extra) const {
    const QString path = QDir::homePath() + u"/sudorgb-diagnostics-"_s +
                         QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss"_s) + u".txt"_s;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return {};
    f.write(m_reg.diagnosticsReport(extra).toUtf8());
    return path;
}
