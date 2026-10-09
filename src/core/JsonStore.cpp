#include "core/JsonStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace JsonStore {
namespace {

QString xdg(const char* var, const QString& fallbackRelHome) {
    const QString v = qEnvironmentVariable(var);
    // XDG spec: relative paths are invalid and must be ignored.
    const QString base = (!v.isEmpty() && QDir::isAbsolutePath(v)) ? v : QDir::homePath() + u'/' + fallbackRelHome;
    return base + QStringLiteral("/orkc");
}

QString ensure(const QString& dir) {
    QDir().mkpath(dir);
    return dir;
}

}  // namespace

QString configDir() { return ensure(xdg("XDG_CONFIG_HOME", QStringLiteral(".config"))); }
QString stateDir() { return ensure(xdg("XDG_STATE_HOME", QStringLiteral(".local/state"))); }
QString cacheDir() { return ensure(xdg("XDG_CACHE_HOME", QStringLiteral(".cache"))); }
QString profilesDir() { return ensure(configDir() + QStringLiteral("/profiles")); }

bool writeAtomicBytes(const QString& path, const QByteArray& data, QString* error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    if (f.write(data) != data.size()) {
        if (error) *error = f.errorString();
        f.cancelWriting();
        return false;
    }
    if (!f.commit()) {  // flush + fsync + rename
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

bool writeAtomic(const QString& path, const QJsonObject& obj, QString* error) {
    return writeAtomicBytes(path, QJsonDocument(obj).toJson(QJsonDocument::Indented), error);
}

QJsonObject read(const QString& path, bool* ok, QString* error) {
    if (ok) *ok = true;
    QFile f(path);
    if (!f.exists()) return {};
    if (!f.open(QIODevice::ReadOnly)) {
        if (ok) *ok = false;
        if (error) *error = f.errorString();
        return {};
    }
    QJsonParseError pe{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &pe);
    f.close();
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        const QString aside = path + QStringLiteral(".corrupt-") +
                              QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        QFile::rename(path, aside);
        if (ok) *ok = false;
        if (error) *error = QStringLiteral("%1 was unreadable (%2); moved to %3").arg(path, pe.errorString(), aside);
        return {};
    }
    return doc.object();
}

}  // namespace JsonStore
