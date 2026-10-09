#include "profiles/ProfileManager.h"

#include "core/JsonStore.h"
#include "core/Settings.h"
#include "kraken/KrakenService.h"
#include "openrgb/RgbService.h"
#include "profiles/Presets.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QUuid>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcProfiles, "orkc.profiles")

ProfileManager::ProfileManager(Settings* settings, RgbService* rgb, KrakenService* kraken, QObject* parent)
    : QObject(parent), m_settings(settings), m_rgb(rgb), m_kraken(kraken) {
    connect(m_rgb, &RgbService::devicesChanged, this, [this] {
        if (m_rgb->connected()) onRgbConnected();
    });
}

void ProfileManager::load() {
    m_profiles = Presets::builtins();
    const QDir dir(JsonStore::profilesDir());
    for (const QString& f : dir.entryList({u"*.json"_s}, QDir::Files, QDir::Name)) {
        bool ok = true;
        QString err;
        const QJsonObject raw = JsonStore::read(dir.absoluteFilePath(f), &ok, &err);
        QJsonObject p = Presets::sanitize(raw, &err);
        if (p.isEmpty()) {
            qCWarning(lcProfiles) << "skipping profile" << f << err;
            continue;
        }
        p.insert(u"id"_s, QFileInfo(f).completeBaseName());  // file name is the id of record
        p.insert(u"builtin"_s, false);
        m_profiles.append(p);
    }
    Q_EMIT profilesChanged();
    Q_EMIT activeChanged();
}

QString ProfileManager::pathFor(const QString& id) const { return JsonStore::profilesDir() + u'/' + id + u".json"_s; }

int ProfileManager::indexOf(const QString& id) const {
    for (int i = 0; i < m_profiles.size(); ++i)
        if (m_profiles[i].value(u"id"_s).toString() == id) return i;
    return -1;
}

QVariantList ProfileManager::profiles() const {
    QVariantList out;
    for (const QJsonObject& p : m_profiles) {
        // Summary colours for the profile card swatches.
        QVariantList swatches;
        const QJsonObject all = p.value(u"rgb"_s).toObject().value(u"all"_s).toObject();
        const QString effect = all.value(u"effect"_s).toString();
        if (effect == u"rainbow"_s) swatches = {u"#ff3b30"_s, u"#ffcc00"_s, u"#34c759"_s, u"#0a84ff"_s, u"#bf5af2"_s};
        else if (effect == u"off"_s) swatches = {u"#1f2128"_s};
        for (const QJsonValue& c : all.value(u"colors"_s).toArray()) swatches.append(c.toString());
        const QJsonObject lighting = p.value(u"kraken"_s).toObject().value(u"lighting"_s).toObject();
        for (const QString& ch : {u"ring"_s, u"fans"_s})
            for (const QJsonValue& c : lighting.value(ch).toObject().value(u"colors"_s).toArray())
                if (!swatches.contains(c.toString())) swatches.append(c.toString());
        const QJsonArray devs = p.value(u"rgb"_s).toObject().value(u"devices"_s).toArray();
        for (int i = 0; i < devs.size() && swatches.size() < 6; ++i) {
            const QJsonArray cs = devs[i].toObject().value(u"colors"_s).toArray();
            if (!cs.isEmpty() && !swatches.contains(cs.first().toString())) swatches.append(cs.first().toString());
        }
        QVariantMap m = p.toVariantMap();
        m.insert(u"swatches"_s, swatches.mid(0, 6));
        m.insert(u"lcdMode"_s, p.value(u"kraken"_s).toObject().value(u"lcd"_s).toObject().value(u"mode"_s).toString());
        m.insert(u"deviceCount"_s, devs.size());
        out.append(m);
    }
    return out;
}

QString ProfileManager::activeId() const { return m_settings->activeProfileId(); }

QString ProfileManager::activeName() const {
    const int i = indexOf(activeId());
    return i < 0 ? QString() : m_profiles[i].value(u"name"_s).toString();
}

QVariantMap ProfileManager::get(const QString& id) const {
    const int i = indexOf(id);
    return i < 0 ? QVariantMap() : m_profiles[i].toVariantMap();
}

