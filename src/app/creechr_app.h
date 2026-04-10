// CreechrApp — QApplication subclass that owns everything that lives
// for the lifetime of the process. right now that's just the tray icon.
// later it'll be the overlay window, the creature, the world ticker,
// the heist executor, and a small pile of regrets.
#pragma once

#include <QApplication>
#include <memory>

class TrayIcon;
class OverlayWindow;
class QTimer;
namespace cr {
class Creechr;
class SpriteAtlas;
class WindowEnumerator;
class FullscreenDetector;
class WindowTargetProvider;
class CursorTargetProvider;
class UiaTargetProvider;
class Hoard;
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

public slots:
    void setPaused(bool paused);
    void quitGracefully();

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
    std::unique_ptr<cr::WindowTargetProvider> m_winTargets;
    std::unique_ptr<cr::CursorTargetProvider> m_curTargets;
    std::unique_ptr<cr::UiaTargetProvider> m_uiaTargets;
    std::unique_ptr<cr::Hoard> m_hoard;
    qint64 m_lastHeistAttemptMs = 0;
    QTimer* m_tickTimer = nullptr;
    qint64 m_lastTickMs = 0;
    qint64 m_lastWorldRefreshMs = 0;
    bool m_paused = false;
};
