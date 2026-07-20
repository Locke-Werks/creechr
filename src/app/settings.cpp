#include "app/settings.h"
#include "util/logging.h"

#include <QSettings>
#include <QString>

namespace cr {

namespace {

QString mischiefName(Settings::Mischief m)
{
    switch (m) {
    case Settings::Mischief::Calm:    return QStringLiteral("calm");
    case Settings::Mischief::Gremlin: return QStringLiteral("gremlin");
    case Settings::Mischief::Normal:  break;
    }
    return QStringLiteral("normal");
}

Settings::Mischief mischiefFromName(const QString& s)
{
    if (s == QLatin1String("calm"))    return Settings::Mischief::Calm;
    if (s == QLatin1String("gremlin")) return Settings::Mischief::Gremlin;
    return Settings::Mischief::Normal;
}

// IniFormat + UserScope + the org/app names resolves to
// %APPDATA%\creechr\creechr.ini. constructed in place at each use
// because QSettings is a QObject and refuses to be passed around.
#define CREECHR_SETTINGS_STORE(varname) \
    QSettings varname(QSettings::IniFormat, QSettings::UserScope, \
                      QStringLiteral("creechr"), QStringLiteral("creechr"))

} // namespace

void Settings::load()
{
    CREECHR_SETTINGS_STORE(s);
    mischief = mischiefFromName(s.value(QStringLiteral("mischief/level"),
                                        QStringLiteral("normal")).toString().toLower());
    stealWindows = s.value(QStringLiteral("targets/windows"), true).toBool();
    stealCursor  = s.value(QStringLiteral("targets/cursor"),  true).toBool();
    stealUia     = s.value(QStringLiteral("targets/taskbar"), true).toBool();
    stealDom     = s.value(QStringLiteral("targets/browser"), true).toBool();
    calmWhenInCall   = s.value(QStringLiteral("politeness/calm_when_in_call"), true).toBool();
    hideOnFullscreen = s.value(QStringLiteral("politeness/hide_on_fullscreen"), true).toBool();
    adaptiveTick = s.value(QStringLiteral("perf/adaptive_tick"), true).toBool();
    ghostMode    = s.value(QStringLiteral("interact/ghost_mode"), false).toBool();
    LOG_INFO(QStringLiteral("settings: loaded (mischief=%1) from %2")
        .arg(mischiefName(mischief), s.fileName()));
}

void Settings::save() const
{
    CREECHR_SETTINGS_STORE(s);
    s.setValue(QStringLiteral("mischief/level"), mischiefName(mischief));
    s.setValue(QStringLiteral("targets/windows"), stealWindows);
    s.setValue(QStringLiteral("targets/cursor"),  stealCursor);
    s.setValue(QStringLiteral("targets/taskbar"), stealUia);
    s.setValue(QStringLiteral("targets/browser"), stealDom);
    s.setValue(QStringLiteral("politeness/calm_when_in_call"), calmWhenInCall);
    s.setValue(QStringLiteral("politeness/hide_on_fullscreen"), hideOnFullscreen);
    s.setValue(QStringLiteral("perf/adaptive_tick"), adaptiveTick);
    s.setValue(QStringLiteral("interact/ghost_mode"), ghostMode);
}

int Settings::heistMeanIntervalMs() const
{
    switch (mischief) {
    case Mischief::Calm:    return 0;      // look, dont touch
    case Mischief::Gremlin: return 6000;
    case Mischief::Normal:  break;
    }
    return 15000;
}

int Settings::inputIdleGateMs() const
{
    return mischief == Mischief::Gremlin ? 1500 : 2500;
}

int Settings::heistCooldownMs() const
{
    return mischief == Mischief::Gremlin ? 1500 : 2000;
}

int Settings::stashWaitMinMs() const
{
    return mischief == Mischief::Gremlin ? 4000 : 6000;
}

int Settings::stashWaitRangeMs() const
{
    return mischief == Mischief::Gremlin ? 8000 : 10000;
}

} // namespace cr
