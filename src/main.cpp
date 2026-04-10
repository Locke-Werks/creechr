// creechr / main entry point.
//
// nothing real lives here. all the action is in CreechrApp. this file
// just exists because qt and msvc both want one.
//
// IMPORTANT: dpi awareness MUST be set before QApplication is built or
// qt will lock in its own choice and ignore us. that's what the first
// line of main is doing. don't move it.

#include "app/creechr_app.h"
#include "util/dpi.h"

int main(int argc, char* argv[])
{
    cr::setupDpiAwareness();
    CreechrApp app(argc, argv);
    app.start();
    return app.exec();
}
