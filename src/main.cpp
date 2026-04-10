// creechr / main entry point.
//
// nothing real lives here yet. this commit is just the "does the build
// system actually work, does qt6 actually link, does the WIN32 subsystem
// actually let me show a window without a console behind it" smoke test.
// real overlay + tray + creature stuff lands in subsequent commits.
//
// if you ran this and got a 1-second placeholder window then i did my job.

#include <QApplication>
#include <QLabel>
#include <QTimer>
#include <Qt>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("creechr"));
    app.setApplicationVersion(QStringLiteral("0.0.1"));
    app.setOrganizationName(QStringLiteral("creechr"));
    app.setQuitOnLastWindowClosed(false);

    QLabel placeholder(QStringLiteral("creechr is alive (kind of)"));
    placeholder.setWindowTitle(QStringLiteral("creechr smoke test"));
    placeholder.setMargin(24);
    placeholder.show();

    // close myself after 1.5 seconds. this binary has nothing to say.
    QTimer::singleShot(1500, &app, &QCoreApplication::quit);

    return app.exec();
}
