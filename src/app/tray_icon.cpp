#include "app/tray_icon.h"
#include "app/creechr_app.h"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>

TrayIcon::TrayIcon(CreechrApp* app)
    : QObject(app)
    , m_app(app)
{
    m_icon = std::make_unique<QSystemTrayIcon>(makePlaceholderIcon());
    m_icon->setToolTip(QStringLiteral("creechr — right click to pause or quit"));

    m_menu = std::make_unique<QMenu>();
    m_pauseAction = m_menu->addAction(QStringLiteral("pause"));
    m_pauseAction->setCheckable(true);

    m_menu->addSeparator();
    m_fireHeistAction = m_menu->addAction(QStringLiteral("fire a heist now"));
    m_releaseAction = m_menu->addAction(QStringLiteral("release everything he's stolen"));

    m_menu->addSeparator();
    m_openLogAction = m_menu->addAction(QStringLiteral("open log folder"));

    m_menu->addSeparator();
    m_quitAction = m_menu->addAction(QStringLiteral("quit"));

    connect(m_pauseAction,    &QAction::toggled,   this, &TrayIcon::onPauseToggled);
    connect(m_fireHeistAction,&QAction::triggered, m_app, &CreechrApp::fireHeistNow);
    connect(m_releaseAction,  &QAction::triggered, m_app, &CreechrApp::releaseEverything);
    connect(m_openLogAction,  &QAction::triggered, m_app, &CreechrApp::openLogFolder);
    connect(m_quitAction,     &QAction::triggered, m_app, &CreechrApp::quitGracefully);
    connect(m_app, &CreechrApp::pauseChanged, this, &TrayIcon::onAppPauseChanged);
    connect(m_icon.get(), &QSystemTrayIcon::activated, this, &TrayIcon::onActivated);

    m_icon->setContextMenu(m_menu.get());
}

TrayIcon::~TrayIcon() = default;

void TrayIcon::show()
{
    m_icon->show();
}

void TrayIcon::onPauseToggled(bool checked)
{
    m_app->setPaused(checked);
    m_pauseAction->setText(checked ? QStringLiteral("resume")
                                   : QStringLiteral("pause"));
}

void TrayIcon::onAppPauseChanged(bool paused)
{
    // keep the menu checkbox in sync if pause was toggled from elsewhere
    if (m_pauseAction && m_pauseAction->isChecked() != paused) {
        QSignalBlocker block(m_pauseAction);
        m_pauseAction->setChecked(paused);
        m_pauseAction->setText(paused ? QStringLiteral("resume")
                                      : QStringLiteral("pause"));
    }
}

void TrayIcon::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    // a left click on the tray icon is a no-op for now. someday this
    // might summon a settings panel. that day is not today.
    Q_UNUSED(reason);
}

QIcon TrayIcon::makePlaceholderIcon() const
{
    // real .ico file is a v0.2-or-later problem. this draws a 16x16
    // angry magenta square so the tray has SOMETHING to show without
    // me having to commit a binary asset right now.
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(1, 1, 14, 14, QColor(220, 50, 140));
        p.setPen(QColor(40, 0, 30));
        p.drawRect(1, 1, 13, 13);
        // two pixel "eyes" because i couldnt help myself
        p.fillRect(5, 6, 2, 2, QColor(255, 255, 255));
        p.fillRect(9, 6, 2, 2, QColor(255, 255, 255));
    }
    return QIcon(pm);
}
