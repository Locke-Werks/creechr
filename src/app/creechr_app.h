// CreechrApp — QApplication subclass that owns everything that lives
// for the lifetime of the process. right now that's just the tray icon.
// later it'll be the overlay window, the creature, the world ticker,
// the heist executor, and a small pile of regrets.
#pragma once

#include <QApplication>
#include <memory>

class TrayIcon;

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

private:
    std::unique_ptr<TrayIcon> m_tray;
    bool m_paused = false;
};
