// Unit tests for the protocol codec, persistence and profile validation.
#include "core/JsonStore.h"
#include "devices/NativeDevices.h"
#include "openrgb/OrgbProtocol.h"
#include "profiles/Presets.h"

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestOrkc : public QObject {
    Q_OBJECT

    static orgb::Controller sample() {
        orgb::Controller c;
        c.type = 0;
        c.name = u"ASUS Aura"_s;
        c.vendor = u"ASUS"_s;
        c.description = u"Motherboard"_s;
        c.version = u"1.0"_s;
        c.serial = u"9876543210"_s;
        c.location = u"HID: /dev/hidraw4"_s;
        c.activeMode = 1;
        orgb::Mode direct;
        direct.name = u"Direct"_s;
        direct.flags = orgb::HasPerLedColor;
        direct.colorMode = orgb::ColorPerLed;
        orgb::Mode breathing;
        breathing.name = u"Breathing"_s;
        breathing.flags = orgb::HasSpeed | orgb::HasBrightness | orgb::HasModeSpecificColor;
        breathing.speedMin = 4; breathing.speedMax = 0; breathing.speed = 2;   // inverted range, as some devices report
        breathing.brightnessMin = 0; breathing.brightnessMax = 255; breathing.brightness = 128;
        breathing.colorsMin = 1; breathing.colorsMax = 2;
        breathing.colorMode = orgb::ColorModeSpecific;
        breathing.colors = {qRgb(255, 0, 0)};
        c.modes = {direct, breathing};
        orgb::Zone z;
        z.name = u"Keyboard"_s;
        z.type = 2;
        z.ledsMin = z.ledsMax = z.ledsCount = 4;
        z.matrixHeight = 2; z.matrixWidth = 2;
        z.matrix = {0, 1, 2, 3};
        z.segments = {{u"left"_s, 1, 0, 2}};
        orgb::Zone strip;
        strip.name = u"ARGB 1"_s;
        strip.type = 1;
        strip.ledsMin = 0; strip.ledsMax = 120; strip.ledsCount = 3;
        c.zones = {z, strip};
        for (int i = 0; i < 7; ++i) {
            c.leds.append({u"LED %1"_s.arg(i), uint32_t(i)});
            c.colors.append(qRgb(i * 10, 255 - i, i));
        }
        return c;
    }

private Q_SLOTS:
    void controllerRoundTrip_data() {
        QTest::addColumn<uint32_t>("protocol");
        for (uint32_t v : {0u, 1u, 3u, 4u}) QTest::newRow(qPrintable(u"v%1"_s.arg(v))) << v;
    }
    void controllerRoundTrip() {
        QFETCH(uint32_t, protocol);
        const orgb::Controller c = sample();
        const auto parsed = orgb::parseController(orgb::serializeController(c, protocol), protocol);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->name, c.name);
        QCOMPARE(parsed->vendor, protocol >= 1 ? c.vendor : QString());
        QCOMPARE(parsed->modes.size(), 2);
        QCOMPARE(parsed->modes[1].speedMin, 4u);
        QCOMPARE(parsed->modes[1].brightness, protocol >= 3 ? 128u : 0u);
        QCOMPARE(parsed->zones[0].matrix.size(), 4);
        QCOMPARE(parsed->zones[0].segments.size(), protocol >= 4 ? 1 : 0);
        QCOMPARE(parsed->colors, c.colors);
        QCOMPARE(parsed->zoneStart(1), 4);
        QCOMPARE(parsed->activeMode, 1);
    }

    void truncatedDataIsRejected() {
        const QByteArray full = orgb::serializeController(sample(), 4);
        for (int cut : {5, 20, 60, int(full.size()) - 1})
            QVERIFY2(!orgb::parseController(full.left(cut), 4).has_value(), qPrintable(u"cut at %1"_s.arg(cut)));
    }

    void colourWireOrder() {
        QCOMPARE(orgb::toWire(qRgb(0x11, 0x22, 0x33)), 0x00332211u);
        QCOMPARE(orgb::fromWire(0x00332211u), qRgb(0x11, 0x22, 0x33));
    }

    void headerParsing() {
        const QByteArray p = orgb::packet(3, orgb::UpdateLeds, QByteArray(10, 'x'));
        const auto h = orgb::parseHeader(p);
        QVERIFY(h);
        QCOMPARE(h->deviceIndex, 3u);
        QCOMPARE(h->packetId, uint32_t(orgb::UpdateLeds));
        QCOMPARE(h->size, 10u);
        QByteArray bad = p;
        bad[0] = 'X';
        QVERIFY(!orgb::parseHeader(bad));
    }

    void updateLedsLayout() {
        const QByteArray b = orgb::packUpdateLeds({qRgb(1, 2, 3), qRgb(4, 5, 6)});
        QCOMPARE(b.size(), 4 + 2 + 8);
        QCOMPARE(uchar(b[0]), uchar(14));  // data_size includes itself
        QCOMPARE(uchar(b[4]), uchar(2));   // num_colors
        QCOMPARE(uchar(b[6]), uchar(1));   // R first on the wire
    }

    void atomicWriteAndCorruptRecovery() {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"x.json"_s);
        QVERIFY(JsonStore::writeAtomic(path, {{u"a"_s, 1}}));
        bool ok = false;
        QCOMPARE(JsonStore::read(path, &ok).value(u"a"_s).toInt(), 1);
        QVERIFY(ok);
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ not json");
        f.close();
        QString err;
        QVERIFY(JsonStore::read(path, &ok, &err).isEmpty());
        QVERIFY(!ok);
        QVERIFY(!QFile::exists(path));  // moved aside, never silently overwritten
        QCOMPARE(QDir(dir.path()).entryList({u"x.json.corrupt-*"_s}).size(), 1);
    }

    void builtinPresetsAreValid() {
        const auto presets = Presets::builtins();
        QCOMPARE(presets.size(), 5);
        QStringList names;
        for (const auto& p : presets) {
            QString err;
            QVERIFY2(!Presets::sanitize(p, &err).isEmpty(), qPrintable(err));
            names << p.value(u"name"_s).toString();
        }
        QCOMPARE(names, (QStringList{u"Gaming"_s, u"Purple"_s, u"White"_s, u"Rainbow"_s, u"Minimal"_s}));
    }

    void sanitizeRejectsAndClamps() {
        QString err;
        QVERIFY(Presets::sanitize({{u"name"_s, u""_s}}, &err).isEmpty());
        QVERIFY(Presets::sanitize({{u"name"_s, u"x"_s}, {u"schema"_s, 99}}, &err).isEmpty());
        const QJsonObject in{
            {u"name"_s, u"Evil"_s},
            {u"junk"_s, u"dropped"_s},
            {u"kraken"_s, QJsonObject{
                {u"lighting"_s, QJsonObject{{u"ring"_s, QJsonObject{{u"mode"_s, u"rm -rf"_s}, {u"brightness"_s, 900}, {u"colors"_s, QJsonArray{u"#ff0000"_s, u"nope"_s}}}}}},
                {u"lcd"_s, QJsonObject{{u"mode"_s, u"gif"_s}, {u"orientation"_s, 135}}},  // gif without path -> liquid
            }},
        };
        const QJsonObject out = Presets::sanitize(in, &err);
        QVERIFY(!out.contains(u"junk"_s));
        const QJsonObject ring = out[u"kraken"_s][u"lighting"_s][u"ring"_s].toObject();
        QCOMPARE(ring[u"mode"_s].toString(), u"fixed"_s);
        QCOMPARE(ring[u"brightness"_s].toInt(), 100);
        QCOMPARE(ring[u"colors"_s].toArray().size(), 1);
        QCOMPARE(out[u"kraken"_s][u"lcd"_s][u"mode"_s].toString(), u"liquid"_s);
        QCOMPARE(out[u"kraken"_s][u"lcd"_s][u"orientation"_s].toInt(), 90);
    }

    void gloriousLightingPayload() {
        const auto f = glorious::lightingPayload(glorious::Static, 0x13, 0x07, {qRgb(0x12, 0x34, 0x56)});
        QCOMPARE(f.size(), 3);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(f[i].size(), 64);
            QCOMPARE(QByteArray(f[i].constData(), 6), QByteArray::fromHex("0302fb") + char(i) + char(1) + char(glorious::Static));
        }
        QCOMPARE(uchar(f[0][6]), uchar(0x14));  // never 0: the firmware would revert to its stored profile
        QCOMPARE(uchar(f[0][7]), uchar(0x14));
        QCOMPARE(uchar(f[0][8]), uchar(1));     // static uses one colour
        QCOMPARE(uchar(f[0][9]), uchar(0x05));  // speed quantised
        QCOMPARE(uchar(f[0][10]), uchar(0x13)); // master brightness
        QCOMPARE(uchar(glorious::lightingPayload(glorious::Static, 0, 0x0a, {})[0][10]), uchar(0x01));  // 0 is clamped to 1
        QCOMPARE(f[0].mid(11, 3), QByteArray::fromHex("123456"));  // RGB order
        const auto rave = glorious::lightingPayload(glorious::Rave, 20, 20, {qRgb(255, 0, 0), qRgb(0, 0, 255)});
        QCOMPARE(uchar(rave[0][8]), uchar(2));
        QCOMPARE(rave[1].mid(6, 3), QByteArray::fromHex("0000ff"));  // palette slot 1
    }

    void gloriousModesMatchGenericEffects() {
        QStringList names;
        for (const auto& m : glorious::modes()) names << m.name;
        for (const QString& want : {u"Static"_s, u"Breathing"_s, u"Rainbow"_s, u"Off"_s}) QVERIFY(names.contains(want));
    }
};

QTEST_GUILESS_MAIN(TestOrkc)
#include "tst_orkc.moc"
