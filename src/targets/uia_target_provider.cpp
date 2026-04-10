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

QVector<UiaSnapshotItem> UiaTargetProvider::scan(const QRect& virtualDesktop)
{
    QVector<UiaSnapshotItem> out;
    if (!m_automation) return out;

    HWND fg = GetForegroundWindow();
    if (!fg) return out;

    // skip ourselves and the shell
    {
        wchar_t cls[256] = {};
        GetClassNameW(fg, cls, 256);
        const QString cn = QString::fromWCharArray(cls);
        if (cn == QLatin1String("Progman") || cn == QLatin1String("WorkerW")
            || cn.startsWith(QLatin1String("Qt6"))) {
            return out;
        }
    }

    auto* automation = static_cast<IUIAutomation*>(m_automation);

    IUIAutomationElement* root = nullptr;
    HRESULT hr = automation->ElementFromHandle(fg, &root);
    if (FAILED(hr) || !root) return out;

    // build a condition: ControlType IN { button, hyperlink, image, menuitem, listitem }
    // and IsOffscreen = false
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
    // chain CreateOrCondition for the 5 control types we care about.
    // CreateOrConditionFromArray exists too but it wants a SAFEARRAY
    // of IUnknowns and life is too short.
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

    // off-screen filter
    VARIANT vFalse; vFalse.vt = VT_BOOL; vFalse.boolVal = VARIANT_FALSE;
    IUIAutomationCondition* notOffscreen = nullptr;
    automation->CreatePropertyCondition(UIA_IsOffscreenPropertyId, vFalse, &notOffscreen);

    IUIAutomationCondition* finalCond = nullptr;
    if (ctypeOr && notOffscreen) {
        automation->CreateAndCondition(ctypeOr, notOffscreen, &finalCond);
    }
    if (ctypeOr) ctypeOr->Release();
    if (notOffscreen) notOffscreen->Release();

    if (!finalCond) {
        root->Release();
        return out;
    }

    IUIAutomationElementArray* found = nullptr;
    hr = root->FindAll(TreeScope_Descendants, finalCond, &found);
    finalCond->Release();
    root->Release();

    if (FAILED(hr) || !found) return out;

    int count = 0;
    found->get_Length(&count);

    QScreen* primary = QGuiApplication::primaryScreen();
    const qreal dpr = primary ? primary->devicePixelRatio() : 1.0;

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
