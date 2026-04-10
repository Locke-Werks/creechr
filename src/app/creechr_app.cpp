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

#include <QDesktopServices>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QUrl>

#ifdef _WIN32
#  include <psapi.h>
#endif

namespace {
// cached world snapshot, refreshed at ~10Hz inside onTick. defined here
// at the top of the file so the tray-menu helper functions can also
// see it (they fire heists from outside the tick loop).
cr::WorldContext g_cachedWorld;
// previous snapshot's window positions, indexed by hwnd. used to
// compute the windowDeltas field each refresh. cleared when an hwnd
// drops out of the snapshot (window closed).
QHash<void*, QPoint> g_prevWindowPositions;

#ifdef _WIN32
// is this hwnd one of the windows creechr should be afraid of?
// covers elevated terminals (window title starts with "Administrator:"),
// known scary processes (taskmgr / regedit / mmc / msconfig / etc), and
// the credential-dialog xaml host class. consent.exe + the actual UAC
// secure-desktop window are NOT detectable from this process because
// they live on a separate desktop — those will scare him only by
// proximity to whatever launched them.
bool isScaryWindow(HWND hwnd, QString* whyOut)
{
    if (!hwnd) return false;

    wchar_t cls[256] = {};
    GetClassNameW(hwnd, cls, 256);
    const QString className = QString::fromWCharArray(cls);
    if (className == QLatin1String("$$$Secure UI App Wnd")
        || className == QLatin1String("Credential Dialog Xaml Host")
        || className.startsWith(QLatin1String("UAC"))) {
        if (whyOut) *whyOut = QStringLiteral("secure dialog");
        return true;
    }

    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, 256);
    const QString titleStr = QString::fromWCharArray(title);
    if (titleStr.startsWith(QLatin1String("Administrator:"))) {
        if (whyOut) *whyOut = QStringLiteral("elevated terminal");
        return true;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    wchar_t path[MAX_PATH] = {};
    DWORD sz = MAX_PATH;
    bool isScary = false;
    if (QueryFullProcessImageNameW(h, 0, path, &sz)) {
        QString p = QString::fromWCharArray(path).toLower();
        const int slash = p.lastIndexOf('\\');
        const QString name = (slash >= 0) ? p.mid(slash + 1) : p;
        // these are the windows where creechr should not be poking
        // around. taskmgr / regedit / mmc / etc are user-shell visible
        // and "look administrative". consent.exe normally hides on the
        // secure desktop but check for it anyway in case its visible.
        static const QStringList kScaryNames = {
            QStringLiteral("taskmgr.exe"),
            QStringLiteral("regedit.exe"),
            QStringLiteral("mmc.exe"),
            QStringLiteral("msconfig.exe"),
            QStringLiteral("services.exe"),
            QStringLiteral("certmgr.exe"),
            QStringLiteral("perfmon.exe"),
            QStringLiteral("eventvwr.exe"),
            QStringLiteral("compmgmt.exe"),
            QStringLiteral("consent.exe"),
            QStringLiteral("logonui.exe"),
            QStringLiteral("winlogon.exe"),
        };
        if (kScaryNames.contains(name)) {
            if (whyOut) *whyOut = name;
            isScary = true;
        }
    }
    CloseHandle(h);
    return isScary;
}
#endif
} // namespace

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
    // optional sprite override via env. if CREECHR_SPRITE points at a
    // valid PNG we replace the procedural texture with the loaded one.
    // animation grid (8 cols × 15 rows of 48px) is still applied — the
    // user's PNG needs to follow that layout or anims will land wrong.
    const QByteArray spritePath = qgetenv("CREECHR_SPRITE");
    if (!spritePath.isEmpty()) {
        m_atlas->loadFromFile(QString::fromLocal8Bit(spritePath));
    }

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

void CreechrApp::releaseEverything()
{
    LOG_INFO(QStringLiteral("tray: releaseEverything"));
    if (m_hoard) m_hoard->restoreAll();
    if (m_creechr) {
        m_creechr->clearHeist();
        m_creechr->speak(QStringLiteral("fine, take it"), 1500);
    }
}

