// creechr / main entry point.
//
// nothing real lives here. all the action is in CreechrApp. this file
// just exists because qt and msvc both want one.

#include "app/creechr_app.h"

int main(int argc, char* argv[])
{
    CreechrApp app(argc, argv);
    app.start();
    return app.exec();
}
