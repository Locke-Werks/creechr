// CreechrApp — QApplication subclass that owns everything that lives
// for the lifetime of the process. right now that's just the tray icon.
// later it'll be the overlay window, the creature, the world ticker,
// the heist executor, and a small pile of regrets.
#pragma once

#include "app/settings.h"

#include <QApplication>
#include <QHash>
#include <QString>
#include <memory>

class TrayIcon;
class OverlayWindow;
class QTimer;
namespace cr {
class Creechr;
class SpriteAtlas;
class WindowEnumerator;
class FullscreenDetector;
class BusyDetector;
class WindowTargetProvider;
class CursorTargetProvider;
class UiaTargetProvider;
class ExtensionTargetProvider;
class Hoard;
class ExtensionPipeServer;
}

class CreechrApp : public QApplication
{
    Q_OBJECT

public:
    CreechrApp(int& argc, char** argv);
    ~CreechrApp() override;

    // construct and show all the runtime crap. call this once after
    // CreechrApp itself is constructed but before exec().
    void start();

    // global pause toggle. nothing actually responds to this yet but the
    // tray menu wires into it so future code can hook the signal.
    bool isPaused() const { return m_paused; }

    // live settings. the tray mutates fields directly and calls
    // saveSettings(); everything reads them fresh each tick, so
    // changes apply without ceremony.
    cr::Settings& settings() { return m_settings; }
    void saveSettings() { m_settings.save(); }

    // start-with-windows via the HKCU Run key. the registry is the
    // truth here, not the ini: the checkbox reflects whats actually
    // going to happen at logon.
    bool autostartEnabled() const;
    void setAutostart(bool on);

    // whether the extension pipe server came up (the browser toggle
    // greys out when it didnt)
    bool extensionPipeOk() const { return m_extPipeOk; }

public slots:
    void setPaused(bool paused);
    void quitGracefully();
    // tray-menu actions:
    void releaseEverything();   // manually run hoard.restoreAll()
    void openLogFolder();       // shell-open the log directory
    void fireHeistNow();        // queue an immediate heist attempt

signals:
    void pauseChanged(bool paused);

private slots:
    void onTick();

private:
    std::unique_ptr<TrayIcon> m_tray;
    std::unique_ptr<OverlayWindow> m_overlay;
    std::unique_ptr<cr::SpriteAtlas> m_atlas;
    std::unique_ptr<cr::Creechr> m_creechr;
    std::unique_ptr<cr::WindowEnumerator> m_windows;
    std::unique_ptr<cr::FullscreenDetector> m_fullscreen;
    std::unique_ptr<cr::BusyDetector> m_busy;
    qint64 m_lastBusyPollMs = 0;
    // fullscreen detected but the user opted out of hiding: treat as
    // "present but polite" instead of "gone"
    bool m_fullscreenAsBusy = false;
    bool m_lastUserBusy = false;
    std::unique_ptr<cr::WindowTargetProvider> m_winTargets;
    std::unique_ptr<cr::CursorTargetProvider> m_curTargets;
    std::unique_ptr<cr::UiaTargetProvider> m_uiaTargets;
    std::unique_ptr<cr::ExtensionPipeServer> m_extPipe;
    std::unique_ptr<cr::ExtensionTargetProvider> m_extTargets;
    std::unique_ptr<cr::Hoard> m_hoard;
    qint64 m_lastHeistAttemptMs = 0;
    qint64 m_lastNoticedMs = 0;
    qint64 m_lastDragReactMs = 0;
    qint64 m_lastScareMs = 0;      // post-scare cooldown timestamp
    qint64 m_lastScaryScanMs = 0;  // scan rate limiter, updated every scan
    // pid -> scary exe name, empty string = benign. saves the
    // OpenProcess + QueryFullProcessImageName pair per window per
    // scan. pids recycle, so the cache gets nuked when it grows past
    // silly and one scan rebuilds it.
    QHash<quint32, QString> m_scaryPidCache;
    QTimer* m_tickTimer = nullptr;
    qint64 m_lastTickMs = 0;
    qint64 m_lastWorldRefreshMs = 0;
    qint64 m_calmSinceMs = 0;
    bool m_paused = false;
    bool m_extPipeOk = false;
    cr::Settings m_settings;
};
