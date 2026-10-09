#pragma once
// Built-in presets and the profile schema validator.
//
// Profile (schema 1):
// {
//   "schema": 1, "id": "...", "name": "...", "description": "...", "accent": "#rrggbb",
//   "rgb": {
//     "all":     {"effect": "static|off|rainbow|breathing", "colors": ["#..."], "brightness": 0-100, "speed": 0-100},
//     "devices": [{"name", "serial", "location", "mode", "colors": [...], "brightness"?, "speed"?, "direction"?}]
//   },
//   "kraken": {
//     "lighting": {"ring"|"fans": {"mode": "off|fixed|gradient|breathing|cycle|spectrum", "colors", "brightness", "speed": "slow|normal|fast"}},
//     "cooling":  {"pump"|"fan": {"mode": "fixed", "duty"} | {"mode": "curve", "points": [[°C, %], ...]}},
//     "lcd":      {"mode": "liquid|sensors|image|gif|off", "path", "fit", "brightness", "orientation", "sensor_style", "ring_color"}
//   }
// }
// Every section is optional; omitted sections leave hardware untouched.

#include <QJsonObject>
#include <QList>

namespace Presets {

QList<QJsonObject> builtins();

// Returns a sanitized copy (unknown keys dropped, values clamped), or an empty
// object with *error set when the input is unusable. Never trusts its input:
// imported files come from anywhere.
QJsonObject sanitize(const QJsonObject& in, QString* error);

}  // namespace Presets
