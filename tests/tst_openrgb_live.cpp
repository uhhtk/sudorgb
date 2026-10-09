// Live protocol test against a real OpenRGB server.
//   ORKC_TEST_OPENRGB_PORT=6743 ./orkc_live_tests
// Skips when the variable is unset. Point it ONLY at a server whose devices you
// are happy to recolour (e.g. one started with OpenRGB's DebugDevices).
#include "openrgb/OpenRgbClient.h"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

class LiveOpenRgb : public QObject {
    Q_OBJECT
    OpenRgbClient* c = nullptr;

    bool waitConnected(int ms = 8000) {
        return QTest::qWaitFor([&] { return c->state() == OpenRgbClient::State::Connected; }, ms);
    }

private Q_SLOTS:
    void initTestCase() {
        const int port = qEnvironmentVariableIntValue("ORKC_TEST_OPENRGB_PORT");
        if (!port) QSKIP("ORKC_TEST_OPENRGB_PORT not set");
        c = new OpenRgbClient(this);
        c->connectToServer(u"127.0.0.1"_s, quint16(port));
        QVERIFY2(waitConnected(), qPrintable(c->lastError()));
        qInfo() << "protocol" << c->protocol() << "controllers" << c->controllers().size();
        QVERIFY(c->controllers().size() > 0);
        for (const auto& ctl : c->controllers())
            qInfo().noquote() << " -" << ctl.name << "|" << orgb::deviceTypeName(ctl.type) << "|" << ctl.modes.size() << "modes |"
                              << ctl.zones.size() << "zones |" << ctl.leds.size() << "LEDs | active:" << (ctl.currentMode() ? ctl.currentMode()->name : QString());
    }

    void perLedWriteRoundTrip() {
        int dev = -1, direct = -1;
        for (int i = 0; i < c->controllers().size() && dev < 0; ++i)
            for (int m = 0; m < c->controllers()[i].modes.size(); ++m)
                if (c->controllers()[i].modes[m].has(orgb::HasPerLedColor) && !c->controllers()[i].leds.isEmpty()) { dev = i; direct = m; break; }
        QVERIFY2(dev >= 0, "no per-LED capable device");
        orgb::Mode mode = c->controllers()[dev].modes[direct];
        mode.colorMode = orgb::ColorPerLed;
        QVERIFY(c->updateMode(dev, direct, mode));
        const QList<QRgb> want(c->controllers()[dev].leds.size(), qRgb(0x12, 0x34, 0x56));
        QVERIFY(c->updateLeds(dev, want));
        QTest::qWait(150);  // coalescing window
        // Force a re-read from the server and compare.
        QSignalSpy spy(c, &OpenRgbClient::controllerChanged);
        QVERIFY(c->setCustomMode(dev) || true);
        QVERIFY(QTest::qWaitFor([&] { return spy.count() > 0; }, 3000));
        QTest::qWait(600);
        QCOMPARE(c->controllers()[dev].colors, want);
    }

    void zoneWrite() {
        int dev = -1;
        for (int i = 0; i < c->controllers().size(); ++i)
            if (c->controllers()[i].zones.size() > 1) { dev = i; break; }
        if (dev < 0) QSKIP("no multi-zone device");
        const auto& ctl = c->controllers()[dev];
        const QList<QRgb> z(ctl.zones[1].ledsCount, qRgb(200, 10, 20));
        QVERIFY(c->updateZoneLeds(dev, 1, z));
        c->resync();
        QVERIFY(QTest::qWaitFor([&] { return c->state() == OpenRgbClient::State::Connected; }, 5000));
        QTest::qWait(300);
        const auto& after = c->controllers()[dev];
        QCOMPARE(after.colors.mid(after.zoneStart(1), z.size()), z);
    }

    void modeSwitchWithParameters() {
        for (int i = 0; i < c->controllers().size(); ++i) {
            const auto& ctl = c->controllers()[i];
            for (int m = 0; m < ctl.modes.size(); ++m) {
                if (!ctl.modes[m].has(orgb::HasSpeed)) continue;
                orgb::Mode mode = ctl.modes[m];
                mode.speed = mode.speedMax;
                QVERIFY(c->updateMode(i, m, mode));
                QTest::qWait(800);  // client schedules a refresh after mode changes
                QCOMPARE(c->controllers()[i].activeMode, m);
                QCOMPARE(c->controllers()[i].modes[m].speed, mode.speedMax);
                return;
            }
        }
        QSKIP("no mode with speed");
    }

    void reconnectsAfterDrop() {
        c->disconnectFromServer();
        QCOMPARE(c->state(), OpenRgbClient::State::Disconnected);
        c->connectToServer(u"127.0.0.1"_s, quint16(qEnvironmentVariableIntValue("ORKC_TEST_OPENRGB_PORT")));
        QVERIFY(waitConnected());
    }
};

QTEST_GUILESS_MAIN(LiveOpenRgb)
#include "tst_openrgb_live.moc"
