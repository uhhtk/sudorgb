#pragma once
// Supervisor for the isolated Python Kraken service (service/kraken_service.py).
//
// * Launches the *system* interpreter in isolated mode (-I) with a scrubbed
//   environment, so user/venv/mise Pythons can never inject other packages.
// * Newline-delimited JSON IPC with per-request ids and timeouts; a heartbeat
//   detects a hung service and restarts it.
// * Restarts on crash with exponential backoff; gives up after a crash loop
//   (5 crashes / 2 min) until the user retries.
// * Keeps the desired Kraken state (lighting, cooling, LCD) and persists it to
//   $XDG_STATE_HOME/orkc/kraken.json; the service is told to re-apply it on
//   every start, which also restores the saved LCD configuration after reboots.

#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariant>
#include <functional>

class Settings;

class KrakenService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateMessage READ stateMessage NOTIFY stateChanged)
    Q_PROPERTY(QVariantList conflictHolders READ conflictHolders NOTIFY stateChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(bool simulated READ simulated NOTIFY stateChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY stateChanged)
    Q_PROPERTY(QString firmware READ firmware NOTIFY stateChanged)
    Q_PROPERTY(QString backendLabel READ backendLabel NOTIFY stateChanged)
    Q_PROPERTY(QString pythonInfo READ pythonInfo NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap capabilities READ capabilities NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap deviceInfo READ deviceInfo NOTIFY stateChanged)
    Q_PROPERTY(int restartCount READ restartCount NOTIFY stateChanged)

    Q_PROPERTY(QVariant liquidTemp READ liquidTemp NOTIFY telemetryChanged)
    Q_PROPERTY(QVariant pumpRpm READ pumpRpm NOTIFY telemetryChanged)
    Q_PROPERTY(QVariant pumpDuty READ pumpDuty NOTIFY telemetryChanged)
    Q_PROPERTY(QVariant fanRpm READ fanRpm NOTIFY telemetryChanged)
    Q_PROPERTY(QVariant fanDuty READ fanDuty NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantList liquidHistory READ liquidHistory NOTIFY telemetryChanged)

    Q_PROPERTY(QString lcdMode READ lcdMode NOTIFY lcdChanged)
    Q_PROPERTY(QString lcdPreview READ lcdPreview NOTIFY lcdChanged)
    Q_PROPERTY(QString lcdMessage READ lcdMessage NOTIFY lcdChanged)
    Q_PROPERTY(bool lcdBusy READ lcdBusy NOTIFY lcdChanged)

    Q_PROPERTY(QVariantMap desired READ desired NOTIFY desiredChanged)
    Q_PROPERTY(QString logTail READ logTail NOTIFY logChanged)

public:
    explicit KrakenService(Settings* settings, QObject* parent = nullptr);
    ~KrakenService() override;

    void start();
    void stop();

    QString state() const { return m_state; }
    QString stateMessage() const { return m_stateMessage; }
    QVariantList conflictHolders() const { return m_holders; }
    bool ready() const { return m_state == QLatin1String("ready"); }
    bool simulated() const { return m_simulated; }
    QString deviceName() const { return m_device.value(QStringLiteral("description")).toString(); }
    QString firmware() const { return m_device.value(QStringLiteral("firmware")).toString(); }
    QString backendLabel() const { return m_backendLabel; }
    QString pythonInfo() const { return m_pythonInfo; }
    QVariantMap capabilities() const { return m_caps; }
    QVariantMap deviceInfo() const { return m_device; }
    int restartCount() const { return m_restarts; }

    QVariant liquidTemp() const { return m_status.value(QStringLiteral("liquid_temp")); }
    QVariant pumpRpm() const { return m_status.value(QStringLiteral("pump_rpm")); }
    QVariant pumpDuty() const { return m_status.value(QStringLiteral("pump_duty")); }
    QVariant fanRpm() const { return m_status.value(QStringLiteral("fan_rpm")); }
    QVariant fanDuty() const { return m_status.value(QStringLiteral("fan_duty")); }
    QVariantList liquidHistory() const { return m_history; }

    QString lcdMode() const { return m_lcdMode; }
    QString lcdPreview() const { return m_lcdPreview; }
    QString lcdMessage() const { return m_lcdMessage; }
    bool lcdBusy() const { return m_lcdBusy; }

    QVariantMap desired() const { return m_desired.toVariantMap(); }
    QString logTail() const { return m_log.join(QLatin1Char('\n')); }

    // All return a request id; completion arrives via commandFinished().
    Q_INVOKABLE int setLighting(const QString& channel, const QVariantMap& cfg);
    Q_INVOKABLE int setCooling(const QString& channel, const QVariantMap& cfg);
    Q_INVOKABLE int setLcd(const QVariantMap& cfg);
    Q_INVOKABLE int setLcdBrightness(int value);
    Q_INVOKABLE int clearLcdMedia();
    Q_INVOKABLE int applyState(const QVariantMap& kraken);  // profile section
    Q_INVOKABLE void retry();
    Q_INVOKABLE void setPollInterval(double seconds);

Q_SIGNALS:
    void stateChanged();
    void telemetryChanged();
    void lcdChanged();
    void desiredChanged();
    void logChanged();
    void commandFinished(int id, bool ok, const QString& message, const QVariantMap& result);
    void becameReady();

private:
    using Callback = std::function<void(bool ok, const QJsonObject& resultOrError)>;
    struct Request {
        QString cmd;
        qint64 deadline;
        Callback cb;
    };

    int send(const QString& cmd, const QJsonObject& args, int timeoutMs, Callback cb = {});
    void launch();
    void onStdout();
    void onStderr();
    void onFinished(int code, QProcess::ExitStatus status);
    void handleMessage(const QJsonObject& msg);
    void handleEvent(const QString& name, const QJsonObject& data);
    void setState(const QString& state, const QString& message = {});
    void checkTimeouts();
    void persistDesired();
    void sendLighting(const QString& channel, const QJsonObject& args);
    void loadDesired();
    void mergeDesired(const QString& section, const QString& key, const QJsonObject& value);
    void appendLog(const QString& line);
    QString servicePath() const;

    Settings* m_settings;
    QProcess* m_proc = nullptr;
    QByteArray m_outBuf;
    QByteArray m_errBuf;
    int m_nextId = 1;
    QHash<int, Request> m_pending;
    QTimer m_timeoutTimer;
    QTimer m_heartbeat;
    QTimer m_restartTimer;
    QTimer m_persistTimer;                    // debounced state save (no fsync per colour change)
    QSet<QString> m_lightInFlight;            // channels with a set_lighting awaiting reply
    QHash<QString, QJsonObject> m_lightPending; // newest args per channel while one is in flight
    QList<qint64> m_crashTimes;
    QElapsedTimer m_clock;
    int m_restarts = 0;
    int m_missedPings = 0;
    bool m_stopping = false;

    QString m_state = QStringLiteral("stopped");
    QString m_stateMessage;
    QVariantList m_holders;
    bool m_simulated = false;
    QString m_backendLabel;
    QString m_pythonInfo;
    QVariantMap m_caps;
    QVariantMap m_device;
    QVariantMap m_status;
    QVariantList m_history;

    QString m_lcdMode = QStringLiteral("unknown");
    QString m_lcdPreview;
    QString m_lcdMessage;
    bool m_lcdBusy = false;

    QJsonObject m_desired;  // {"lighting": {...}, "cooling": {...}, "lcd": {...}}
    QStringList m_log;
};
