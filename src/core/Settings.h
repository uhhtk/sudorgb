#pragma once
// Application settings, persisted to $XDG_CONFIG_HOME/orkc/settings.json.
// Saves are debounced (500 ms) and atomic.

#include <QColor>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

#define ORKC_SETTING(Type, name, Name, def)                                       \
    Q_PROPERTY(Type name READ name WRITE set##Name NOTIFY changed)                 \
public:                                                                            \
    Type name() const { return m_##name; }                                         \
    void set##Name(const Type& v) {                                                \
        if (m_##name == v) return;                                                 \
        m_##name = v;                                                              \
        Q_EMIT changed();                                                          \
        scheduleSave();                                                            \
    }                                                                              \
                                                                                   \
private:                                                                           \
    Type m_##name = def;

class Settings : public QObject {
    Q_OBJECT
    ORKC_SETTING(QColor, accentColor, AccentColor, QColor(0xff, 0x7a, 0x29))
    ORKC_SETTING(QString, openRgbHost, OpenRgbHost, QStringLiteral("127.0.0.1"))
    ORKC_SETTING(int, openRgbPort, OpenRgbPort, 6742)
    ORKC_SETTING(bool, autoStartOpenRgb, AutoStartOpenRgb, true)
    ORKC_SETTING(bool, autoRestoreProfile, AutoRestoreProfile, true)
    ORKC_SETTING(QString, activeProfileId, ActiveProfileId, QString())  // empty: never auto-apply anything the user did not choose
    ORKC_SETTING(QString, pythonExecutable, PythonExecutable, QStringLiteral("/usr/bin/python3"))
    ORKC_SETTING(bool, krakenEnabled, KrakenEnabled, true)
    ORKC_SETTING(double, telemetryInterval, TelemetryInterval, 1.5)
    ORKC_SETTING(bool, fahrenheit, Fahrenheit, false)
    ORKC_SETTING(bool, reduceMotion, ReduceMotion, false)
    Q_PROPERTY(QString configPath READ configPath CONSTANT)
    Q_PROPERTY(QString loadError READ loadError CONSTANT)

public:
    explicit Settings(QObject* parent = nullptr);
    ~Settings() override;

    void load();
    Q_INVOKABLE void saveNow();
    QString configPath() const;
    QString loadError() const { return m_loadError; }

Q_SIGNALS:
    void changed();

private:
    void scheduleSave();
    QJsonObject toJson() const;

    QTimer m_saveTimer;
    QString m_loadError;
    bool m_loading = false;
};

#undef ORKC_SETTING
