#pragma once
// Hardware compatibility registry built at runtime from the backends that are
// actually installed, not from a hard-coded list:
//
//  * OpenRGB, liquidctl, OpenRazer and SudoRGB each ship udev rules that list
//    the USB vendor/product ids they drive. We parse those rules, so support
//    grows automatically when the user updates a backend, and we never copy
//    any project's driver code.
//  * usb.ids (hwdata) gives human-readable vendor/product names.
//  * Connected devices come from sysfs (no root needed).
//
// Devices that no backend claims are reported honestly as unsupported, with a
// diagnostics export so users can request support.

#include <QHash>
#include <QObject>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace catalog {

struct Rule {
    QString backend;   // "OpenRGB", "liquidctl", "OpenRazer", "SudoRGB"
    uint16_t vid = 0;
    int pid = -1;      // -1 = every product of this vendor
    QString product;   // name hint from the rule (OpenRGB tags), may be empty
};

struct UsbDevice {
    uint16_t vid = 0, pid = 0;
    QString manufacturer, product, sysPath;
    QStringList hidraw;        // /dev/hidrawN nodes of this device
    bool hidWritable = false;  // can the current user open at least one hidraw node read-write?
};

// Pure functions (unit-tested).
QList<Rule> parseRules(const QString& text, const QString& backend);
QString backendForRulesFile(const QString& fileName);  // "" if not a known backend
// usb.ids: vendor id -> name, and (vid<<16|pid) -> name.
void parseUsbIds(const QString& text, QHash<uint32_t, QString>* names);
// SudoRGB can drive a device if OpenRGB, a SudoRGB native driver, or SudoRGB's Kraken
// service (liquidctl KrakenZ3 family) claims it. Other backends merely *know* it.
bool usableBy(const QStringList& backends, uint16_t vid, uint16_t pid);
// "supported" | "permissions" | "known" (backend knows it, SudoRGB integration pending) | "unsupported" | "other"
QString describeStatus(const QStringList& backends, bool usable, bool hidWritable, bool rgbVendor);
bool isRgbVendor(uint16_t vid);

class Registry {
public:
    // Loads rules from the standard udev directories and usb.ids. Cheap; call once.
    void load();
    QList<UsbDevice> scanUsb() const;
    QVariantList devices(bool includeAll) const;   // for QML
    QVariantMap summary() const;
    QString diagnosticsReport(const QVariantMap& extra) const;
    QStringList backendsFor(uint16_t vid, uint16_t pid, QString* productHint = nullptr) const;
    QString nameFor(uint16_t vid, uint16_t pid) const;

    QList<Rule> rules;  // public for tests
    QHash<uint32_t, QString> names;
    QStringList rulesFiles;
};

}  // namespace catalog

// QML facade: scan on demand (sysfs only, cheap), so no background polling.
class HardwareCatalog : public QObject {
    Q_OBJECT
public:
    explicit HardwareCatalog(QObject* parent = nullptr) : QObject(parent) { m_reg.load(); }
    Q_INVOKABLE QVariantList devices(bool includeAll) const { return m_reg.devices(includeAll); }
    Q_INVOKABLE QVariantMap summary() const { return m_reg.summary(); }
    Q_INVOKABLE void reload() { m_reg.load(); }
    // Writes ~/sudorgb-diagnostics-<timestamp>.txt and returns its path ("" on failure).
    Q_INVOKABLE QString exportDiagnostics(const QVariantMap& extra) const;

private:
    catalog::Registry m_reg;
};
