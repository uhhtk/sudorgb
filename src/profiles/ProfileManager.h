#pragma once
// Native profile manager: built-in presets + user profiles stored as one JSON
// file each in $XDG_CONFIG_HOME/orkc/profiles (atomic writes).
//
// Restoring: the Kraken service persists and re-applies its own desired state
// on every (re)connect. For OpenRGB devices, the last live RGB state is saved
// to $XDG_STATE_HOME/orkc/rgb-session.json on exit and re-applied whenever the
// OpenRGB server (re)connects, so a restarted server or a reboot gets the user's
// lighting back. Without a session file, the active profile is used.

#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <QVariant>

class Settings;
class RgbService;
class KrakenService;

class ProfileManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString activeId READ activeId NOTIFY activeChanged)
    Q_PROPERTY(QString activeName READ activeName NOTIFY activeChanged)
    Q_PROPERTY(QString lastWarnings READ lastWarnings NOTIFY applied)

public:
    ProfileManager(Settings* settings, RgbService* rgb, KrakenService* kraken, QObject* parent = nullptr);

    void load();
    void saveSession();

    QVariantList profiles() const;
    QString activeId() const;
    QString activeName() const;
    QString lastWarnings() const { return m_lastWarnings; }

    Q_INVOKABLE QVariantMap get(const QString& id) const;
    Q_INVOKABLE bool apply(const QString& id);
    Q_INVOKABLE QString saveCurrent(const QString& name, const QString& description, bool includeRgb = true, bool includeKraken = true);
    Q_INVOKABLE bool overwriteWithCurrent(const QString& id);
    Q_INVOKABLE QString duplicate(const QString& id);
    Q_INVOKABLE bool rename(const QString& id, const QString& name, const QString& description);
    Q_INVOKABLE bool setAccent(const QString& id, const QString& accent);
    Q_INVOKABLE bool remove(const QString& id);
    Q_INVOKABLE QString importFile(const QUrl& file);  // returns new id, or "" (see notify)
    Q_INVOKABLE bool exportFile(const QString& id, const QUrl& file);

Q_SIGNALS:
    void profilesChanged();
    void activeChanged();
    void applied(const QString& id, const QStringList& warnings);
    void notify(const QString& message, bool error);

private:
    QJsonObject capture(bool rgb, bool kraken) const;
    bool store(QJsonObject profile);
    QString pathFor(const QString& id) const;
    int indexOf(const QString& id) const;
    void onRgbConnected();

    Settings* m_settings;
    RgbService* m_rgb;
    KrakenService* m_kraken;
    QList<QJsonObject> m_profiles;  // builtins first
    QString m_lastWarnings;
};
