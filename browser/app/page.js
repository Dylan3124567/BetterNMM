// SPDX-License-Identifier: GPL-3.0-only
// The page script of BetterNMM's Nexus Mods download window. It runs in every queued page's document before the page's
// own scripts, in a world of its own that the page cannot see or reach.
//
// It watches for the person's own click on "Slow download" (isTrusted: real input, not a script) and reports when the
// button is there. Nexus Mods draws its download panel inside web components, so the search goes into open shadow roots
// and clicks are matched along composedPath(). Logged out, the panel shows the same button with "You need an account to
// download" beside it; a click then leads to the login page, so it does not count. For a file over 500 MB, "Slow
// download" opens Nexus Mods' dialog "Download this large file without losing progress" instead of downloading; that
// dialog's "Standard download" is then the click that counts ("Resumable download" saves through the browser's file
// access, which hands BetterNMM no link). The script never clicks anything.
//
// Nexus Mods gives free accounts 3 MB/s instead of 1.5 MB/s when its own ad check finds no ad blocker, and decides that
// when it makes the page, from the check's last result (its "ab" cookie, kept 5 minutes). The check runs while the page
// is still being read, before Nexus Mods' consent manager has answered, and keeps its result only with consent: so it
// mostly keeps nothing, and download pages, which are all this window opens, say 1.5 MB/s. Once the consent manager has
// answered, the same check runs again here: Nexus Mods' own code, with the page's own result. A page made before there
// was a result is then loaded again by the window. Without consent, or with an ad blocker, nothing changes.

'use strict';

const { ipcRenderer, webFrame } = require('electron');

const report = what => ipcRenderer.send('betternmm:page', what);
report('document'); // a new document: clicks on the one it replaces no longer count
const isButton = e => !!e && e.nodeType === 1 && (e.tagName === 'BUTTON' || e.tagName === 'A' || e.getAttribute('role') === 'button');
const isSlow = e =>
    !!e && e.nodeType === 1 && (e.id === 'slowDownloadButton' || (isButton(e) && /slow\s*download/i.test(e.textContent || '')));
const isStandard = e => isButton(e) && /^\s*standard download\s*$/i.test(e.textContent || '');
const deep = (root, test = isSlow) => {
    for (const e of root.querySelectorAll('*')) {
        if (test(e)) {
            return e;
        }
        if (e.shadowRoot) {
            const found = deep(e.shadowRoot, test);
            if (found) {
                return found;
            }
        }
    }
    return null;
};

/// Runs @p decide once the page has drawn twice after a click (14-33 ms), or after 100 ms if it draws nothing
const afterTwoFrames = decide => {
    let done = false;
    const once = () => {
        if (!done) {
            done = true;
            decide();
        }
    };
    requestAnimationFrame(() => requestAnimationFrame(once));
    setTimeout(once, 100);
};
const up = e => e.parentElement || (e.getRootNode && e.getRootNode().host) || null;
const loggedOut = button => {
    for (let e = button, i = 0; e && i < 12; e = up(e), i++) {
        if (/need an account to download/i.test((e.shadowRoot ? e.shadowRoot.textContent : '') + (e.textContent || ''))) {
            return true;
        }
    }
    return false;
};

document.addEventListener('click', event => {
    if (!event.isTrusted) {
        return;
    }
    const path = event.composedPath().slice(0, 10);
    if (path.some(isStandard)) {
        report('clicked');
        return;
    }
    const button = path.find(isSlow);
    if (!button || loggedOut(button)) {
        return;
    }
    // The page's own click handler runs after this one and opens a large file's dialog right away (Nexus Mods renders
    // it with React, before the next frame). The page has to stay visible meanwhile: hidden, it would not draw again.
    afterTwoFrames(() => report(deep(document, isStandard) ? 'choose' : 'clicked'));
}, true);

/// Runs in the page's own world: Nexus Mods' ad check again, once its consent manager has answered (see the top)
function adCheckAgain() {
    return new Promise(resolve => {
        const deadline = Date.now() + 4000; // Nexus Mods' consent manager answers within about a second
        const attempt = () => {
            const consent = window.NexusConsent && typeof window.NexusConsent.get === 'function' && window.NexusConsent.get();
            if (!consent || !consent.preferences) {
                if (Date.now() < deadline) {
                    setTimeout(attempt, 200);
                } else {
                    resolve('no consent');
                }
                return;
            }
            // The check, as the page has it: if (document.getElementById('<decoy>')) { <check>(false); } else { <check>(true); }
            // Both names change from page to page.
            for (const script of document.scripts) {
                const found = !script.src &&
                    /if\s*\(\s*document\.getElementById\(\s*'([\w-]+)'\s*\)\s*\)\s*\{\s*([A-Za-z_$][\w$]*)\(\s*false\s*\)/.exec(script.textContent);
                if (found && typeof window[found[2]] === 'function') {
                    window[found[2]](!document.getElementById(found[1]));
                    resolve('checked');
                    return;
                }
            }
            resolve('no check');
        };
        attempt();
    });
}

/// The result of Nexus Mods' ad check as it keeps it: "0" no ad blocker, while it lasts
const adCheckResult = () => {
    const cookie = document.cookie.split('; ').find(c => c.startsWith('ab='));
    const [status, until] = cookie ? decodeURIComponent(cookie.slice(3)).split('|') : [];
    return Number(until) * 1000 > Date.now() ? status : undefined;
};

/// Tells the window whether this page has its speed settled ('speedok') or is better loaded again ('slowspeed')
const checkSpeed = () => {
    const panel = document.querySelector('mod-file-download');
    if (!panel || panel.getAttribute('download-speed') !== 'reduced') {
        report('speedok');
        return;
    }
    webFrame.executeJavaScript('(' + adCheckAgain.toString() + ')()').then(
        outcome => report(outcome === 'checked' && adCheckResult() === '0' ? 'slowspeed' : 'speedok'),
        () => undefined); // rejected only when the page went away meanwhile: nothing left to tell
};

let tries = 0;
const look = () => {
    const button = document.getElementById('slowDownloadButton') || deep(document);
    if (button) {
        report(loggedOut(button) ? 'loggedout' : 'ready');
    } else if (++tries < 240) {
        setTimeout(look, 125);
    }
};
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => {
        look();
        checkSpeed();
    });
} else {
    look();
    checkSpeed();
}
