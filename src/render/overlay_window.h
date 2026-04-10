// OverlayWindow — the transparent canvas creechr lives on.
//
// the rules of this window are:
//   - frameless, always on top, no taskbar entry, no alt-tab entry
//   - completely click-through (mouse events fall through to whatever
//     is underneath, which is your actual desktop and your apps)
//   - covers the entire virtual desktop, recalculated whenever screens
//     change so you can plug a monitor in mid-run and not break things
//   - never steals focus, never appears in z-order above fullscreen apps
//
// if any of those become false you have a bug. probably the windows
// flags got reset by some qt update. check applyClickThroughFlags().
#pragma once

#include <QWidget>

class OverlayWindow : public QWidget
{
    Q_OBJECT

public:
    explicit OverlayWindow(QWidget* parent = nullptr);
    ~OverlayWindow() override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void recomputeGeometry();

private:
    // smash WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
    // WS_EX_NOACTIVATE onto the native HWND. qt sets some of these via
    // its window flags but i don't trust it not to drop them on a
    // restyle, so we set them again, manually, after every show().
    void applyClickThroughFlags();
};
