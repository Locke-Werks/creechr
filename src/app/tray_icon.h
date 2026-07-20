// the tray icon is the only ui creechr has. right click → everything.
// still no settings dialog; submenus are as far as i bend, and the
// header comment saying "don't add a settings dialog" remains binding
// precedent.
#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <memory>

class CreechrApp;
class QMenu;
class QAction;
namespace cr { class SpriteAtlas; }

class TrayIcon : public QObject
{
    Q_OBJECT

public:
    // atlas is borrowed for one frame blit at construction (the tray
    // icon is him now, not a magenta placeholder). nullptr falls back
    // to the placeholder.
    explicit TrayIcon(CreechrApp* app, const cr::SpriteAtlas* atlas = nullptr);
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
    QAction* m_autostartAction = nullptr;

    QIcon makeCreatureIcon(const cr::SpriteAtlas* atlas) const;
    QIcon makePlaceholderIcon() const;
};
