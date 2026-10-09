#pragma once
// Supervisor for service/cooler_service.py: every non-Kraken AIO / fan hub that
// liquidctl drives, plus host-streamed LCD panels (Corsair ELITE/NAUTILUS/LINK).
// The service owns the hardware and persists desired state itself; this class
// only relays the device list and commands, and restarts the process on crash.

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariant>

class CoolerHub : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QString message READ message NOTIFY devicesChanged)

public:
    CoolerHub(QString python, QString script, QObject* parent = nullptr);
    ~CoolerHub() override;

    void start();
    void stop();
    QVariantList devices() const { return m_devices; }
    QString message() const { return m_message; }

    // cfg: {mode: "fixed", duty} | {mode: "curve", points: [[t, d], ...]}
    Q_INVOKABLE int setSpeed(const QString& device, const QString& channel, const QVariantMap& cfg);
    // cfg: {mode: "off"} | {mode: "media", path, fit, rotation, brightness}
    Q_INVOKABLE int setLcd(const QString& device, const QVariantMap& cfg);

Q_SIGNALS:
    void devicesChanged();
    void commandFinished(int id, bool ok, const QString& message);

private:
    int send(const QString& cmd, const QVariantMap& args);
    void onStdout();
    void onFinished();

    QString m_python;
    QString m_script;
    QProcess* m_proc = nullptr;
    QByteArray m_buf;
    QTimer m_restart;
    QList<qint64> m_crashes;
    int m_nextId = 1;
    bool m_stopping = false;
    QVariantList m_devices;
    QString m_message;
};
