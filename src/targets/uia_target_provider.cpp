#include "targets/uia_target_provider.h"
#include "util/logging.h"

#include <QGuiApplication>
#include <QRandomGenerator>
#include <QScreen>

#ifdef _WIN32
#  include <windows.h>
#  include <objbase.h>
#  include <UIAutomation.h>
#  pragma comment(lib, "ole32.lib")
#  pragma comment(lib, "uiautomationcore.lib")
#endif

namespace cr {

#ifdef _WIN32

UiaTargetProvider::UiaTargetProvider()
{
    // COINIT_APARTMENTTHREADED — UIA wants STA. caller (main thread) is
    // already STA via Qt's QGuiApplication, so this is fine.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
        m_comInitialized = (hr == S_OK || hr == S_FALSE);
    } else {
        LOG_WARN(QStringLiteral("UIA: CoInitializeEx failed (hr=0x%1)")
            .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')));
        return;
    }

    IUIAutomation* automation = nullptr;
    hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr,
                          CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                          reinterpret_cast<void**>(&automation));
    if (FAILED(hr) || !automation) {
        LOG_WARN(QStringLiteral("UIA: CoCreateInstance(CUIAutomation) failed"));
        return;
    }
    m_automation = automation;
    LOG_INFO(QStringLiteral("UIA: provider ready"));
}

UiaTargetProvider::~UiaTargetProvider()
{
    if (m_automation) {
        static_cast<IUIAutomation*>(m_automation)->Release();
        m_automation = nullptr;
    }
    if (m_comInitialized) {
        CoUninitialize();
    }
}

namespace {

QString bstrToQString(BSTR b)
{
    if (!b) return {};
    return QString::fromWCharArray(b, ::SysStringLen(b));
}

QString controlTypeName(int id)
{
    switch (id) {
    case UIA_ButtonControlTypeId:    return QStringLiteral("button");
    case UIA_HyperlinkControlTypeId: return QStringLiteral("hyperlink");
    case UIA_ImageControlTypeId:     return QStringLiteral("image");
    case UIA_MenuItemControlTypeId:  return QStringLiteral("menuitem");
    case UIA_ListItemControlTypeId:  return QStringLiteral("listitem");
    default: return QStringLiteral("ctype?");
    }
}

} // namespace

void UiaTargetProvider::scanFromRoot(void* rootPtr, void* condPtr, void* sourceHwndPtr,
                                      const QRect& virtualDesktop,
                                      QVector<UiaSnapshotItem>& out)
{
    auto* root      = static_cast<IUIAutomationElement*>(rootPtr);
    auto* finalCond = static_cast<IUIAutomationCondition*>(condPtr);
    HWND sourceHwnd = static_cast<HWND>(sourceHwndPtr);
    if (!root || !finalCond) return;

    IUIAutomationElementArray* found = nullptr;
    HRESULT hr = root->FindAll(TreeScope_Descendants, finalCond, &found);
    if (FAILED(hr) || !found) return;

    int count = 0;
    found->get_Length(&count);

    // per-monitor dpi for the source window. if GetDpiForWindow fails
    // or theres no source hwnd, fall back to the primary screen.
    qreal dpr = 1.0;
    if (sourceHwnd) {
        UINT dpi = GetDpiForWindow(sourceHwnd);
        if (dpi > 0) dpr = dpi / 96.0;
    }
    if (dpr <= 0.0) {
        QScreen* primary = QGuiApplication::primaryScreen();
        dpr = primary ? primary->devicePixelRatio() : 1.0;
    }

    for (int i = 0; i < count && i < 256; ++i) {
        IUIAutomationElement* el = nullptr;
        if (FAILED(found->GetElement(i, &el)) || !el) continue;

        RECT r{};
        if (FAILED(el->get_CurrentBoundingRectangle(&r))) {
            el->Release();
            continue;
        }
        if (r.right - r.left < 16 || r.bottom - r.top < 12) {
            el->Release();
            continue;
        }
        const QRect rect(
            QPoint(static_cast<int>(r.left   / dpr), static_cast<int>(r.top    / dpr)),
            QPoint(static_cast<int>(r.right  / dpr - 1), static_cast<int>(r.bottom / dpr - 1))
        );
        if (!virtualDesktop.intersects(rect)) {
            el->Release();
            continue;
        }

        BSTR name = nullptr;
        el->get_CurrentName(&name);
        int ctype = 0;
        el->get_CurrentControlType(&ctype);

        UiaSnapshotItem item;
        item.screenRect = rect;
        item.name = bstrToQString(name);
        item.controlType = controlTypeName(ctype);

        if (name) ::SysFreeString(name);

        out.push_back(std::move(item));
        el->Release();
    }
    found->Release();
}

