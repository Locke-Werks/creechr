#include "app/creechr_app.h"
#include "app/tray_icon.h"
#include "render/overlay_window.h"
#include "util/logging.h"

#include <QByteArray>
#include <QTimer>

CreechrApp::CreechrApp(int& argc, char** argv)
    : QApplication(argc, argv)
{
    setApplicationName(QStringLiteral("creechr"));
    setApplicationVersion(QStringLiteral("0.1.0"));
    setOrganizationName(QStringLiteral("creechr"));
    // critical: with no main window we DO NOT want qt to quit when
    // the last widget closes. there is no last widget. there's a tray.
    setQuitOnLastWindowClosed(false);
}

CreechrApp::~CreechrApp() = default;

void CreechrApp::start()
{
    cr::initLogging();
    // dev knob: set CREECHR_LOG_LEVEL=debug to get the chatty stuff.
    // valid values: trace debug info warn error. default is info.
    const QByteArray lvl = qgetenv("CREECHR_LOG_LEVEL").toLower();
    if      (lvl == "trace") cr::setLogLevel(cr::LogLevel::Trace);
    else if (lvl == "debug") cr::setLogLevel(cr::LogLevel::Debug);
    else if (lvl == "warn")  cr::setLogLevel(cr::LogLevel::Warn);
    else if (lvl == "error") cr::setLogLevel(cr::LogLevel::Error);
    LOG_INFO(QStringLiteral("CreechrApp::start"));

    m_overlay = std::make_unique<OverlayWindow>();
    m_overlay->show();

    m_tray = std::make_unique<TrayIcon>(this);
    m_tray->show();

    // dev convenience: if CREECHR_TEST_EXIT_MS is set in the env, schedule
    // a quit after that many ms. this is so the build/test loop can run
    // the binary without it sitting there forever waiting on a tray click.
    // production runs will never set this and the timer never fires.
    const QByteArray testExit = qgetenv("CREECHR_TEST_EXIT_MS");
    if (!testExit.isEmpty()) {
        bool ok = false;
        const int ms = testExit.toInt(&ok);
        if (ok && ms > 0) {
            QTimer::singleShot(ms, this, &CreechrApp::quitGracefully);
        }
    }
}

void CreechrApp::setPaused(bool paused)
{
    if (m_paused == paused) {
        return;
    }
    m_paused = paused;
    emit pauseChanged(m_paused);
}

void CreechrApp::quitGracefully()
{
    // nothing to clean up yet. when there is, this is where it goes:
    //  - restore stolen items from the hoard
    //  - kill any active occluders
    //  - flush the log
    quit();
}
