// the tray icon is the only ui creechr has. right click → pause/quit.
// that's the entire surface area. don't add a settings dialog. don't.
#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <memory>

class CreechrApp;
class QMenu;
class QAction;

class TrayIcon : public QObject
{
    Q_OBJECT

public:
    explicit TrayIcon(CreechrApp* app);
    ~TrayIcon() override;

    void show();

private slots:
    void onPauseToggled(bool checked);
    void onAppPauseChanged(bool paused);
    void onActivated(QSystemTrayIcon::ActivationReason reason);

private:
    CreechrApp* m_app;
    std::unique_ptr<QSystemTrayIcon> m_icon;
    std::unique_ptr<QMenu> m_menu;
    QAction* m_pauseAction = nullptr;
    QAction* m_quitAction = nullptr;
    QAction* m_releaseAction = nullptr;
    QAction* m_fireHeistAction = nullptr;
    QAction* m_openLogAction = nullptr;

    QIcon makePlaceholderIcon() const;
};
