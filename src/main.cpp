#include "core/Settings.h"
#include "devices/DeviceCatalog.h"
#include "kraken/KrakenService.h"
#include "openrgb/RgbService.h"
#include "profiles/ProfileManager.h"
#include "system/SystemMonitor.h"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSocketNotifier>
#include <QTimer>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {
int g_sigFd[2] = {-1, -1};

void onSignal(int) {
    const char c = 1;
    [[maybe_unused]] auto n = ::write(g_sigFd[0], &c, 1);
}

// SIGTERM/SIGINT/SIGHUP -> orderly quit, so the Kraken service is shut down
// cleanly (it releases the USB interfaces) and settings/session are flushed.
void installSignalHandlers(QCoreApplication* app) {
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_sigFd) != 0) return;
    auto* sn = new QSocketNotifier(g_sigFd[1], QSocketNotifier::Read, app);
    QObject::connect(sn, &QSocketNotifier::activated, app, [] {
        char c;
        [[maybe_unused]] auto n = ::read(g_sigFd[1], &c, 1);
        QCoreApplication::quit();
    });
    struct sigaction sa{};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    for (int s : {SIGTERM, SIGINT, SIGHUP}) sigaction(s, &sa, nullptr);
}
}  // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(u"orkc"_s);
    QGuiApplication::setApplicationDisplayName(u"SudoRGB"_s);
    QGuiApplication::setApplicationVersion(QStringLiteral(ORKC_VERSION));
    QGuiApplication::setDesktopFileName(u"orkc"_s);  // Wayland app_id for Hyprland rules
    QGuiApplication::setWindowIcon(QIcon(u":/qt/qml/Orkc/resources/icons/sudorgb.png"_s));

    QCommandLineParser cli;
    cli.setApplicationDescription(u"Unified OpenRGB + NZXT Kraken control center"_s);
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption restoreOpt(u"restore"_s, u"Headless: restore the last lighting/Kraken state, then exit."_s);
    QCommandLineOption shotOpt(u"screenshot"_s, u"Developer: render each page into <dir> and exit."_s, u"dir"_s);
    cli.addOption(restoreOpt);
    cli.addOption(shotOpt);
    cli.process(app);

    installSignalHandlers(&app);

    Settings settings;
    settings.load();
    RgbService rgb(&settings);
    KrakenService kraken(&settings);
    SystemMonitor system;
    ProfileManager profiles(&settings, &rgb, &kraken);
    profiles.load();

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
        profiles.saveSession();
        kraken.stop();
        settings.saveNow();
    });

    rgb.start();
    kraken.start();

    if (cli.isSet(restoreOpt)) {
        // Exit once both sides have had their chance (or after 45 s).
        auto* done = new QTimer(&app);
        done->setSingleShot(true);
        QObject::connect(done, &QTimer::timeout, &app, &QCoreApplication::quit);
        done->start(45000);
        auto maybeQuit = [&] {
            const bool rgbDone = rgb.connected() || !settings.autoRestoreProfile();
            const bool krakenDone = kraken.ready() || kraken.state() == u"conflict"_s || kraken.state() == u"disabled"_s ||
                                    kraken.state() == u"error"_s;
            if (rgbDone && krakenDone && !kraken.lcdBusy()) QTimer::singleShot(1500, &app, &QCoreApplication::quit);
        };
        QObject::connect(&rgb, &RgbService::devicesChanged, &app, maybeQuit);
        QObject::connect(&kraken, &KrakenService::stateChanged, &app, maybeQuit);
        QObject::connect(&kraken, &KrakenService::lcdChanged, &app, maybeQuit);
        return app.exec();
    }

    QQuickStyle::setStyle(u"Basic"_s);
    qmlRegisterSingletonInstance("Orkc.Backend", 1, 0, "AppSettings", &settings);
    qmlRegisterSingletonInstance("Orkc.Backend", 1, 0, "Rgb", &rgb);
    qmlRegisterSingletonInstance("Orkc.Backend", 1, 0, "Kraken", &kraken);
    qmlRegisterSingletonInstance("Orkc.Backend", 1, 0, "SystemInfo", &system);
    qmlRegisterSingletonInstance("Orkc.Backend", 1, 0, "Profiles", &profiles);
    HardwareCatalog hardware;
    qmlRegisterSingletonInstance("Orkc.Backend", 1, 0, "Hardware", &hardware);
    qmlRegisterUncreatableType<RgbDeviceModel>("Orkc.Backend", 1, 0, "RgbDeviceModel", u"provided by Rgb"_s);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("Orkc", "Main");
    if (engine.rootObjects().isEmpty()) return 1;

    if (cli.isSet(shotOpt)) {
        auto* win = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        const QString dir = cli.value(shotOpt);
        QDir().mkpath(dir);
        auto* step = new QTimer(&app);
        auto page = std::make_shared<int>(0);
        QObject::connect(step, &QTimer::timeout, &app, [win, dir, page, step] {
            if (*page > 0) win->grabWindow().save(u"%1/page-%2.png"_s.arg(dir).arg(*page - 1));
            if (*page >= 7) {
                step->stop();
                QCoreApplication::quit();
                return;
            }
            win->setProperty("currentPage", *page);
            ++*page;
        });
        step->start(qEnvironmentVariableIntValue("ORKC_SHOT_DELAY_MS") > 0 ? qEnvironmentVariableIntValue("ORKC_SHOT_DELAY_MS") : 2500);
    }
    return app.exec();
}
