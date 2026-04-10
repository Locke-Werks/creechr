// background service worker for creechr companion. talks to the
// native messaging host (com.creechr.bridge) and dispatches messages
// to/from the active tab's content script.
//
// the host registers itself in HKCU\Software\Google\Chrome\NativeMessagingHosts
// pointing at creechr-bridge-host.json which points at creechr-bridge.exe.
// see install_extension_host.ps1 in the repo root.
//
// if the host isn't installed, this script just sits idle and never
// connects. the extension still loads, the popup still works, the only
// thing that breaks is "creechr can talk to webpages". which is the
// whole point. so. yeah. install the host.

let port = null;
let optedInTabs = new Set();

function ensurePort() {
    if (port) return port;
    try {
        port = chrome.runtime.connectNative('com.creechr.bridge');
    } catch (e) {
        console.warn('creechr: connectNative failed, no host registered?', e);
        port = null;
        return null;
    }
    port.onMessage.addListener(handleNativeMessage);
    port.onDisconnect.addListener(() => {
        const err = chrome.runtime.lastError;
        if (err) console.warn('creechr: native port disconnected:', err.message);
        port = null;
    });
    return port;
}

async function handleNativeMessage(msg) {
    // shape: { type: 'scan_targets' | 'steal' | 'restore', tabId?, targetId? }
    if (!msg || !msg.type) return;
    if (msg.type === 'scan_targets') {
        const tabId = msg.tabId ?? (await getActiveTabId());
        if (!optedInTabs.has(tabId)) {
            sendNative({ type: 'scan_result', tabId, items: [] });
            return;
        }
        const result = await chrome.scripting.executeScript({
            target: { tabId },
            func: contentScanTargets,
        });
        sendNative({ type: 'scan_result', tabId, items: result?.[0]?.result ?? [] });
    } else if (msg.type === 'steal') {
        const tabId = msg.tabId;
        if (!optedInTabs.has(tabId)) return;
        await chrome.scripting.executeScript({
            target: { tabId },
            func: contentSteal,
            args: [msg.targetId],
        });
        sendNative({ type: 'steal_ack', tabId, targetId: msg.targetId });
    } else if (msg.type === 'restore') {
        const tabId = msg.tabId;
        if (!optedInTabs.has(tabId)) return;
        await chrome.scripting.executeScript({
            target: { tabId },
            func: contentRestore,
            args: [msg.targetId],
        });
        sendNative({ type: 'restore_ack', tabId, targetId: msg.targetId });
    }
}

function sendNative(obj) {
    const p = ensurePort();
    if (p) {
        try { p.postMessage(obj); } catch (e) { console.warn('creechr: postMessage failed', e); }
    }
}

async function getActiveTabId() {
    const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
    return tab?.id;
}

// === content-script functions, executed in page context ===

function contentScanTargets() {
    // walk the dom for stealable elements. give each one a stable id
    // for this scan + a screen-coord rect.
    const out = [];
    const elements = document.querySelectorAll('img, a[href], button, li');
    let i = 0;
    for (const el of elements) {
        const r = el.getBoundingClientRect();
        if (r.width < 16 || r.height < 12) continue;
        if (r.bottom < 0 || r.top > window.innerHeight) continue;
        const id = 'creechr-' + (i++);
        el.setAttribute('data-creechr-id', id);
        out.push({
            id,
            tag: el.tagName.toLowerCase(),
            rectCss: { x: r.left, y: r.top, w: r.width, h: r.height },
            screenX: r.left + window.screenX,
            screenY: r.top  + window.screenY,
            dpr: window.devicePixelRatio || 1,
            label: (el.alt || el.title || el.textContent || '').slice(0, 40),
        });
        if (out.length >= 64) break;
    }
    return out;
}

function contentSteal(targetId) {
    const el = document.querySelector('[data-creechr-id="' + targetId + '"]');
    if (!el) return;
    // remember the parent + next sibling so we can put it back exactly
    el.__creechrParent  = el.parentNode;
    el.__creechrSibling = el.nextSibling;
    if (el.parentNode) el.parentNode.removeChild(el);
    // stash on window so the restore can find it across executeScript calls
    window.__creechrStash = window.__creechrStash || {};
    window.__creechrStash[targetId] = el;
}

function contentRestore(targetId) {
    const stash = window.__creechrStash || {};
    const el = stash[targetId];
    if (!el) return;
    if (el.__creechrParent) {
        el.__creechrParent.insertBefore(el, el.__creechrSibling || null);
    }
    delete stash[targetId];
}

// === popup → background communication ===

chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
    if (msg?.type === 'opt_in_active_tab') {
        getActiveTabId().then((tabId) => {
            if (tabId == null) { sendResponse({ ok: false }); return; }
            optedInTabs.add(tabId);
            sendResponse({ ok: true, tabId });
        });
        return true; // async
    }
    if (msg?.type === 'opt_out_active_tab') {
        getActiveTabId().then((tabId) => {
            if (tabId == null) { sendResponse({ ok: false }); return; }
            optedInTabs.delete(tabId);
            sendResponse({ ok: true, tabId });
        });
        return true;
    }
    if (msg?.type === 'is_opted_in') {
        getActiveTabId().then((tabId) => {
            sendResponse({ ok: true, optedIn: optedInTabs.has(tabId) });
        });
        return true;
    }
});

// clean up when a tab closes
chrome.tabs.onRemoved.addListener((tabId) => {
    optedInTabs.delete(tabId);
});

// keep the worker semi-alive by doing a no-op every minute. mv3 service
// workers go to sleep aggressively and we want the native port to live.
setInterval(() => {}, 25_000);
