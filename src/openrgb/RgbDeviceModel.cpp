#include "openrgb/RgbDeviceModel.h"


using namespace Qt::StringLiterals;

RgbDeviceModel::RgbDeviceModel(const QList<orgb::Controller>* list, QObject* parent) : QAbstractListModel(parent), m_list(list) {}

int RgbDeviceModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : int((*m_list).size());
}

bool RgbDeviceModel::isKraken(const orgb::Controller& c) {
    // The Kraken is owned exclusively by the Kraken service (liquidctl/HUE2).
    // If OpenRGB also detects it, both would drive the same HID node.
    return c.name.contains(u"Kraken"_s, Qt::CaseInsensitive) &&
           (c.vendor.contains(u"NZXT"_s, Qt::CaseInsensitive) || c.name.contains(u"NZXT"_s, Qt::CaseInsensitive));
}

QString RgbDeviceModel::deviceKey(const orgb::Controller& c) {
    // Locations like "HID: /dev/hidraw4" change across boots; serials usually don't.
    const QString id = c.serial.trimmed().isEmpty() ? c.location : c.serial;
    return c.name + u'|' + id;
}

QVariantMap RgbDeviceModel::modeToVariant(const orgb::Mode& m, int index) {
    QVariantList colors;
    for (QRgb c : m.colors) colors.append(QColor(c));
    return {
        {u"index"_s, index},
        {u"name"_s, m.name},
        {u"hasSpeed"_s, m.has(orgb::HasSpeed)},
        {u"hasBrightness"_s, m.has(orgb::HasBrightness)},
        {u"hasDirection"_s, m.has(orgb::HasDirectionLR) || m.has(orgb::HasDirectionUD) || m.has(orgb::HasDirectionHV)},
        {u"hasPerLedColor"_s, m.has(orgb::HasPerLedColor)},
        {u"hasModeSpecificColor"_s, m.has(orgb::HasModeSpecificColor)},
        {u"hasRandomColor"_s, m.has(orgb::HasRandomColor)},
        {u"colorMode"_s, int(m.colorMode)},
        {u"colorsMin"_s, int(m.colorsMin)},
        {u"colorsMax"_s, int(m.colorsMax)},
        {u"speedMin"_s, int(m.speedMin)},
        {u"speedMax"_s, int(m.speedMax)},
        {u"speed"_s, int(m.speed)},
        {u"brightnessMin"_s, int(m.brightnessMin)},
        {u"brightnessMax"_s, int(m.brightnessMax)},
        {u"brightness"_s, int(m.brightness)},
        {u"direction"_s, int(m.direction)},
        {u"colors"_s, colors},
    };
}

static int toPct(uint32_t v, uint32_t lo, uint32_t hi) {
    if (lo == hi) return 100;
    return qBound(0, int(std::lround(100.0 * (double(v) - lo) / (double(hi) - lo))), 100);
}

QVariant RgbDeviceModel::data(const QModelIndex& index, int role) const {
    const auto& list = (*m_list);
    if (!index.isValid() || index.row() >= list.size()) return {};
    const orgb::Controller& c = list[index.row()];
    const orgb::Mode* mode = c.currentMode();
    switch (role) {
    case NameRole: return c.name;
    case VendorRole: return c.vendor;
    case TypeNameRole: return orgb::deviceTypeName(c.type);
    case TypeIconRole: return orgb::deviceTypeIcon(c.type);
    case DescriptionRole: return c.description;
    case LocationRole: return c.location;
    case SerialRole: return c.serial;
    case LedCountRole: return int(c.leds.size());
    case ZonesRole: {
        QVariantList zones;
        for (int i = 0; i < c.zones.size(); ++i) {
            const auto& z = c.zones[i];
            const int start = c.zoneStart(i);
            zones.append(QVariantMap{{u"index"_s, i},
                                     {u"name"_s, z.name},
                                     {u"ledsCount"_s, int(z.ledsCount)},
                                     {u"resizable"_s, z.ledsMin != z.ledsMax},
                                     {u"color"_s, start < c.colors.size() ? QColor(c.colors[start]) : QColor(Qt::black)}});
        }
        return zones;
    }
    case ModesRole: {
        QVariantList modes;
        for (int i = 0; i < c.modes.size(); ++i) modes.append(modeToVariant(c.modes[i], i));
        return modes;
    }
    case ActiveModeRole: return c.activeMode;
    case ActiveModeNameRole: return mode ? mode->name : QString();
    case ColorsRole: {
        // Down-sample long strips/keyboards to <= 48 swatches for previews.
        QVariantList out;
        const QList<QRgb>& src = (mode && !mode->isPerLed() && !mode->colors.isEmpty()) ? mode->colors : c.colors;
        const int n = int(src.size());
        const int step = qMax(1, n / 48);
        for (int i = 0; i < n; i += step) out.append(QColor(src[i]));
        return out;
    }
    case PrimaryColorRole: {
        if (mode && !mode->isPerLed() && !mode->colors.isEmpty()) return QColor(mode->colors.first());
        // Brightest LED is a better "what colour is it" summary than LED 0.
        QRgb best = qRgb(0, 0, 0);
        int bestV = -1;
        for (QRgb rgb : c.colors) {
            const int v = qRed(rgb) + qGreen(rgb) + qBlue(rgb);
            if (v > bestV) bestV = v, best = rgb;
        }
        return QColor(best);
    }
    case PerLedRole: return mode && mode->isPerLed();
    case SupportsBrightnessRole: return (mode && mode->has(orgb::HasBrightness)) || (mode && mode->isPerLed());
    case BrightnessRole:
        if (mode && mode->has(orgb::HasBrightness)) return toPct(mode->brightness, mode->brightnessMin, mode->brightnessMax);
        return m_softBrightness.value(deviceKey(c), 100);
    case KrakenOwnedRole: return isKraken(c);
    case KeyRole: return deviceKey(c);
    }
    return {};
}

QHash<int, QByteArray> RgbDeviceModel::roleNames() const {
    return {
        {NameRole, "name"},
        {VendorRole, "vendor"},
        {TypeNameRole, "typeName"},
        {TypeIconRole, "typeIcon"},
        {DescriptionRole, "description"},
        {LocationRole, "location"},
        {SerialRole, "serial"},
        {LedCountRole, "ledCount"},
        {ZonesRole, "zones"},
        {ModesRole, "modes"},
        {ActiveModeRole, "activeMode"},
        {ActiveModeNameRole, "activeModeName"},
        {ColorsRole, "colors"},
        {PrimaryColorRole, "primaryColor"},
        {PerLedRole, "perLed"},
        {SupportsBrightnessRole, "supportsBrightness"},
        {BrightnessRole, "brightness"},
        {KrakenOwnedRole, "krakenOwned"},
        {KeyRole, "deviceKey"},
    };
}

void RgbDeviceModel::reset() {
    beginResetModel();
    endResetModel();
    Q_EMIT countChanged();
}

void RgbDeviceModel::refreshRow(int row) {
    if (row < 0 || row >= rowCount()) return;
    const QModelIndex i = index(row);
    Q_EMIT dataChanged(i, i);
}

void RgbDeviceModel::setSoftBrightness(int row, int pct) {
    const auto& list = (*m_list);
    if (row < 0 || row >= list.size()) return;
    m_softBrightness.insert(deviceKey(list[row]), qBound(0, pct, 100));
    refreshRow(row);
}

int RgbDeviceModel::softBrightness(int row) const {
    const auto& list = (*m_list);
    if (row < 0 || row >= list.size()) return 100;
    return m_softBrightness.value(deviceKey(list[row]), 100);
}
