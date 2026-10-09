#include "profiles/Presets.h"

#include <QColor>
#include <QJsonArray>
#include <QSet>

using namespace Qt::StringLiterals;

namespace Presets {
namespace {

QJsonArray curve(std::initializer_list<std::pair<int, int>> pts) {
    QJsonArray a;
    for (auto [t, d] : pts) a.append(QJsonArray{t, d});
    return a;
}

QJsonObject cooling(const QString& kind) {
    if (kind == u"quiet"_s)
        return {{u"pump"_s, QJsonObject{{u"mode"_s, u"curve"_s}, {u"points"_s, curve({{20, 40}, {35, 50}, {42, 70}, {48, 90}, {52, 100}})}}},
                {u"fan"_s, QJsonObject{{u"mode"_s, u"curve"_s}, {u"points"_s, curve({{20, 25}, {35, 30}, {42, 50}, {48, 75}, {52, 100}})}}}};
    if (kind == u"performance"_s)
        return {{u"pump"_s, QJsonObject{{u"mode"_s, u"curve"_s}, {u"points"_s, curve({{20, 70}, {30, 80}, {38, 90}, {44, 100}})}}},
                {u"fan"_s, QJsonObject{{u"mode"_s, u"curve"_s}, {u"points"_s, curve({{20, 45}, {30, 55}, {38, 75}, {44, 100}})}}}};
    return {{u"pump"_s, QJsonObject{{u"mode"_s, u"curve"_s}, {u"points"_s, curve({{20, 50}, {33, 60}, {40, 75}, {46, 90}, {50, 100}})}}},
            {u"fan"_s, QJsonObject{{u"mode"_s, u"curve"_s}, {u"points"_s, curve({{20, 30}, {33, 40}, {40, 60}, {45, 80}, {50, 100}})}}}};
}

QJsonObject light(const QString& mode, QStringList colors, int brightness, const QString& speed = u"normal"_s) {
    return {{u"mode"_s, mode}, {u"colors"_s, QJsonArray::fromStringList(colors)}, {u"brightness"_s, brightness}, {u"speed"_s, speed}};
}

QJsonObject preset(const QString& id, const QString& name, const QString& desc, const QString& accent, QJsonObject all,
                   QJsonObject ring, QJsonObject fans, QJsonObject lcd, const QString& coolingKind) {
    return {{u"schema"_s, 1},
            {u"id"_s, u"builtin:"_s + id},
            {u"name"_s, name},
            {u"description"_s, desc},
            {u"accent"_s, accent},
            {u"builtin"_s, true},
            {u"rgb"_s, QJsonObject{{u"all"_s, all}}},
            {u"kraken"_s, QJsonObject{{u"lighting"_s, QJsonObject{{u"ring"_s, ring}, {u"fans"_s, fans}}},
                                      {u"cooling"_s, cooling(coolingKind)},
                                      {u"lcd"_s, lcd}}}};
}

QJsonObject all(const QString& effect, QStringList colors, int brightness = 100, int speed = 50) {
    return {{u"effect"_s, effect}, {u"colors"_s, QJsonArray::fromStringList(colors)}, {u"brightness"_s, brightness}, {u"speed"_s, speed}};
}

QJsonObject lcd(const QString& mode, int brightness, const QString& style = u"liquid_ring"_s, const QString& ring = u"#ff7a29"_s) {
    return {{u"mode"_s, mode}, {u"brightness"_s, brightness}, {u"sensor_style"_s, style}, {u"ring_color"_s, ring}, {u"orientation"_s, 0}};
}

// ---- sanitizing helpers ----------------------------------------------------
int clampInt(const QJsonValue& v, int lo, int hi, int def) {
    return v.isDouble() ? qBound(lo, v.toInt(def), hi) : def;
}

QJsonArray colorList(const QJsonValue& v, int max) {
    QJsonArray out;
    for (const QJsonValue& c : v.toArray()) {
        const QColor col(c.toString());
        if (col.isValid()) out.append(col.name());
        if (out.size() >= max) break;
    }
    return out;
}

QString oneOf(const QJsonValue& v, const QSet<QString>& allowed, const QString& def) {
    const QString s = v.toString();
    return allowed.contains(s) ? s : def;
}

QJsonObject sanitizeLighting(const QJsonObject& in) {
    static const QSet<QString> modes{u"off"_s, u"fixed"_s, u"gradient"_s, u"breathing"_s, u"cycle"_s, u"spectrum"_s};
    static const QSet<QString> speeds{u"slow"_s, u"normal"_s, u"fast"_s};
    return {{u"mode"_s, oneOf(in.value(u"mode"_s), modes, u"fixed"_s)},
            {u"colors"_s, colorList(in.value(u"colors"_s), 8)},
            {u"brightness"_s, clampInt(in.value(u"brightness"_s), 0, 100, 100)},
            {u"speed"_s, oneOf(in.value(u"speed"_s), speeds, u"normal"_s)}};
}

QJsonObject sanitizeCooling(const QJsonObject& in, QString* err) {
    if (in.value(u"mode"_s).toString() == u"fixed"_s)
        return {{u"mode"_s, u"fixed"_s}, {u"duty"_s, clampInt(in.value(u"duty"_s), 0, 100, 60)}};
    QJsonArray pts;
    for (const QJsonValue& p : in.value(u"points"_s).toArray()) {
        const QJsonArray a = p.toArray();
        if (a.size() != 2 || !a[0].isDouble() || !a[1].isDouble()) continue;
        pts.append(QJsonArray{qBound(0.0, a[0].toDouble(), 100.0), qBound(0, a[1].toInt(), 100)});
        if (pts.size() >= 16) break;
    }
    if (pts.size() < 2) {
        *err = u"cooling curve needs at least two points"_s;
        return {};
    }
    return {{u"mode"_s, u"curve"_s}, {u"points"_s, pts}};
}

QJsonObject sanitizeLcd(const QJsonObject& in) {
    static const QSet<QString> modes{u"liquid"_s, u"sensors"_s, u"image"_s, u"gif"_s, u"off"_s};
    static const QSet<QString> fits{u"cover"_s, u"contain"_s, u"stretch"_s};
    static const QSet<QString> styles{u"liquid_ring"_s, u"cpu_gpu"_s, u"triple"_s};
    QJsonObject o{{u"mode"_s, oneOf(in.value(u"mode"_s), modes, u"liquid"_s)},
                  {u"brightness"_s, clampInt(in.value(u"brightness"_s), 0, 100, 70)},
                  {u"fit"_s, oneOf(in.value(u"fit"_s), fits, u"cover"_s)},
                  {u"sensor_style"_s, oneOf(in.value(u"sensor_style"_s), styles, u"liquid_ring"_s)}};
    const int orient = clampInt(in.value(u"orientation"_s), 0, 270, 0);
    o.insert(u"orientation"_s, (orient / 90) * 90);
    const QColor ring(in.value(u"ring_color"_s).toString());
    o.insert(u"ring_color"_s, ring.isValid() ? ring.name() : u"#ff7a29"_s);
    const QString path = in.value(u"path"_s).toString();
    if (!path.isEmpty()) o.insert(u"path"_s, path.left(4096));
    if ((o.value(u"mode"_s) == u"image"_s || o.value(u"mode"_s) == u"gif"_s) && path.isEmpty()) o.insert(u"mode"_s, u"liquid"_s);
    return o;
}

}  // namespace

QList<QJsonObject> builtins() {
    return {
        preset(u"gaming"_s, u"Gaming"_s, u"Crimson lighting, live CPU/GPU on the LCD, performance cooling"_s, u"#ff2d55"_s,
               all(u"static"_s, {u"#ff1f3d"_s}), light(u"breathing"_s, {u"#ff1f3d"_s, u"#ff6a00"_s}, 100),
               light(u"fixed"_s, {u"#ff1f3d"_s}, 90), lcd(u"sensors"_s, 80, u"cpu_gpu"_s, u"#ff1f3d"_s), u"performance"_s),
        preset(u"purple"_s, u"Purple"_s, u"Signature violet everywhere, balanced cooling"_s, u"#7c3aed"_s,
               all(u"static"_s, {u"#7c3aed"_s}), light(u"fixed"_s, {u"#7c3aed"_s}, 100), light(u"fixed"_s, {u"#7c3aed"_s}, 100),
               lcd(u"liquid"_s, 70), u"balanced"_s),
        preset(u"white"_s, u"White"_s, u"Clean neutral white at 80% brightness"_s, u"#e5e7eb"_s,
               all(u"static"_s, {u"#ffffff"_s}, 80), light(u"fixed"_s, {u"#ffffff"_s}, 80), light(u"fixed"_s, {u"#ffffff"_s}, 80),
               lcd(u"liquid"_s, 70, u"liquid_ring"_s, u"#e5e7eb"_s), u"balanced"_s),
        preset(u"rainbow"_s, u"Rainbow"_s, u"Hardware rainbow effects where devices support them"_s, u"#22d3ee"_s,
               all(u"rainbow"_s, {}, 100, 50), light(u"spectrum"_s, {}, 100), light(u"spectrum"_s, {}, 100, u"slow"_s),
               lcd(u"sensors"_s, 75, u"liquid_ring"_s, u"#22d3ee"_s), u"balanced"_s),
        preset(u"minimal"_s, u"Minimal"_s, u"Lights off, dim LCD, quiet cooling"_s, u"#94a3b8"_s,
               all(u"off"_s, {}), light(u"off"_s, {}, 0), light(u"off"_s, {}, 0), lcd(u"liquid"_s, 30), u"quiet"_s),
    };
}

QJsonObject sanitize(const QJsonObject& in, QString* error) {
    QString err;
    const QString name = in.value(u"name"_s).toString().trimmed().left(64);
    if (name.isEmpty()) {
        if (error) *error = u"Profile has no name"_s;
        return {};
    }
    if (in.value(u"schema"_s).toInt(1) > 1) {
        if (error) *error = u"Profile was made by a newer version (schema %1)"_s.arg(in.value(u"schema"_s).toInt());
        return {};
    }
    QJsonObject out{{u"schema"_s, 1}, {u"name"_s, name}, {u"description"_s, in.value(u"description"_s).toString().left(200)}};
    const QColor accent(in.value(u"accent"_s).toString());
    out.insert(u"accent"_s, accent.isValid() ? accent.name() : u"#ff7a29"_s);
    if (in.contains(u"id"_s)) out.insert(u"id"_s, in.value(u"id"_s).toString().left(80));

    const QJsonObject rgbIn = in.value(u"rgb"_s).toObject();
    QJsonObject rgb;
    if (rgbIn.contains(u"all"_s)) {
        static const QSet<QString> effects{u"static"_s, u"off"_s, u"rainbow"_s, u"breathing"_s};
        const QJsonObject a = rgbIn.value(u"all"_s).toObject();
        rgb.insert(u"all"_s, QJsonObject{{u"effect"_s, oneOf(a.value(u"effect"_s), effects, u"static"_s)},
                                         {u"colors"_s, colorList(a.value(u"colors"_s), 8)},
                                         {u"brightness"_s, clampInt(a.value(u"brightness"_s), 0, 100, 100)},
                                         {u"speed"_s, clampInt(a.value(u"speed"_s), 0, 100, 50)}});
    }
    QJsonArray devices;
    for (const QJsonValue& v : rgbIn.value(u"devices"_s).toArray()) {
        const QJsonObject d = v.toObject();
        if (d.value(u"name"_s).toString().isEmpty() || d.value(u"mode"_s).toString().isEmpty()) continue;
        QJsonObject e{{u"name"_s, d.value(u"name"_s).toString().left(128)},
                      {u"serial"_s, d.value(u"serial"_s).toString().left(128)},
                      {u"location"_s, d.value(u"location"_s).toString().left(256)},
                      {u"mode"_s, d.value(u"mode"_s).toString().left(64)},
                      {u"colors"_s, colorList(d.value(u"colors"_s), 4096)}};
        for (const QString& k : {u"brightness"_s, u"speed"_s})
            if (d.value(k).isDouble()) e.insert(k, clampInt(d.value(k), 0, 100, 100));
        if (d.value(u"direction"_s).isDouble()) e.insert(u"direction"_s, clampInt(d.value(u"direction"_s), 0, 16, 0));
        static const QSet<QString> softEffects{u"rainbow"_s, u"breathing"_s};
        if (softEffects.contains(d.value(u"softEffect"_s).toString())) {
            e.insert(u"softEffect"_s, d.value(u"softEffect"_s).toString());
            e.insert(u"softColors"_s, colorList(d.value(u"softColors"_s), 8));
        }
        devices.append(e);
    }
    if (!devices.isEmpty()) rgb.insert(u"devices"_s, devices);
    if (!rgb.isEmpty()) out.insert(u"rgb"_s, rgb);

    const QJsonObject kIn = in.value(u"kraken"_s).toObject();
    QJsonObject kraken;
    QJsonObject lighting;
    for (const QString& ch : {u"ring"_s, u"fans"_s})
        if (kIn.value(u"lighting"_s).toObject().contains(ch))
            lighting.insert(ch, sanitizeLighting(kIn.value(u"lighting"_s).toObject().value(ch).toObject()));
    if (!lighting.isEmpty()) kraken.insert(u"lighting"_s, lighting);
    QJsonObject cool;
    for (const QString& ch : {u"pump"_s, u"fan"_s}) {
        if (!kIn.value(u"cooling"_s).toObject().contains(ch)) continue;
        const QJsonObject c = sanitizeCooling(kIn.value(u"cooling"_s).toObject().value(ch).toObject(), &err);
        if (c.isEmpty()) {
            if (error) *error = ch + u": "_s + err;
            return {};
        }
        cool.insert(ch, c);
    }
    if (!cool.isEmpty()) kraken.insert(u"cooling"_s, cool);
    if (kIn.contains(u"lcd"_s)) kraken.insert(u"lcd"_s, sanitizeLcd(kIn.value(u"lcd"_s).toObject()));
    if (!kraken.isEmpty()) out.insert(u"kraken"_s, kraken);
    return out;
}

}  // namespace Presets
