#include "core/Settings.h"

#include "core/JsonStore.h"

#include <QLoggingCategory>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcSettings, "orkc.settings")

Settings::Settings(QObject* parent) : QObject(parent) {
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &Settings::saveNow);
}

Settings::~Settings() {
    if (m_saveTimer.isActive()) saveNow();
}

QString Settings::configPath() const { return JsonStore::configDir() + QStringLiteral("/settings.json"); }

void Settings::load() {
    bool ok = true;
    const QJsonObject o = JsonStore::read(configPath(), &ok, &m_loadError);
    if (!ok) qCWarning(lcSettings) << m_loadError;
    m_loading = true;
    const QColor accent(o.value(u"accentColor").toString());
    if (accent.isValid()) setAccentColor(accent);
    if (o.contains(u"openRgbHost")) setOpenRgbHost(o.value(u"openRgbHost").toString(m_openRgbHost));
    const int port = o.value(u"openRgbPort").toInt(m_openRgbPort);
    if (port > 0 && port < 65536) setOpenRgbPort(port);
    setAutoStartOpenRgb(o.value(u"autoStartOpenRgb").toBool(m_autoStartOpenRgb));
    setAutoRestoreProfile(o.value(u"autoRestoreProfile").toBool(m_autoRestoreProfile));
    setActiveProfileId(o.value(u"activeProfileId").toString(m_activeProfileId));
    setPythonExecutable(o.value(u"pythonExecutable").toString(m_pythonExecutable));
    setKrakenEnabled(o.value(u"krakenEnabled").toBool(m_krakenEnabled));
    setTelemetryInterval(qBound(0.5, o.value(u"telemetryInterval").toDouble(m_telemetryInterval), 10.0));
    setFahrenheit(o.value(u"fahrenheit").toBool(m_fahrenheit));
    setReduceMotion(o.value(u"reduceMotion").toBool(m_reduceMotion));
    m_loading = false;
    m_saveTimer.stop();
}

QJsonObject Settings::toJson() const {
    return {
        {u"schema"_s, 1},
        {u"accentColor"_s, m_accentColor.name()},
        {u"openRgbHost"_s, m_openRgbHost},
        {u"openRgbPort"_s, m_openRgbPort},
        {u"autoStartOpenRgb"_s, m_autoStartOpenRgb},
        {u"autoRestoreProfile"_s, m_autoRestoreProfile},
        {u"activeProfileId"_s, m_activeProfileId},
        {u"pythonExecutable"_s, m_pythonExecutable},
        {u"krakenEnabled"_s, m_krakenEnabled},
        {u"telemetryInterval"_s, m_telemetryInterval},
        {u"fahrenheit"_s, m_fahrenheit},
        {u"reduceMotion"_s, m_reduceMotion},
    };
}

void Settings::scheduleSave() {
    if (!m_loading) m_saveTimer.start();
}

void Settings::saveNow() {
    m_saveTimer.stop();
    QString err;
    if (!JsonStore::writeAtomic(configPath(), toJson(), &err)) qCWarning(lcSettings) << "save failed:" << err;
}
