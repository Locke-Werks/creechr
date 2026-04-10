#include "app/creechr_app.h"
#include "app/tray_icon.h"
#include "creature/creechr.h"
#include "creature/world_context.h"
#include "render/overlay_window.h"
#include "render/sprite_atlas.h"
#include "util/logging.h"
#include "world/window_enumerator.h"

#include <QByteArray>
#include <QCursor>
#include <QDateTime>
#include <QGuiApplication>
#include <QScreen>
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

    m_atlas = std::make_unique<cr::SpriteAtlas>();
    m_atlas->makePlaceholder();

    m_creechr = std::make_unique<cr::Creechr>(*m_atlas);

    m_overlay = std::make_unique<OverlayWindow>();
    m_overlay->setCreechr(m_creechr.get());
    m_overlay->setAtlas(m_atlas.get());
    m_overlay->show();

    m_windows = std::make_unique<cr::WindowEnumerator>();
#ifdef _WIN32
    m_windows->setSelfHwnd(reinterpret_cast<HWND>(m_overlay->winId()));
#endif

    // give creechr a real world before letting his states fire enter()
    cr::WorldContext bootstrapWorld;
    QRect db;
    for (QScreen* s : QGuiApplication::screens()) {
        db = db.united(s->geometry());
    }
    bootstrapWorld.virtualDesktop = db;
    bootstrapWorld.cursorPos = QCursor::pos();
    m_creechr->initialize(bootstrapWorld);

    m_tray = std::make_unique<TrayIcon>(this);
    m_tray->show();

    // logic at 10Hz, render at 30Hz. spec §4.2.
    m_logicTimer = new QTimer(this);
    m_logicTimer->setInterval(100);
    connect(m_logicTimer, &QTimer::timeout, this, &CreechrApp::onLogicTick);
    m_logicTimer->start();

    m_renderTimer = new QTimer(this);
    m_renderTimer->setInterval(33);
    connect(m_renderTimer, &QTimer::timeout, this, &CreechrApp::onRenderTick);
    m_renderTimer->start();

    m_lastLogicMs  = QDateTime::currentMSecsSinceEpoch();
    m_lastRenderMs = m_lastLogicMs;

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
    LOG_INFO(QStringLiteral("CreechrApp::quitGracefully"));
    quit();
}

namespace {
// hand-rolled "millis since last input" — in v0.1 we just track the
// cursor position and reset our own counter when it moves. v0.2 swaps
// in GetLastInputInfo for keyboard awareness too.
struct InputIdleTracker {
    QPoint lastCursor;
    qint64 lastChangeMs = 0;
    int sample(qint64 nowMs)
    {
        const QPoint c = QCursor::pos();
        if (c != lastCursor) {
            lastCursor = c;
            lastChangeMs = nowMs;
        }
        if (lastChangeMs == 0) {
            lastChangeMs = nowMs;
        }
        return static_cast<int>(nowMs - lastChangeMs);
    }
};
InputIdleTracker g_idle;
} // namespace

void CreechrApp::onLogicTick()
{
    if (m_paused || !m_creechr) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int dt = static_cast<int>(now - m_lastLogicMs);
    m_lastLogicMs = now;
    if (dt < 0)   dt = 0;
    if (dt > 500) dt = 500; // dont let a long pause snap-translate him

    cr::WorldContext world;
    QRect db;
    for (QScreen* s : QGuiApplication::screens()) {
        db = db.united(s->geometry());
    }
    world.virtualDesktop = db;
    world.cursorPos = QCursor::pos();
    world.msSinceLastInput = g_idle.sample(now);
    if (m_windows) {
        world.windowRects = m_windows->snapshotRects();
    }

    m_creechr->tickLogic(dt, world);
}

void CreechrApp::onRenderTick()
{
    if (m_paused || !m_creechr || !m_overlay) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int dt = static_cast<int>(now - m_lastRenderMs);
    m_lastRenderMs = now;
    if (dt < 0)   dt = 0;
    if (dt > 500) dt = 500;

    m_creechr->tickRender(dt);
    m_overlay->update();
}
