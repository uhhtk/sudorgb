#pragma once
// XDG-compliant locations and crash-safe JSON persistence.
//
// Writes go through QSaveFile (temp file in the same directory, fsync, atomic
// rename), so a crash or power loss leaves either the old or the new file,
// never a truncated one. Unreadable files are moved aside (*.corrupt-<ts>)
// rather than silently overwritten, so user data is never lost.

#include <QJsonObject>
#include <QString>

namespace JsonStore {

QString configDir();   // $XDG_CONFIG_HOME/orkc
QString stateDir();    // $XDG_STATE_HOME/orkc
QString cacheDir();    // $XDG_CACHE_HOME/orkc
QString profilesDir(); // $XDG_CONFIG_HOME/orkc/profiles

// Returns false and sets *error on failure; never leaves a partial file.
bool writeAtomic(const QString& path, const QJsonObject& obj, QString* error = nullptr);
bool writeAtomicBytes(const QString& path, const QByteArray& data, QString* error = nullptr);

// Missing file -> empty object, ok=true. Corrupt file -> moved aside, empty object, ok=false.
QJsonObject read(const QString& path, bool* ok = nullptr, QString* error = nullptr);

}  // namespace JsonStore