bool ProfileManager::apply(const QString& id) {
    const int i = indexOf(id);
    if (i < 0) return false;
    const QJsonObject p = m_profiles[i];
    QStringList warnings;

    if (p.contains(u"rgb"_s)) warnings += m_rgb->applyState(p.value(u"rgb"_s).toObject().toVariantMap());

    QJsonObject kraken = p.value(u"kraken"_s).toObject();
    if (!kraken.isEmpty()) {
        const QVariantMap caps = m_kraken->capabilities();
        const QString lcdMode = kraken.value(u"lcd"_s).toObject().value(u"mode"_s).toString();
        const QVariantList modes = caps.value(u"lcd_modes"_s).toList();
        if (!lcdMode.isEmpty() && !modes.isEmpty() && !modes.contains(lcdMode)) {
            warnings << u"This Kraken doesn’t support the “%1” LCD mode; LCD left unchanged."_s.arg(lcdMode);
            kraken.remove(u"lcd"_s);
        }
        const QString path = kraken.value(u"lcd"_s).toObject().value(u"path"_s).toString();
        if (!path.isEmpty() && !QFile::exists(QDir::cleanPath(path.startsWith(u'~') ? QDir::homePath() + path.mid(1) : path))) {
            warnings << u"LCD media %1 is missing; LCD left unchanged."_s.arg(path);
            kraken.remove(u"lcd"_s);
        }
        m_kraken->applyState(kraken.toVariantMap());
        if (!m_kraken->ready()) warnings << u"Kraken not available right now; its settings will apply when it connects."_s;
    }

    m_settings->setActiveProfileId(id);
    m_lastWarnings = warnings.join(u'\n');
    Q_EMIT activeChanged();
    Q_EMIT applied(id, warnings);
    saveSession();
    return true;
}

QJsonObject ProfileManager::capture(bool rgb, bool kraken) const {
    QJsonObject p;
    if (rgb && m_rgb->connected()) p.insert(u"rgb"_s, QJsonObject::fromVariantMap(m_rgb->captureState()));
    if (kraken) {
        const QJsonObject d = QJsonObject::fromVariantMap(m_kraken->desired());
        QJsonObject k;
        for (const QString& s : {u"lighting"_s, u"cooling"_s, u"lcd"_s})
            if (d.contains(s) && !d.value(s).toObject().isEmpty()) k.insert(s, d.value(s));
        if (!k.isEmpty()) p.insert(u"kraken"_s, k);
    }
    return p;
}

bool ProfileManager::store(QJsonObject profile) {
    QString err;
    const QString id = profile.value(u"id"_s).toString();
    QJsonObject clean = Presets::sanitize(profile, &err);
    if (clean.isEmpty()) {
        Q_EMIT notify(err, true);
        return false;
    }
    clean.insert(u"id"_s, id);
    if (!JsonStore::writeAtomic(pathFor(id), clean, &err)) {
        Q_EMIT notify(u"Could not save profile: "_s + err, true);
        return false;
    }
    clean.insert(u"builtin"_s, false);
    const int i = indexOf(id);
    if (i >= 0) m_profiles[i] = clean;
    else m_profiles.append(clean);
    Q_EMIT profilesChanged();
    if (id == activeId()) Q_EMIT activeChanged();
    return true;
}

QString ProfileManager::saveCurrent(const QString& name, const QString& description, bool includeRgb, bool includeKraken) {
    QJsonObject p = capture(includeRgb, includeKraken);
    if (!p.contains(u"rgb"_s) && !p.contains(u"kraken"_s)) {
        Q_EMIT notify(u"Nothing to save yet: no RGB devices connected and no Kraken settings made."_s, true);
        return {};
    }
    p.insert(u"id"_s, QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) + u'-' + QString::number(QDateTime::currentSecsSinceEpoch(), 36));
    p.insert(u"name"_s, name.trimmed().isEmpty() ? u"My profile"_s : name.trimmed());
    p.insert(u"description"_s, description);
    p.insert(u"accent"_s, m_settings->accentColor().name());
    if (!store(p)) return {};
    m_settings->setActiveProfileId(p.value(u"id"_s).toString());
    Q_EMIT activeChanged();
    Q_EMIT notify(u"Saved “%1”"_s.arg(p.value(u"name"_s).toString()), false);
    return p.value(u"id"_s).toString();
}

bool ProfileManager::overwriteWithCurrent(const QString& id) {
    const int i = indexOf(id);
    if (i < 0 || m_profiles[i].value(u"builtin"_s).toBool()) return false;
    QJsonObject p = m_profiles[i];
    const QJsonObject cur = capture(true, true);
    for (auto it = cur.begin(); it != cur.end(); ++it) p.insert(it.key(), it.value());
    return store(p);
}

QString ProfileManager::duplicate(const QString& id) {
    const int i = indexOf(id);
    if (i < 0) return {};
    QJsonObject p = m_profiles[i];
    const QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) + u'-' + QString::number(QDateTime::currentSecsSinceEpoch(), 36);
    p.insert(u"id"_s, newId);
    p.insert(u"name"_s, p.value(u"name"_s).toString() + u" copy"_s);
    p.remove(u"builtin"_s);
    return store(p) ? newId : QString();
}

