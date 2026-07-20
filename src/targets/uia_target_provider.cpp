#include "targets/uia_target_provider.h"
#include "util/logging.h"

#include <QDateTime>
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

UiaTargetProvider::UiaTargetProvider()
{
#ifdef _WIN32
    m_worker = std::thread([this] { workerMain(); });
#endif
}

UiaTargetProvider::~UiaTargetProvider()
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_quit = true;
    }
    m_cv.notify_one();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void UiaTargetProvider::requestScan(const QRect& virtualDesktop)
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_pendingVd = virtualDesktop;
        m_scanRequested = true;
    }
    m_cv.notify_one();
}

std::optional<HeistTarget> UiaTargetProvider::pickRandom(const QRect& virtualDesktop)
{
    QVector<UiaSnapshotItem> items;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_snapshotMs != 0 && now - m_snapshotMs < 15000) {
            items = m_snapshot;
        }
    }
    // keep it warm either way: interest now predicts interest soon
    requestScan(virtualDesktop);

    if (items.isEmpty()) {
        LOG_INFO(QStringLiteral("uia: snapshot cold, warming for next time"));
        return std::nullopt;
    }
    const int idx = QRandomGenerator::global()->bounded(items.size());
    HeistTarget t;
    t.kind = TargetKind::UiaElement;
    t.screenRect = items[idx].screenRect;
    t.hwnd = nullptr; // we don't track the source hwnd as a heist field
    t.label = items[idx].controlType + QStringLiteral(":") + items[idx].name;
    return t;
}

#ifdef _WIN32

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

void UiaTargetProvider::workerMain()
{
    // MTA on purpose: this thread has no message pump, and a pumpless
    // STA deadlocks the first cross-process UIA call. MTA is the
    // documented mode for exactly this shape of client.
    const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hrInit)) {
        LOG_WARN(QStringLiteral("UIA worker: CoInitializeEx failed (hr=0x%1)")
            .arg(static_cast<quint32>(hrInit), 8, 16, QLatin1Char('0')));
        return;
    }

    IUIAutomation* automation = nullptr;
    const HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr,
                                        CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                                        reinterpret_cast<void**>(&automation));
    if (FAILED(hr) || !automation) {
        LOG_WARN(QStringLiteral("UIA worker: CoCreateInstance(CUIAutomation) failed"));
        CoUninitialize();
        return;
    }
    LOG_INFO(QStringLiteral("UIA: provider ready (worker thread)"));

    for (;;) {
        QRect vd;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cv.wait(lk, [this] { return m_scanRequested || m_quit; });
            if (m_quit) break;
            m_scanRequested = false;
            vd = m_pendingVd;
        }
        QVector<UiaSnapshotItem> out = runScan(automation, vd);
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_snapshot = std::move(out);
            m_snapshotMs = QDateTime::currentMSecsSinceEpoch();
        }
    }

    automation->Release();
    CoUninitialize();
}

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

QVector<UiaSnapshotItem> UiaTargetProvider::runScan(void* automationPtr,
                                                    const QRect& virtualDesktop)
{
    QVector<UiaSnapshotItem> out;
    auto* automation = static_cast<IUIAutomation*>(automationPtr);
    if (!automation) return out;

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
    return out;
}

#else // !_WIN32

void UiaTargetProvider::workerMain() {}
QVector<UiaSnapshotItem> UiaTargetProvider::runScan(void*, const QRect&) { return {}; }
void UiaTargetProvider::scanFromRoot(void*, void*, void*, const QRect&, QVector<UiaSnapshotItem>&) {}

#endif

} // namespace cr
