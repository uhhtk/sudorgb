#pragma once
// QML facade over the OpenRGB client: device-capability-aware colour/mode
// operations, profile capture/apply, server lifecycle and OpenRGB config fixes.

#include "devices/NativeDevices.h"
#include "openrgb/OpenRgbClient.h"
#include "openrgb/RgbDeviceModel.h"

#include <QColor>
#include <QElapsedTimer>
#include <QObject>
#include <QVariantMap>

class Settings;

class RgbService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(RgbDeviceModel* devices READ devices CONSTANT)
    Q_PROPERTY(int controllableCount READ controllableCount NOTIFY devicesChanged)
    Q_PROPERTY(bool krakenInOpenRgb READ krakenInOpenRgb NOTIFY devicesChanged)
    Q_PROPERTY(bool openRgbRunning READ openRgbRunning NOTIFY stateChanged)
    Q_PROPERTY(int protocolVersion READ protocolVersion NOTIFY stateChanged)
    Q_PROPERTY(QString serverAddress READ serverAddress NOTIFY stateChanged)

public:
    explicit RgbService(Settings* settings, QObject* parent = nullptr);

    void start();
    QString state() const;
    QString statusText() const { return m_statusText; }
    bool connected() const { return m_client.state() == OpenRgbClient::State::Connected; }
    RgbDeviceModel* devices() { return &m_model; }
    int controllableCount() const;
    bool krakenInOpenRgb() const;
    bool openRgbRunning() const { return m_openRgbRunning; }
    int protocolVersion() const { return int(m_client.protocol()); }
    QString serverAddress() const;

    Q_INVOKABLE bool setDeviceColor(int dev, const QColor& color);
    Q_INVOKABLE bool setZoneColor(int dev, int zone, const QColor& color);
    Q_INVOKABLE int setAllColor(const QColor& color);
    Q_INVOKABLE bool setDeviceMode(int dev, int modeIndex, const QVariantMap& params = {});
    Q_INVOKABLE bool setDeviceBrightness(int dev, int pct);
    Q_INVOKABLE int applyEffectAll(const QString& effect, const QVariantList& colors, int speedPct = 50, int brightnessPct = 100);
    Q_INVOKABLE bool applyEffect(int dev, const QString& effect, const QVariantList& colors, int speedPct = 50, int brightnessPct = 100);
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void reconnect();  // after host/port change
    Q_INVOKABLE QString startServer();

    // OpenRGB detector management (OpenRGB must be closed: it rewrites its config).
    Q_INVOKABLE QVariantList krakenDetectorCandidates(const QString& krakenDescription) const;
    Q_INVOKABLE QVariantMap disableOpenRgbDetectors(const QStringList& names);
    Q_INVOKABLE bool openRgbConfigReadable() const;
    Q_INVOKABLE QString openRgbConfigFile() const;

    // Profiles
    Q_INVOKABLE QVariantMap captureState() const;
    Q_INVOKABLE QStringList applyState(const QVariantMap& rgb);  // returns warnings

Q_SIGNALS:
    void stateChanged();
    void devicesChanged();
    void notify(const QString& message, bool error);

private:
    bool writable(int dev) const;
    int findPerLedMode(const orgb::Controller& c) const;
    bool ensurePerLed(int dev);
    bool writeLeds(int dev, const QList<QRgb>& base);
    int findDevice(const QVariantMap& entry) const;
    void updateStatus();
    void refreshProcessState();
    static QList<QRgb> rgbList(const QVariantList& colors);
    // Host-animated effects for per-LED devices that lack a hardware effect
    // (e.g. Corsair Commander Core, SteelSeries Apex Pro expose only "Direct").
    struct SoftFx { QString effect; QList<QRgb> colors; int speedPct; int brightnessPct; };
    void startSoftFx(int dev, const QString& effect, const QList<QRgb>& colors, int speedPct, int brightnessPct);
    void stopSoftFx(int dev);
    void tickSoftFx();
    QHash<QString, SoftFx> m_softFx;  // device key -> running effect
    QTimer m_fxTimer;
    QElapsedTimer m_fxClock;
    // Combined device list: OpenRGB controllers first, then native devices.
    void rebuild();
    int orgbCount() const { return int(m_client.controllers().size()); }
    bool doUpdateMode(int dev, int modeIndex, const orgb::Mode& mode);
    bool doUpdateLeds(int dev, const QList<QRgb>& colors);
    bool doUpdateZoneLeds(int dev, int zone, const QList<QRgb>& colors);

    Settings* m_settings;
    OpenRgbClient m_client;
    NativeDevices m_native;
    QList<orgb::Controller> m_all;
    RgbDeviceModel m_model;
    QHash<QString, QList<QRgb>> m_baseColors;  // unscaled colours for software brightness
    QString m_statusText;
    bool m_openRgbRunning = false;
    bool m_autoStartTried = false;
    QTimer m_procTimer;
};
