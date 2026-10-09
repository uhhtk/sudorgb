#pragma once
// Host sensors from sysfs/procfs (no subprocesses, no polling when nobody looks).
// Any value that can't be read is exposed as an invalid QVariant (null in QML),
// so the UI can say "unavailable" instead of showing a made-up number.

#include <QObject>
#include <QTimer>
#include <QVariant>

class SystemMonitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariant cpuTemp READ cpuTemp NOTIFY updated)
    Q_PROPERTY(QVariant cpuLoad READ cpuLoad NOTIFY updated)
    Q_PROPERTY(QVariant gpuTemp READ gpuTemp NOTIFY updated)
    Q_PROPERTY(QString cpuName READ cpuName CONSTANT)
    Q_PROPERTY(QString cpuSensor READ cpuSensor CONSTANT)
    Q_PROPERTY(QString gpuSensor READ gpuSensor CONSTANT)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY updated)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    explicit SystemMonitor(QObject* parent = nullptr);

    QVariant cpuTemp() const { return m_cpuTemp; }
    QVariant cpuLoad() const { return m_cpuLoad; }
    QVariant gpuTemp() const { return m_gpuTemp; }
    QString cpuName() const { return m_cpuName; }
    QString cpuSensor() const { return m_cpuSensorLabel; }
    QString gpuSensor() const { return m_gpuSensorLabel; }
    QVariantList cpuHistory() const { return m_history; }
    bool active() const { return m_timer.isActive(); }
    void setActive(bool on);

Q_SIGNALS:
    void updated();
    void activeChanged();

private:
    void sample();
    static QString findTemp(const QStringList& chips, const QStringList& labels, QString* label);

    QTimer m_timer;
    QString m_cpuPath, m_gpuPath, m_cpuName, m_cpuSensorLabel, m_gpuSensorLabel;
    QVariant m_cpuTemp, m_cpuLoad, m_gpuTemp;
    quint64 m_prevIdle = 0, m_prevTotal = 0;
    QVariantList m_history;
};
