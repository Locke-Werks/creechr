// tiny popup script. one button, opts the active tab in/out.

const btn    = document.getElementById('toggle');
const status = document.getElementById('status');

async function refresh() {
    const r = await chrome.runtime.sendMessage({ type: 'is_opted_in' });
    if (!r?.ok) {
        btn.textContent = 'something is broken';
        return;
    }
    btn.textContent = r.optedIn ? 'kick him out of this tab' : 'let him in';
    status.textContent = r.optedIn ? 'opted in' : 'opted out';
}

btn.addEventListener('click', async () => {
    const r0 = await chrome.runtime.sendMessage({ type: 'is_opted_in' });
    const next = !r0?.optedIn;
    await chrome.runtime.sendMessage({
        type: next ? 'opt_in_active_tab' : 'opt_out_active_tab',
    });
    refresh();
});

refresh();