QVector<UiaSnapshotItem> UiaTargetProvider::scan(const QRect& virtualDesktop)
{
    QVector<UiaSnapshotItem> out;
    if (!m_automation) return out;

    auto* automation = static_cast<IUIAutomation*>(m_automation);

    // build the OR-of-control-types AND not-offscreen condition once,
    // reuse it for both the foreground-window scan and the taskbar scan
    auto makeCtypeCond = [&](int id) -> IUIAutomationCondition* {
        VARIANT v; v.vt = VT_I4; v.lVal = id;
        IUIAutomationCondition* c = nullptr;
        automation->CreatePropertyCondition(UIA_ControlTypePropertyId, v, &c);
        return c;
    };

    IUIAutomationCondition* ctypes[5] = {
        makeCtypeCond(UIA_ButtonControlTypeId),
        makeCtypeCond(UIA_HyperlinkControlTypeId),
        makeCtypeCond(UIA_ImageControlTypeId),
        makeCtypeCond(UIA_MenuItemControlTypeId),
        makeCtypeCond(UIA_ListItemControlTypeId),
    };
    IUIAutomationCondition* ctypeOr = nullptr;
    {
        IUIAutomationCondition* a = nullptr;
        automation->CreateOrCondition(ctypes[0], ctypes[1], &a);
        IUIAutomationCondition* b = nullptr;
        if (a) automation->CreateOrCondition(a, ctypes[2], &b);
        if (a) a->Release();
        IUIAutomationCondition* c = nullptr;
        if (b) automation->CreateOrCondition(b, ctypes[3], &c);
        if (b) b->Release();
        if (c) automation->CreateOrCondition(c, ctypes[4], &ctypeOr);
        if (c) c->Release();
    }
    for (int i = 0; i < 5; ++i) if (ctypes[i]) ctypes[i]->Release();

    VARIANT vFalse; vFalse.vt = VT_BOOL; vFalse.boolVal = VARIANT_FALSE;
    IUIAutomationCondition* notOffscreen = nullptr;
    automation->CreatePropertyCondition(UIA_IsOffscreenPropertyId, vFalse, &notOffscreen);

    IUIAutomationCondition* finalCond = nullptr;
    if (ctypeOr && notOffscreen) {
        automation->CreateAndCondition(ctypeOr, notOffscreen, &finalCond);
    }
    if (ctypeOr) ctypeOr->Release();
    if (notOffscreen) notOffscreen->Release();

    if (!finalCond) return out;

    // pass 1: foreground window
    HWND fg = GetForegroundWindow();
    if (fg) {
        wchar_t cls[256] = {};
        GetClassNameW(fg, cls, 256);
        const QString cn = QString::fromWCharArray(cls);
        const bool isShellOrSelf = (cn == QLatin1String("Progman")
            || cn == QLatin1String("WorkerW")
            || cn.startsWith(QLatin1String("Qt6")));
        if (!isShellOrSelf) {
            IUIAutomationElement* root = nullptr;
            if (SUCCEEDED(automation->ElementFromHandle(fg, &root)) && root) {
                scanFromRoot(root, finalCond, fg, virtualDesktop, out);
                root->Release();
            }
        }
    }

    // pass 2: taskbar (Shell_TrayWnd) — gets us start menu icons,
    // pinned apps, the start button itself, the search box, system
    // tray, etc. spec §8.4 forbids touching system processes / secure
    // desktop / UAC, but explorer's tray window is regular user shell
    // and we never modify it (just BitBlt + the §6.2 deviation).
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (tray) {
        IUIAutomationElement* trayRoot = nullptr;
        if (SUCCEEDED(automation->ElementFromHandle(tray, &trayRoot)) && trayRoot) {
            const int beforeCount = out.size();
            scanFromRoot(trayRoot, finalCond, tray, virtualDesktop, out);
            const int added = out.size() - beforeCount;
            if (added > 0) {
                LOG_DEBUG(QStringLiteral("UIA: scanned taskbar, +%1 targets").arg(added));
            }
            trayRoot->Release();
        }
    }

    finalCond->Release();

    m_lastSnapshot = out;
    return out;
}

std::optional<HeistTarget> UiaTargetProvider::pickRandom(const QRect& virtualDesktop)
{
    auto items = scan(virtualDesktop);
    if (items.isEmpty()) return std::nullopt;
    const int idx = QRandomGenerator::global()->bounded(items.size());
    HeistTarget t;
    t.kind = TargetKind::UiaElement;
    t.screenRect = items[idx].screenRect;
    t.hwnd = nullptr; // we don't track the source hwnd as a heist field
    t.label = items[idx].controlType + QStringLiteral(":") + items[idx].name;
    return t;
}

#else // !_WIN32

UiaTargetProvider::UiaTargetProvider()  = default;
UiaTargetProvider::~UiaTargetProvider() = default;
QVector<UiaSnapshotItem> UiaTargetProvider::scan(const QRect&)            { return {}; }
std::optional<HeistTarget> UiaTargetProvider::pickRandom(const QRect&)    { return {}; }

#endif

} // namespace cr
