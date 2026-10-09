#pragma once

#include "openrgb/OrgbProtocol.h"

#include <QAbstractListModel>
#include <QColor>


// List model over the OpenRGB controllers, for QML. Data comes straight from
// what the server reports (modes, zones, ranges); nothing is hard-coded.
class RgbDeviceModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        VendorRole,
        TypeNameRole,
        TypeIconRole,
        DescriptionRole,
        LocationRole,
        SerialRole,
        LedCountRole,
        ZonesRole,
        ModesRole,
        ActiveModeRole,
        ActiveModeNameRole,
        ColorsRole,
        PrimaryColorRole,
        PerLedRole,
        SupportsBrightnessRole,
        BrightnessRole,
        KrakenOwnedRole,
        KeyRole,
    };

    // `list` is RgbService's combined device list (OpenRGB + native devices).
    explicit RgbDeviceModel(const QList<orgb::Controller>* list, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reset();
    void refreshRow(int row);
    void setSoftBrightness(int row, int pct);
    int softBrightness(int row) const;

    static bool isKraken(const orgb::Controller& c);
    static QString deviceKey(const orgb::Controller& c);
    static QVariantMap modeToVariant(const orgb::Mode& m, int index);

Q_SIGNALS:
    void countChanged();

private:
    const QList<orgb::Controller>* m_list;
    QHash<QString, int> m_softBrightness;  // device key -> 0..100 (per-LED modes)
};