bool ProfileManager::rename(const QString& id, const QString& name, const QString& description) {
    const int i = indexOf(id);
    if (i < 0 || m_profiles[i].value(u"builtin"_s).toBool() || name.trimmed().isEmpty()) return false;
    QJsonObject p = m_profiles[i];
    p.insert(u"name"_s, name.trimmed());
    p.insert(u"description"_s, description);
    return store(p);
}

bool ProfileManager::setAccent(const QString& id, const QString& accent) {
    const int i = indexOf(id);
    if (i < 0 || m_profiles[i].value(u"builtin"_s).toBool()) return false;
    QJsonObject p = m_profiles[i];
    p.insert(u"accent"_s, accent);
    return store(p);
}

bool ProfileManager::remove(const QString& id) {
    const int i = indexOf(id);
    if (i < 0 || m_profiles[i].value(u"builtin"_s).toBool()) return false;
    // Keep a recoverable copy instead of hard-deleting the user's file.
    const QString trash = JsonStore::stateDir() + u"/deleted-profiles"_s;
    QDir().mkpath(trash);
    QFile::remove(trash + u'/' + id + u".json"_s);
    if (!QFile::rename(pathFor(id), trash + u'/' + id + u".json"_s) && QFile::exists(pathFor(id))) {
        Q_EMIT notify(u"Could not remove profile file"_s, true);
        return false;
    }
    const QString name = m_profiles[i].value(u"name"_s).toString();
    m_profiles.removeAt(i);
    if (activeId() == id) m_settings->setActiveProfileId({});
    Q_EMIT profilesChanged();
    Q_EMIT activeChanged();
    Q_EMIT notify(u"Deleted “%1” (a copy is kept in %2)"_s.arg(name, trash), false);
    return true;
}

QString ProfileManager::importFile(const QUrl& file) {
    QFile f(file.toLocalFile());
    if (f.size() > 4 * 1024 * 1024 || !f.open(QIODevice::ReadOnly)) {
        Q_EMIT notify(u"Cannot read %1"_s.arg(file.toLocalFile()), true);
        return {};
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject()) {
        Q_EMIT notify(u"Not a profile file: "_s + pe.errorString(), true);
        return {};
    }
    QString err;
    QJsonObject p = Presets::sanitize(doc.object(), &err);
    if (p.isEmpty()) {
        Q_EMIT notify(u"Invalid profile: "_s + err, true);
        return {};
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) + u'-' + QString::number(QDateTime::currentSecsSinceEpoch(), 36);
    p.insert(u"id"_s, id);
    if (!store(p)) return {};
    Q_EMIT notify(u"Imported “%1”"_s.arg(p.value(u"name"_s).toString()), false);
    return id;
}

bool ProfileManager::exportFile(const QString& id, const QUrl& file) {
    const int i = indexOf(id);
    if (i < 0) return false;
    QJsonObject p = m_profiles[i];
    p.remove(u"builtin"_s);
    QString path = file.toLocalFile();
    if (!path.endsWith(u".json"_s)) path += u".json"_s;
    QString err;
    if (!JsonStore::writeAtomic(path, p, &err)) {
        Q_EMIT notify(u"Export failed: "_s + err, true);
        return false;
    }
    Q_EMIT notify(u"Exported to %1"_s.arg(path), false);
    return true;
}

void ProfileManager::saveSession() {
    if (!m_rgb->connected()) return;
    const QJsonObject rgb = QJsonObject::fromVariantMap(m_rgb->captureState());
    if (rgb.value(u"devices"_s).toArray().isEmpty()) return;
    JsonStore::writeAtomic(JsonStore::stateDir() + u"/rgb-session.json"_s, {{u"schema"_s, 1}, {u"rgb"_s, rgb}});
}

void ProfileManager::onRgbConnected() {
    if (!m_settings->autoRestoreProfile()) return;
    // Every (re)connect of the OpenRGB server: put the user's lighting back.
    const QJsonObject session = JsonStore::read(JsonStore::stateDir() + u"/rgb-session.json"_s);
    QStringList warnings;
    if (session.contains(u"rgb"_s)) {
        warnings = m_rgb->applyState(session.value(u"rgb"_s).toObject().toVariantMap());
        qCInfo(lcProfiles) << "restored last RGB session" << warnings;
    } else if (const int i = indexOf(activeId()); i >= 0 && m_profiles[i].contains(u"rgb"_s)) {
        warnings = m_rgb->applyState(m_profiles[i].value(u"rgb"_s).toObject().toVariantMap());
        qCInfo(lcProfiles) << "restored active profile RGB" << warnings;
    }
}