void CreechrApp::openLogFolder()
{
    LOG_INFO(QStringLiteral("tray: openLogFolder"));
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void CreechrApp::fireHeistNow()
{
    LOG_INFO(QStringLiteral("tray: fireHeistNow"));
    if (!m_creechr || m_creechr->heist()) return;
    // pick whatever's available — prefer window, then dom, then uia, then cursor
    std::optional<cr::HeistTarget> target;
    if (m_winTargets) target = m_winTargets->pickRandom(g_cachedWorld.virtualDesktop);
    if (!target.has_value() && m_extTargets && m_extTargets->isReady())
        target = m_extTargets->pickRandom(g_cachedWorld.virtualDesktop);
    if (!target.has_value() && m_uiaTargets)
        target = m_uiaTargets->pickRandom(g_cachedWorld.virtualDesktop);
    if (!target.has_value() && m_curTargets)
        target = m_curTargets->current();
    if (target.has_value()) {
        LOG_INFO(QStringLiteral("tray: fire heist on %1").arg(target->label));
        m_creechr->beginHeist(*target);
        m_lastHeistAttemptMs = QDateTime::currentMSecsSinceEpoch();
    }
}

// v0.2 uses GetLastInputInfo via cr::win32::millisSinceLastInput() so
// keyboard activity counts too. v0.1 used a hand-rolled cursor tracker
// that ignored typing — embarrassing in retrospect. g_cachedWorld is
// defined at the top of this file.

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
            // smooth window-following). also computes per-window
            // deltas since the previous snapshot so the drag-chase
            // detection in WalkState has something to look at.
            const auto full = m_windows->snapshot();
            g_cachedWorld.windowRects.clear();
            g_cachedWorld.windowHwnds.clear();
            g_cachedWorld.windowDeltas.clear();
            g_cachedWorld.windowRects.reserve(full.size());
            g_cachedWorld.windowHwnds.reserve(full.size());
            g_cachedWorld.windowDeltas.reserve(full.size());
            QHash<void*, QPoint> nextPrev;
            nextPrev.reserve(full.size());
            for (const auto& wi : full) {
                void* hwndPtr = reinterpret_cast<void*>(wi.hwnd);
                g_cachedWorld.windowRects.push_back(wi.frame);
                g_cachedWorld.windowHwnds.push_back(hwndPtr);
                QPoint delta(0, 0);
                if (auto it = g_prevWindowPositions.find(hwndPtr);
                    it != g_prevWindowPositions.end()) {
                    delta = wi.frame.topLeft() - it.value();
                }
                g_cachedWorld.windowDeltas.push_back(delta);
                nextPrev.insert(hwndPtr, wi.frame.topLeft());
            }
            g_prevWindowPositions = std::move(nextPrev);
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

    // SCARY ADMIN DETECTION: scan visible windows for things that
    // smell administrative (taskmgr, regedit, mmc, elevated terminals,
    // credential dialogs). if a scary one is within 350 px of creechr,
    // he flees — high horizontal velocity in the opposite direction
    // plus an upward kick. flee state is requested via Creechr::
    // requestFlee() which Idle/Walk pick up at the top of their tick
    // and convert to a transition into FlungState.
#ifdef _WIN32
    if (m_creechr && (now - m_lastScareMs) > 6000) {
        const QString stateName = m_creechr->stateMachine().currentName();
        const bool fleeable = (stateName == QLatin1String("idle")
                             || stateName == QLatin1String("walk"));
        if (fleeable) {
            for (int i = 0; i < g_cachedWorld.windowHwnds.size(); ++i) {
                HWND hwnd = static_cast<HWND>(g_cachedWorld.windowHwnds[i]);
                QString why;
                if (!isScaryWindow(hwnd, &why)) continue;
                const QRect& r = g_cachedWorld.windowRects[i];
                const int dx = r.center().x() - static_cast<int>(m_creechr->position().x());
                if (qAbs(dx) > 350) continue;
                m_lastScareMs = now;
                LOG_INFO(QStringLiteral("scared by '%1' (%2 px away)").arg(why).arg(dx));
                m_creechr->speakRandom({
                    QStringLiteral("NO"),
                    QStringLiteral("EVIL"),
                    QStringLiteral("scary"),
                    QStringLiteral("RUN"),
                    QStringLiteral("ABORT"),
                    QStringLiteral("DANGER"),
                    QStringLiteral("uh oh"),
                    QStringLiteral("not that one"),
                    QStringLiteral("not the registry"),
                }, 2200);
                // flee opposite to the scary thing
                const double fleeVx = (dx > 0) ? -340.0 : 340.0;
                m_creechr->setVelocity({ fleeVx, -260.0 });
                m_creechr->setFacingRight(dx <= 0);
                m_creechr->requestFlee();
                break;
            }
        }
    }
#endif

    // window-drag noticing: when the user drags a window quickly,
    // creechr says something and turns to face it. distinct from
    // gnaw's shake-detection (which only fires while ATTACHED to a
    // window). this fires when he's just walking around and notices
    // someone fling a window across the screen. 4-second cooldown.
    if (m_creechr && (now - m_lastDragReactMs) > 4000
        && (m_creechr->stateMachine().currentName() == QLatin1String("walk")
            || m_creechr->stateMachine().currentName() == QLatin1String("idle"))) {
        for (int i = 0; i < g_cachedWorld.windowDeltas.size(); ++i) {
            const QPoint d = g_cachedWorld.windowDeltas[i];
            const int mag = qAbs(d.x()) + qAbs(d.y());
            if (mag < 35) continue; // not fast enough — ~350 px/sec at 10Hz
            // also require the window to be reasonably nearby in x
            const QRect& r = g_cachedWorld.windowRects[i];
            if (qAbs(r.center().x() - static_cast<int>(m_creechr->position().x())) > 600) continue;

            m_lastDragReactMs = now;
            m_creechr->setFacingRight(r.center().x() >= m_creechr->position().x());
            static const QStringList kDragLines = {
                QStringLiteral("HEY"),
                QStringLiteral("wait"),
                QStringLiteral("come back"),
                QStringLiteral("oh no you dont"),
                QStringLiteral("WHERE"),
                QStringLiteral("rude"),
                QStringLiteral("stop"),
            };
            m_creechr->speakRandom(kDragLines, 1600);
            break;
        }
    }

    // mouse-hover noticing: when the cursor gets within 100 px of
    // creechr while he's just walking or idling, fire a one-shot
    // reaction (speech bubble + flip to face the cursor). 5-second
    // cooldown so wiggling the mouse over him doesnt spam reactions.
    // gated on safe states so we dont interrupt heists / climbs / gnaw.
    if (m_creechr && (now - m_lastNoticedMs) > 5000) {
        const QString stateName = m_creechr->stateMachine().currentName();
        const bool safeState = (stateName == QLatin1String("idle")
                              || stateName == QLatin1String("walk"));
        if (safeState) {
            const QPointF cp = m_creechr->position();
            const QPoint  mp = g_cachedWorld.cursorPos;
            // distance from cursor to creechrs sprite center
            const int cx = static_cast<int>(cp.x()) + 24;
            const int cy = static_cast<int>(cp.y()) + 24;
            const int dx = mp.x() - cx;
            const int dy = mp.y() - cy;
            const int distSq = dx * dx + dy * dy;
            if (distSq < 100 * 100) {
                m_lastNoticedMs = now;
                m_creechr->setFacingRight(dx >= 0);
                static const QStringList kNoticedLines = {
                    QStringLiteral("hi"),
                    QStringLiteral("back off"),
                    QStringLiteral("..."),
                    QStringLiteral("what"),
                    QStringLiteral("excuse me"),
                    QStringLiteral("oh hello"),
                    QStringLiteral("dont"),
                    QStringLiteral("personal space"),
                    QStringLiteral("rude"),
                    QStringLiteral("yes?"),
                };
                m_creechr->speakRandom(kNoticedLines, 1800);
            }
        }
    }

    // physics + animator + render — all every tick now so movement is
    // visibly continuous instead of hopping in 100ms chunks.
    m_creechr->tickLogic(dt, g_cachedWorld);
    m_creechr->tickRender(dt);
    m_overlay->update();
}
