#include "app/tray_icon.h"
#include "app/creechr_app.h"
#include "app/settings.h"
#include "render/sprite_atlas.h"

#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>

TrayIcon::TrayIcon(CreechrApp* app, const cr::SpriteAtlas* atlas)
    : QObject(app)
    , m_app(app)
{
    m_icon = std::make_unique<QSystemTrayIcon>(makeCreatureIcon(atlas));
    m_icon->setToolTip(QStringLiteral("creechr — right click for the manual"));

    m_menu = std::make_unique<QMenu>();
    m_pauseAction = m_menu->addAction(QStringLiteral("pause"));
    m_pauseAction->setCheckable(true);

    m_menu->addSeparator();

    // mischief level: the one dial that matters. radio group, applies
    // live, saved immediately.
    QMenu* mischiefMenu = m_menu->addMenu(QStringLiteral("mischief"));
    auto* levelGroup = new QActionGroup(mischiefMenu);
    levelGroup->setExclusive(true);
    const auto addLevel = [&](const QString& label, cr::Settings::Mischief level) {
        QAction* a = mischiefMenu->addAction(label);
        a->setCheckable(true);
        a->setActionGroup(levelGroup);
        a->setChecked(m_app->settings().mischief == level);
        connect(a, &QAction::triggered, this, [this, level]() {
            m_app->settings().mischief = level;
            m_app->saveSettings();
        });
    };
    addLevel(QStringLiteral("calm (look, dont touch)"), cr::Settings::Mischief::Calm);
    addLevel(QStringLiteral("normal"),                  cr::Settings::Mischief::Normal);
    addLevel(QStringLiteral("gremlin (hide your calculator)"), cr::Settings::Mischief::Gremlin);

    // per-kind consent. unchecking one removes it from the roll table
    // on the next tick, no restart involved.
    QMenu* stealsMenu = m_menu->addMenu(QStringLiteral("things he may steal"));
    const auto addKind = [&](const QString& label, bool cr::Settings::* field,
                             bool available = true, const QString& whyNot = {}) {
        QAction* a = stealsMenu->addAction(label);
        a->setCheckable(true);
        a->setChecked(m_app->settings().*field);
        a->setEnabled(available);
        if (!available) a->setToolTip(whyNot);
        connect(a, &QAction::toggled, this, [this, field](bool on) {
            m_app->settings().*field = on;
            m_app->saveSettings();
        });
    };
    addKind(QStringLiteral("windows"),          &cr::Settings::stealWindows);
    addKind(QStringLiteral("the cursor"),       &cr::Settings::stealCursor);
    addKind(QStringLiteral("taskbar buttons"),  &cr::Settings::stealUia);
    addKind(QStringLiteral("browser bits"),     &cr::Settings::stealDom,
            m_app->extensionPipeOk(),
            QStringLiteral("pipe server didnt start; browser theft is off the table"));

    // politeness: he goes quiet when your mic or camera is live.
    // uncheck if your mic is always hot and you want crimes anyway.
    QAction* polite = m_menu->addAction(QStringLiteral("polite during calls"));
    polite->setCheckable(true);
    polite->setChecked(m_app->settings().calmWhenInCall);
    connect(polite, &QAction::toggled, this, [this](bool on) {
        m_app->settings().calmWhenInCall = on;
        m_app->saveSettings();
    });

    m_menu->addSeparator();
    m_fireHeistAction = m_menu->addAction(QStringLiteral("fire a heist now"));
    m_releaseAction = m_menu->addAction(QStringLiteral("release everything he's stolen"));

    m_menu->addSeparator();
    m_autostartAction = m_menu->addAction(QStringLiteral("start with windows"));
    m_autostartAction->setCheckable(true);
    // the registry is the truth for this checkbox, not the ini
    m_autostartAction->setChecked(m_app->autostartEnabled());
    connect(m_autostartAction, &QAction::toggled, this, [this](bool on) {
        m_app->setAutostart(on);
    });
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

QIcon TrayIcon::makeCreatureIcon(const cr::SpriteAtlas* atlas) const
{
    if (!atlas || atlas->pixmap().isNull()) {
        return makePlaceholderIcon();
    }
    // frame 0 of the atlas, whole body. comes straight off the sheet,
    // so CREECHR_COLOR and custom sprites carry into the tray for free.
    const QPixmap cell = atlas->pixmap().copy(0, 0, cr::kSpriteWidth, cr::kSpriteHeight);
    if (cell.isNull()) {
        return makePlaceholderIcon();
    }
    QIcon icon;
    for (const int sz : { 16, 24, 32, 48 }) {
        icon.addPixmap(cell.scaled(sz, sz, Qt::KeepAspectRatio,
                                   sz >= 24 ? Qt::SmoothTransformation
                                            : Qt::FastTransformation));
    }
    return icon;
}

QIcon TrayIcon::makePlaceholderIcon() const
{
    // the original 16x16 angry magenta square, kept as the fallback
    // for when the atlas isnt around to pose for its portrait
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
