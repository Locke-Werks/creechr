#include "app/creechr_app.h"
#include "app/tray_icon.h"
#include "creature/creechr.h"
#include "creature/world_context.h"
#include "render/overlay_window.h"
#include "render/sprite_atlas.h"
#include "util/logging.h"
#include "heist/hoard.h"
#include "ipc/extension_pipe_server.h"
#include "targets/cursor_target_provider.h"
#include "targets/extension_target_provider.h"
#include "targets/uia_target_provider.h"
#include "targets/window_target_provider.h"
#include "util/win32_helpers.h"
#include "world/fullscreen_detector.h"
#include "world/window_enumerator.h"

#include <QRandomGenerator>

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

    m_hoard = std::make_unique<cr::Hoard>();
    m_hoard->loadFromDisk(); // any orphans get logged + cleared

    m_creechr = std::make_unique<cr::Creechr>(*m_atlas);
    m_creechr->setHoard(m_hoard.get());

    m_overlay = std::make_unique<OverlayWindow>();
    m_overlay->setCreechr(m_creechr.get());
    m_overlay->setAtlas(m_atlas.get());
    m_overlay->show();

    m_windows = std::make_unique<cr::WindowEnumerator>();
#ifdef _WIN32
    m_windows->setSelfHwnd(reinterpret_cast<HWND>(m_overlay->winId()));
#endif

    m_fullscreen = std::make_unique<cr::FullscreenDetector>();
    m_winTargets = std::make_unique<cr::WindowTargetProvider>(m_windows.get());
    m_curTargets = std::make_unique<cr::CursorTargetProvider>();
    m_uiaTargets = std::make_unique<cr::UiaTargetProvider>();

    m_extPipe = std::make_unique<cr::ExtensionPipeServer>(this);
    if (!m_extPipe->start()) {
        LOG_WARN(QStringLiteral("extension pipe server failed to start, "
                                "browser theft will be unavailable"));
    }
    m_extTargets = std::make_unique<cr::ExtensionTargetProvider>(m_extPipe.get(), this);
    m_creechr->setExtensionProvider(m_extTargets.get());

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

    // unified ~60Hz tick. v0.1 had logic at 10Hz and render at 30Hz, on
    // separate timers. that meant between two consecutive renders the
    // position usually hadn't moved, so creechr looked like he was
    // hopping in 6-pixel steps. one timer at 60Hz, physics integrates
    // every render, problem solved. EnumWindows / fullscreen check are
    // rate-limited inside the tick to ~10Hz so we don't burn cpu on the
    // expensive stuff at 60Hz.
    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(16);
    m_tickTimer->setTimerType(Qt::PreciseTimer);
    connect(m_tickTimer, &QTimer::timeout, this, &CreechrApp::onTick);
    m_tickTimer->start();

    m_lastTickMs = QDateTime::currentMSecsSinceEpoch();
    m_lastWorldRefreshMs = 0;

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
    // spec §4.8 says pause stops the timers, not just no-ops them.
    // honoring that. resume restarts them with fresh timestamps so
    // the next dt isn't "however many seconds you were paused for".
    if (m_tickTimer) {
        if (paused) m_tickTimer->stop();
        else        m_tickTimer->start();
    }
    if (!paused) {
        m_lastTickMs = QDateTime::currentMSecsSinceEpoch();
    }
    LOG_INFO(paused ? QStringLiteral("paused") : QStringLiteral("resumed"));
    emit pauseChanged(m_paused);
}

void CreechrApp::quitGracefully()
{
    LOG_INFO(QStringLiteral("CreechrApp::quitGracefully"));
    if (m_hoard) {
        m_hoard->restoreAll(); // critical: never leave a window hidden
    }
    quit();
}

// v0.2 uses GetLastInputInfo via cr::win32::millisSinceLastInput() so
// keyboard activity counts too. v0.1 used a hand-rolled cursor tracker
// that ignored typing — embarrassing in retrospect.
namespace {
cr::WorldContext g_cachedWorld;
} // namespace

