// creechr / main entry point.
//
// nothing real lives here. all the action is in CreechrApp. this file
// just exists because qt and msvc both want one.
//
// IMPORTANT: dpi awareness MUST be set before QApplication is built or
// qt will lock in its own choice and ignore us. that's what the first
// line of main is doing. don't move it.

#include "app/creechr_app.h"
#include "util/crash_guard.h"
#include "util/dpi.h"

int main(int argc, char* argv[])
{
    cr::setupDpiAwareness();
    // crash guard goes in before anything can possibly be stolen, so
    // there is no window (ha) where a crash strands a hidden window
    cr::crashguard::install();
    CreechrApp app(argc, argv);
    app.start();
    return app.exec();
}
