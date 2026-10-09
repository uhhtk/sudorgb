// End-to-end: C++ KrakenService -> real kraken_service.py (simulated cooler).
#include "core/Settings.h"
#include "kraken/KrakenService.h"

#include <QSet>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class KrakenIpc : public QObject {
    Q_OBJECT
    QTemporaryDir tmp;
    Settings* settings = nullptr;
    KrakenService* k = nullptr;

    QStringList frames(const QString& ch) const {
        QStringList out;
        for (const QString& l : k->logTail().split(u'\n'))
            if (l.contains(u"light frame "_s + ch)) out << l.section(u"light frame "_s + ch, 1).trimmed();
        return out;
    }

private Q_SLOTS:
    void initTestCase() {
        for (const char* v : {"XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"})
            qputenv(v, (tmp.path() + u'/' + QString::fromLatin1(v)).toUtf8());
        qputenv("ORKC_KRAKEN_MOCK", "1");
        qputenv("ORKC_KRAKEN_DEBUG", "1");
        settings = new Settings(this);
        k = new KrakenService(settings, this);
        k->start();
        QVERIFY(QTest::qWaitFor([&] { return k->ready(); }, 15000));
    }

    void rainbowFromAllDevicesRunsAsFirmwareEffect() {
        // Exactly what LightingPage.applyAll sends for "Rainbow".
        k->setLighting(u"all"_s, {{u"mode"_s, u"spectrum"_s}, {u"colors"_s, QVariantList{u"#ff7a29"_s}},
                                  {u"brightness"_s, 100}, {u"speed"_s, u"normal"_s}});
        QTest::qWait(2500);
        int ring = 0, fans = 0;
        for (const QString& l : k->logTail().split(u'\n')) {
            ring += l.contains(u"light effect ring spectrum ok=True"_s);
            fans += l.contains(u"light effect fans spectrum ok=True"_s);
        }
        QCOMPARE(ring, 1);  // sent once: the cooler animates it, nothing is streamed
        QCOMPARE(fans, 1);
        QVERIFY(frames(u"ring"_s).isEmpty());
        QCOMPARE(k->desired()[u"lighting"_s].toMap()[u"ring"_s].toMap()[u"mode"_s].toString(), u"spectrum"_s);
    }

    void cleanupTestCase() { k->stop(); }
};

QTEST_GUILESS_MAIN(KrakenIpc)
#include "tst_kraken_ipc.moc"