void CreechrApp::onTick()
{
    if (m_paused || !m_creechr || !m_overlay) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int dt = static_cast<int>(now - m_lastTickMs);
    m_lastTickMs = now;
    if (dt < 0)   dt = 0;
    if (dt > 500) dt = 500; // dont let a long pause snap-translate him

    // expensive world refreshes (EnumWindows, fullscreen state, screen
    // geometry recalc) are rate-limited to ~10Hz inside this 60Hz tick
    // because none of them change fast enough to be worth running every
    // frame.
    if (now - m_lastWorldRefreshMs >= 100) {
        m_lastWorldRefreshMs = now;
        QRect db;
        for (QScreen* s : QGuiApplication::screens()) {
            db = db.united(s->geometry());
        }
        g_cachedWorld.virtualDesktop = db;
        if (m_windows) {
            // single snapshot call, then split into parallel rect+hwnd
            // vectors. the gnaw state needs hwnds so it can talk to
            // dwm directly each tick (10Hz cache is too coarse for
            // smooth window-following).
            const auto full = m_windows->snapshot();
            g_cachedWorld.windowRects.clear();
            g_cachedWorld.windowHwnds.clear();
            g_cachedWorld.windowRects.reserve(full.size());
            g_cachedWorld.windowHwnds.reserve(full.size());
            for (const auto& wi : full) {
                g_cachedWorld.windowRects.push_back(wi.frame);
                g_cachedWorld.windowHwnds.push_back(reinterpret_cast<void*>(wi.hwnd));
            }
        }
        if (m_fullscreen) g_cachedWorld.fullscreenActive = m_fullscreen->isFullscreenActive();
    }
    // cheap stuff every tick
    g_cachedWorld.cursorPos = QCursor::pos();
    g_cachedWorld.msSinceLastInput = cr::win32::millisSinceLastInput();

    // hide the overlay when a fullscreen game/presentation is going.
    if (m_overlay) {
        const bool wantVisible = !g_cachedWorld.fullscreenActive;
        if (wantVisible != m_overlay->isVisible()) {
            if (wantVisible) {
                m_overlay->show();
            } else {
                m_overlay->hide();
                LOG_INFO(QStringLiteral("fullscreen detected, hiding overlay"));
            }
        }
    }

    // heist orchestration. constants are tuned for 60Hz ticks now:
    //   - bounded(900) at 60Hz = ~1 attempt per 15 seconds on average
    //   - 8s hard cooldown still applies
    //   - input-idle gate dropped from 5s to 2.5s so casual breaks count
    // when an attempt fires the random gate but no target is found we
    // log it at info so the user can see *why* nothing visible happened.
    //
    // dev override: CREECHR_HEIST_NOW=1 in the env removes the random
    // gate and the input idle gate so heists fire as fast as the 8s
    // cooldown allows. for "show me it works" runs.
    static const bool kForceHeist = !qgetenv("CREECHR_HEIST_NOW").isEmpty();
    const qint64 sinceLastAttempt = now - m_lastHeistAttemptMs;
    const bool inputGateOk = kForceHeist || g_cachedWorld.msSinceLastInput >= 2500;
    const bool randomGateOk = kForceHeist
        ? (sinceLastAttempt > 2000)
        : (QRandomGenerator::global()->bounded(900) == 0);
    if (m_creechr && !m_creechr->heist()
        && inputGateOk
        && sinceLastAttempt > 2000
        && randomGateOk) {
        m_lastHeistAttemptMs = now;
        // roll table:
        //   0..3 (40%) -> window heist
        //   4..5 (20%) -> uia heist
        //   6..7 (20%) -> dom heist (only if extension is connected and has cache)
        //   8..9 (20%) -> cursor heist
        // any provider that comes back empty falls through to cursor.
        const int roll = QRandomGenerator::global()->bounded(10);
        std::optional<cr::HeistTarget> target;
        const char* whichRoll = "?";
        if (roll < 4 && m_winTargets) {
            whichRoll = "window";
            target = m_winTargets->pickRandom(g_cachedWorld.virtualDesktop);
        } else if (roll < 6 && m_uiaTargets) {
            whichRoll = "uia";
            target = m_uiaTargets->pickRandom(g_cachedWorld.virtualDesktop);
        } else if (roll < 8 && m_extTargets && m_extTargets->isReady()) {
            whichRoll = "dom";
            target = m_extTargets->pickRandom(g_cachedWorld.virtualDesktop);
        } else {
            whichRoll = "cursor";
        }
        if (!target.has_value() && m_curTargets) {
            target = m_curTargets->current();
        }
        if (target.has_value()) {
            LOG_INFO(QStringLiteral("orchestrator: heist start (%1) -> %2")
                .arg(whichRoll).arg(target->label));
            m_creechr->beginHeist(*target);
        } else {
            LOG_INFO(QStringLiteral("orchestrator: %1 attempt found no target").arg(whichRoll));
        }
    }

    // physics + animator + render — all every tick now so movement is
    // visibly continuous instead of hopping in 100ms chunks.
    m_creechr->tickLogic(dt, g_cachedWorld);
    m_creechr->tickRender(dt);
    m_overlay->update();
}
