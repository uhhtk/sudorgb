// Software effects over a real OpenRGB server with debug (Direct-only) devices.
//   ORKC_TEST_OPENRGB_PORT=6743 ./orkc_softfx_tests
#include "core/Settings.h"
#include "openrgb/RgbService.h"

#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class SoftFx : public QObject {
    Q_OBJECT
    QTemporaryDir tmp;
    Settings* settings = nullptr;
    RgbService* rgb = nullptr;

    QVariantList colorsOf(int dev) {
        return rgb->devices()->data(rgb->devices()->index(dev), RgbDeviceModel::ColorsRole).toList();
    }

private Q_SLOTS:
    void initTestCase() {
        const int port = qEnvironmentVariableIntValue("ORKC_TEST_OPENRGB_PORT");
        if (!port) QSKIP("ORKC_TEST_OPENRGB_PORT not set");
        for (const char* v : {"XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"})
            qputenv(v, (tmp.path() + u'/' + QString::fromLatin1(v)).toUtf8());
        qputenv("ORKC_NO_NATIVE_DEVICES", "1");
        settings = new Settings(this);
        settings->setOpenRgbPort(port);
        settings->setAutoStartOpenRgb(false);
        rgb = new RgbService(settings, this);
        rgb->start();
        QVERIFY(QTest::qWaitFor([&] { return rgb->connected() && rgb->controllableCount() > 0; }, 8000));
    }

    void rainbowAnimatesDirectOnlyDevice() {
        QVERIFY(rgb->applyEffect(0, u"rainbow"_s, {}, 80, 100));
        QTest::qWait(300);
        const QVariantList a = colorsOf(0);
        QTest::qWait(500);
        const QVariantList b = colorsOf(0);
        QCOMPARE(a.size(), b.size());
        QVERIFY2(a != b, "rainbow is not animating");
        QSet<QRgb> hues;
        for (const QVariant& v : b) hues.insert(v.value<QColor>().rgb());
        QVERIFY2(hues.size() > 3, "frame is not a rainbow");
    }

    void staticColourStopsTheAnimation() {
        QVERIFY(rgb->setDeviceColor(0, QColor(10, 20, 30)));
        QTest::qWait(400);
        const QVariantList a = colorsOf(0);
        QTest::qWait(400);
        QCOMPARE(colorsOf(0), a);
        QCOMPARE(a.first().value<QColor>(), QColor(10, 20, 30));
    }

    void breathingWithoutHardwareModeIsAnimated() {
        QVERIFY(rgb->applyEffect(0, u"breathing"_s, {QColor(255, 0, 0)}, 100, 100));
        QList<int> reds;
        for (int i = 0; i < 6; ++i) { QTest::qWait(150); reds << colorsOf(0).first().value<QColor>().red(); }
        QVERIFY2(*std::max_element(reds.begin(), reds.end()) - *std::min_element(reds.begin(), reds.end()) > 40, "not breathing");
        rgb->setDeviceColor(0, Qt::black);
    }
};

QTEST_GUILESS_MAIN(SoftFx)
#include "tst_softfx_live.moc"
