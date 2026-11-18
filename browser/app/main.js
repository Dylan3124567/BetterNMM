// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (C) 2026 BetterNMM Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// BetterNMM's Nexus Mods download window. Nexus Mods lets free accounts download only through the "Slow download" button
// on its website, clicked by the person. This window shows that page for each queued file, one after another: the next
// pages load while the person looks at the current one, a click switches to the next at once, and the clicked page
// finishes on its own and hands its download link to BetterNMM, which downloads and installs the file. The window never
// clicks anything itself. It runs only while there is something to click and exits when the queue is done.
//
// It runs in Electron (Chromium), renamed betternmm-browser.exe, with Electron's FFmpeg that has no patented codecs:
// H.264 and HEVC video are decoded by the graphics card instead.
//
// Lines on stdin and stdout, UTF-8, fields separated by tabs:
//   BetterNMM -> window:  item <id> <download page url> <title>
//   window -> BetterNMM:  queued <id> | shown <id> | login <id> | clicked <id> | download <id> <url> <file name>
//                         | nxm <id> <url> | skipped <id> | timeout <id> | cancelled <id> | timing <what> <milliseconds>

'use strict';

const { app, BaseWindow, WebContentsView, Menu, dialog, ipcMain, nativeImage, session } = require('electron');
const fs = require('fs');
const net = require('net');
const path = require('path');

// How many pages load ahead of the one shown. A Nexus Mods download page needs 1.1 to 1.9 s until its button is there
// and, with its ads, a few hundred MB. Three pages ahead let a burst of fast clicks land on ready pages every time, so
// that is the start (while the first page is read) and the pace whenever clicks come quickly. Someone taking more than
// SLOW_CLICK_MS per page gets one page ahead, which keeps up with them, and the others are unloaded to free their memory.
const FAST_PRELOAD = 3;
const SLOW_PRELOAD = 1;
const FAST_CLICK_MS = 2000;
const SLOW_CLICK_MS = 4000;
// A clicked page that has not started its download by then is reported, so BetterNMM can say so instead of waiting
const CLICK_TIMEOUT_MS = 60000;
// How long a click or key press lets a page open a window, as Chromium's own user activation does
const GESTURE_MS = 5000;
const HEADER_HEIGHT = 40;
const TITLE = 'BetterNMM — Nexus Mods downloads';
// Page zoom steps, Chrome's
const ZOOM_STEPS = [0.25, 0.33, 0.5, 0.67, 0.75, 0.8, 0.9, 1, 1.1, 1.25, 1.5, 1.75, 2, 2.5, 3, 4, 5];

function argument(name) {
    const prefix = '--' + name + '=';
    let value = '';
    for (const arg of process.argv.slice(1)) {
        if (arg.startsWith(prefix)) {
            value = arg.slice(prefix.length);
        }
    }
    return value;
}

const profile = argument('profile');
const iconPath = argument('icon');
// Tests run the window off screen and without taking the focus, so a person using the computer meanwhile sees nothing
const offscreen = process.env.BETTERNMM_BROWSER_OFFSCREEN === '1';

function logError(text) {
    try {
        fs.appendFileSync(path.join(profile, 'browser.log'), new Date().toISOString() + ' ' + text + '\n');
    } catch (error) {
        process.stderr.write(text + ' (browser.log: ' + error.message + ')\n');
    }
}

// --- page zoom, kept between runs ---------------------------------------------------------------------------------

const zoomFile = () => path.join(profile, 'zoom.json');

function loadZoom() {
    let factor;
    try {
        factor = Number(JSON.parse(fs.readFileSync(zoomFile(), 'utf8')).factor);
    } catch (error) {
        if (error.code !== 'ENOENT') { // no file: never zoomed
            logError('zoom.json: ' + error.message);
        }
        return 1;
    }
    if (!ZOOM_STEPS.includes(factor)) {
        logError('zoom.json: ' + factor + ' is not a zoom step');
        return 1;
    }
    return factor;
}

let zoom = 1;

/// Zooms every page by @p step steps (0: back to 100 %) and remembers it
function changeZoom(step) {
    const index = ZOOM_STEPS.indexOf(zoom);
    const next = step === 0 ? 1 : ZOOM_STEPS[Math.min(ZOOM_STEPS.length - 1, Math.max(0, index + step))];
    if (next === zoom) {
        return;
    }
    zoom = next;
    for (const item of items) {
        if (item.view) {
            item.view.webContents.setZoomFactor(zoom);
        }
    }
    try {
        fs.writeFileSync(zoomFile(), JSON.stringify({ factor: zoom }));
    } catch (error) {
        logError('zoom.json: ' + error.message);
    }
    updateHeader();
}

let outputBroken = false;

/// One line to BetterNMM, written at once so nothing is lost when the window quits right after
function send(...fields) {
    const line = fields.map(field => String(field).replace(/[\t\n\r]/g, ' ')).join('\t') + '\n';
    try {
        fs.writeSync(1, line);
    } catch (error) {
        // BetterNMM is gone; its input closing ends this window too
        if (!outputBroken) {
            outputBroken = true;
            logError('output: ' + error.message);
        }
    }
}

// --- the queue ----------------------------------------------------------------------------------------------------

const State = { Waiting: 0, Loading: 1, Ready: 2, Clicked: 3, Done: 4 };

const items = []; // in queue order; items are never removed
const gestureAt = new Map(); // webContents id -> time of its last click or key press
let shown = null;
let win = null;
let header = null;
let headerLoaded = false;
let closing = false; // the window was closed: clicked pages finish, nothing else is shown
let timerRunning = false;
let lookahead = FAST_PRELOAD; // pages loaded ahead, following how fast the person clicks
let lastClickAt = 0;

const now = () => performance.now();
const pending = item => item.state === State.Waiting || item.state === State.Loading || item.state === State.Ready;
const itemFor = contents => items.find(item => item.view && item.view.webContents === contents) || null;
const itemById = id => items.find(item => item.id === id) || null;

const pagePreferences = {
    preload: path.join(__dirname, 'page.js'),
    sandbox: true,
    contextIsolation: true,
    nodeIntegration: false,
};

function pageBounds() {
    const [width, height] = win.getContentSize();
    return { x: 0, y: HEADER_HEIGHT, width, height: Math.max(0, height - HEADER_HEIGHT) };
}

function layout() {
    const [width] = win.getContentSize();
    header.setBounds({ x: 0, y: 0, width, height: HEADER_HEIGHT });
    const bounds = pageBounds();
    for (const item of items) {
        if (item.view) {
            item.view.setBounds(bounds);
        }
    }
}

function createPage(item, visible) {
    const view = new WebContentsView({ webPreferences: pagePreferences });
    view.setBackgroundColor('#ffffff');
    view.setVisible(visible);
    view.setBounds(pageBounds());
    win.contentView.addChildView(view);
    item.view = view;
    item.state = State.Loading;
    item.speedSettled = false;
    item.createdAt = now();
    // Chromium keeps a zoom per site while the window runs; set again on each page, it also holds after a restart
    view.webContents.on('did-navigate', () => view.webContents.setZoomFactor(zoom));
    view.webContents.on('did-fail-load', (event, code, description, url, isMainFrame) => {
        if (isMainFrame) {
            item.reloading = false;
        }
    });
    view.webContents.loadURL(item.url).catch(error => {
        // a page that turns into a download or a Mod Manager link ends its own load, and so does closing it early
        if (error.code !== 'ERR_ABORTED' && item.view === view) {
            logError('page ' + item.url + ': ' + error.message);
        }
    });
}

function destroyPage(item) {
    if (!item.view) {
        return;
    }
    if (win) {
        win.contentView.removeChildView(item.view);
    }
    item.view.webContents.close();
    item.view = null;
}

/// Ends an item later, never inside an event of its own page
function finishLater(id, ...report) {
    const item = itemById(id);
    if (!item || item.state === State.Done) {
        return;
    }
    send(...report);
    item.state = State.Done;
    setImmediate(() => {
        if (shown === item) {
            item.view.setVisible(false);
            shown = null;
            showNext();
        }
        destroyPage(item);
        finishWhenIdle();
    });
}

function updateHeader() {
    if (!win) {
        return;
    }
    const total = items.length;
    const position = shown ? items.filter(item => !pending(item)).length + 1 : total;
    let line;
    if (shown && shown.loginNeeded) {
        line = 'Log in to Nexus Mods on this page first (“Log in” at the top). BetterNMM stays logged in for next time.';
    } else if (shown && shown.reloading) {
        line = 'File ' + position + ' of ' + total + ':  ' + shown.title + '   —   loading again for Nexus Mods’ 3 MB/s free speed…';
    } else if (shown && shown.needsChoice) {
        line = 'File ' + position + ' of ' + total + ':  ' + shown.title + '   —   over 500 MB: click “Standard download”' +
               ' (“Resumable download” cannot hand the file to BetterNMM)';
    } else if (shown) {
        line = 'File ' + position + ' of ' + total + ':  ' + shown.title + '   —   click “Slow download”';
    } else {
        line = 'Waiting for Nexus Mods to start the downloads you clicked…';
    }
    if (zoom !== 1) {
        line += '   ·   zoom ' + Math.round(zoom * 100) + ' % (Ctrl+0: 100 %)';
    }
    if (headerLoaded) {
        header.webContents.send('betternmm:status', line, shown !== null);
    }
    win.setTitle(shown ? TITLE + ' (' + position + '/' + total + ')' : TITLE);
}

/// Loads the pages after the shown one, once the shown one is ready (a page loaded before logging in would be useless)
/// and has its speed settled (pages loaded before Nexus Mods' ad check has a result would all be loaded twice)
function preload() {
    if (!shown || shown.state !== State.Ready || !shown.speedSettled || closing) {
        return;
    }
    let ahead = 0;
    let after = false;
    for (const item of items) {
        if (item === shown) {
            after = true;
            continue;
        }
        if (!after || !pending(item)) {
            continue;
        }
        if (ahead >= lookahead) {
            // ahead of a slow pace: unloaded until it is needed, its ads cost memory meanwhile
            if (item.view) {
                destroyPage(item);
                item.state = State.Waiting;
            }
            continue;
        }
        if (!item.view) {
            createPage(item, false);
        }
        ahead++;
    }
}

function showNext() {
    const started = now();
    const next = closing ? null : items.find(pending) || null;
    shown = next;
    if (next) {
        if (!next.view) {
            createPage(next, true);
        } else {
            next.view.setVisible(true);
        }
        next.view.webContents.focus();
        send('shown', next.id);
        send('timing', 'switch', Math.round(now() - started));
    }
    updateHeader();
    preload();
}

function checkClickedPages() {
    timerRunning = false;
    const time = now();
    let waiting = false;
    for (const item of items) {
        if (item.state !== State.Clicked) {
            continue;
        }
        if (time - item.clickedAt > CLICK_TIMEOUT_MS) {
            finishLater(item.id, 'timeout', item.id);
        } else {
            waiting = true;
        }
    }
    if (waiting) {
        timerRunning = true;
        setTimeout(checkClickedPages, 1000);
    }
}

function clicked(item) {
    if (item.state === State.Clicked || item.state === State.Done) {
        return;
    }
    item.state = State.Clicked;
    item.clickedAt = now();
    if (lastClickAt) {
        const interval = item.clickedAt - lastClickAt;
        lookahead = interval < FAST_CLICK_MS ? FAST_PRELOAD : interval > SLOW_CLICK_MS ? SLOW_PRELOAD : lookahead;
    }
    lastClickAt = item.clickedAt;
    send('clicked', item.id);
    if (item === shown) {
        // the page keeps running hidden until Nexus Mods starts its download
        item.view.setVisible(false);
        showNext();
    }
    if (!timerRunning) {
        timerRunning = true;
        setTimeout(checkClickedPages, 1000);
    }
}

/// Closes the window once nothing is left to click or to wait for
function finishWhenIdle() {
    const busy = items.some(item => item.state === State.Clicked || (!closing && pending(item)));
    if (!busy && win) {
        closing = true;
        win.close();
    }
}

// --- the window ---------------------------------------------------------------------------------------------------

function createWindow() {
    const icon = iconPath ? nativeImage.createFromPath(iconPath) : null;
    win = new BaseWindow({
        width: 1120,
        height: 860,
        title: TITLE,
        show: false,
        backgroundColor: '#ffffff',
        ...(icon && !icon.isEmpty() ? { icon } : {}),
        ...(offscreen ? { x: -20000, y: 0, skipTaskbar: true } : { center: true }),
    });
    header = new WebContentsView({ webPreferences: { preload: path.join(__dirname, 'header.js'), sandbox: true, contextIsolation: true } });
    win.contentView.addChildView(header);
    header.webContents.on('did-finish-load', () => {
        headerLoaded = true;
        updateHeader();
    });
    header.webContents.loadFile(path.join(__dirname, 'header.html')).catch(error => logError('header: ' + error.message));
    win.on('resize', layout);
    win.on('close', event => {
        if (!closing) {
            // closed by the person: whatever was not clicked is cancelled; clicked pages still finish, out of sight
            closing = true;
            for (const item of items) {
                if (pending(item)) {
                    send('cancelled', item.id);
                    item.state = State.Done;
                    destroyPage(item);
                }
            }
            shown = null;
        }
        if (items.some(item => item.state === State.Clicked)) {
            event.preventDefault();
            win.hide();
        }
    });
    win.on('closed', () => {
        for (const item of items) {
            if (item.view) {
                item.view.webContents.close();
                item.view = null;
            }
        }
        header.webContents.close();
        header = null;
        win = null;
    });
    layout();
    if (offscreen) {
        win.showInactive();
    } else {
        win.show();
    }
    showNext();
}

// only the page script sends this: pages themselves cannot reach it, and it runs in main frames only
ipcMain.on('betternmm:page', (event, what) => {
    const item = itemFor(event.sender);
    if (!item) {
        return;
    }
    if (what === 'document') {
        item.reloading = false;
        return;
    }
    if (item.reloading) {
        return; // from the document being replaced: a click there starts nothing
    }
    if (what === 'slowspeed' && pending(item) && !item.speedReload && !item.loginNeeded) {
        // made before Nexus Mods' ad check had a result (see page.js): the same page again gets the 3 MB/s free speed
        item.speedReload = true;
        item.reloading = true;
        item.state = State.Loading;
        item.createdAt = now();
        item.view.webContents.reload();
        if (item === shown) {
            updateHeader();
        }
    } else if (what === 'speedok' || what === 'slowspeed') {
        item.speedSettled = true;
        if (item === shown) {
            preload();
        }
    } else if (what === 'clicked') {
        clicked(item);
    } else if (what === 'ready' && item.state === State.Loading) {
        item.state = State.Ready;
        item.loginNeeded = false;
        send('timing', 'ready', Math.round(now() - item.createdAt));
        if (item === shown) {
            updateHeader();
            preload();
        }
    } else if (what === 'choose' && pending(item) && !item.needsChoice) {
        // a file over 500 MB: Nexus Mods asks how to download it, so the page stays until that is answered
        item.needsChoice = true;
        if (item === shown) {
            updateHeader();
        }
    } else if (what === 'loggedout' && !item.loginNeeded) {
        item.loginNeeded = true;
        send('login', item.id);
        if (item === shown) {
            updateHeader();
        }
    }
});

ipcMain.on('betternmm:skip', event => {
    if (header && event.sender === header.webContents && shown) {
        finishLater(shown.id, 'skipped', shown.id);
    }
});

const isNxm = url => /^nxm:/i.test(url);

// Every page, popups included: Mod Manager Download links go to BetterNMM, never to the program Windows has for them
app.on('web-contents-created', (event, contents) => {
    contents.on('will-frame-navigate', details => {
        if (!isNxm(details.url)) {
            return;
        }
        details.preventDefault();
        const item = itemFor(contents);
        if (item) {
            finishLater(item.id, 'nxm', item.id, details.url);
        }
    });
    // Page zoom as in a browser: Ctrl with the wheel, Ctrl and + - 0
    contents.on('zoom-changed', (event, direction) => changeZoom(direction === 'in' ? 1 : -1));
    contents.on('before-input-event', (event, input) => {
        if ((input.type !== 'keyDown' && input.type !== 'rawKeyDown') || !input.control || input.alt || input.meta) {
            return;
        }
        const step = { '=': 1, '+': 1, '-': -1, _: -1, 0: 0 }[input.key];
        if (step !== undefined) {
            event.preventDefault();
            changeZoom(step);
        }
    });
    contents.on('input-event', (event, input) => {
        if (input.type === 'mouseDown' || input.type === 'rawKeyDown' || input.type === 'keyDown' || input.type === 'touchEnd' || input.type === 'gestureTap') {
            gestureAt.set(contents.id, now());
        }
    });
    contents.setWindowOpenHandler(({ url }) => {
        if (isNxm(url)) {
            const item = itemFor(contents);
            if (item) {
                finishLater(item.id, 'nxm', item.id, url);
            }
            return { action: 'deny' };
        }
        // like Chrome: windows only from a click (logging in with another site needs them), one per click
        const at = gestureAt.get(contents.id);
        if (at === undefined || now() - at > GESTURE_MS) {
            return { action: 'deny' };
        }
        gestureAt.delete(contents.id);
        return {
            action: 'allow',
            overrideBrowserWindowOptions: { backgroundColor: '#ffffff', ...(iconPath ? { icon: iconPath } : {}), ...(offscreen ? { show: false } : {}) },
        };
    });
    contents.on('destroyed', () => gestureAt.delete(contents.id));
});

// --- input from BetterNMM -----------------------------------------------------------------------------------------

function addItem(id, url, title) {
    if (!id || !url || itemById(id) || closing) {
        return; // a closing window takes nothing new: BetterNMM starts another one for it
    }
    items.push({ id, url, title, state: State.Waiting, view: null, clickedAt: 0, createdAt: 0, loginNeeded: false, needsChoice: false,
                 speedReload: false, speedSettled: false, reloading: false });
    send('queued', id);
    if (!win) {
        createWindow();
    } else if (!shown) {
        showNext();
    } else {
        updateHeader();
        preload();
    }
}

function readInput() {
    let buffer = '';
    const ended = () => {
        // BetterNMM is gone or done: nothing more will come, so close as if the person had
        if (win && !closing) {
            win.close();
        } else if (!win) {
            app.quit();
        }
    };
    // Electron's process.stdin on Windows is a stand-in that ends at once; the pipe itself is file descriptor 0
    let input;
    try {
        input = new net.Socket({ fd: 0, readable: true, writable: false });
    } catch (error) {
        logError('input: ' + error.message);
        ended();
        return;
    }
    input.setEncoding('utf8');
    input.on('data', chunk => {
        buffer += chunk;
        for (let end = buffer.indexOf('\n'); end >= 0; end = buffer.indexOf('\n')) {
            let line = buffer.slice(0, end);
            buffer = buffer.slice(end + 1);
            if (line.endsWith('\r')) {
                line = line.slice(0, -1);
            }
            const fields = line.split('\t');
            if (fields.length >= 4 && fields[0] === 'item') {
                addItem(fields[1], fields[2], fields[3]);
            }
        }
    });
    input.on('end', ended);
    input.on('error', error => {
        logError('input: ' + error.message);
        ended();
    });
}

/// Session cookies normally end with the program. These are Nexus Mods' own, among them the login, so they get a date
/// and survive until the next download.
function keepNexusLogin(cookies) {
    cookies.on('changed', (event, cookie, cause, removed) => {
        if (removed || !cookie.session || !/(^|\.)nexusmods\.com$/i.test(cookie.domain)) {
            return;
        }
        const host = cookie.domain.replace(/^\./, '');
        const details = {
            url: (cookie.secure ? 'https://' : 'http://') + host + (cookie.path || '/'),
            name: cookie.name,
            value: cookie.value,
            path: cookie.path,
            secure: cookie.secure,
            httpOnly: cookie.httpOnly,
            sameSite: cookie.sameSite,
            expirationDate: Math.floor(Date.now() / 1000) + 400 * 24 * 3600,
        };
        if (!cookie.hostOnly) {
            details.domain = cookie.domain;
        }
        cookies.set(details).catch(error => logError('keeping cookie ' + cookie.name + ': ' + error.message));
    });
}

// --- application --------------------------------------------------------------------------------------------------

if (!profile) {
    // not BetterNMM's own data folder, which Electron would pick by the app's name
    app.setPath('userData', path.join(require('os').tmpdir(), 'betternmm-browser'));
    app.whenReady().then(() => {
        dialog.showMessageBoxSync({ type: 'info', title: 'BetterNMM', message: 'BetterNMM starts this window itself when you download from Nexus Mods.' });
        app.exit(2);
    });
} else {
    app.setPath('userData', profile);
    app.setPath('sessionData', profile);
    // With full site isolation every ad frame's site got a process of its own (150 processes, 3 GB within seconds);
    // isolating only Nexus Mods keeps the login away from the ads and the rest shares one process. Rendering stays on
    // the GPU, in its own process: software rendering looked like a virtual machine to Nexus Mods' Cloudflare check.
    app.commandLine.appendSwitch('process-per-site');
    app.commandLine.appendSwitch('disable-site-isolation-trials');
    app.commandLine.appendSwitch('isolate-origins', 'https://nexusmods.com,https://www.nexusmods.com,https://users.nexusmods.com');
    app.commandLine.appendSwitch('no-pings');
    // an off-screen window counts as covered and would draw nothing
    app.commandLine.appendSwitch('disable-features', 'SpareRendererForSitePerProcess' + (offscreen ? ',CalculateNativeWinOcclusion' : ''));
    // the cache keeps page assets for the next run, but never grows past this
    app.commandLine.appendSwitch('disk-cache-size', '67108864');
    app.commandLine.appendSwitch('lang', 'en-US');
    Menu.setApplicationMenu(null);
    app.on('window-all-closed', () => app.quit());
    app.whenReady().then(() => {
        zoom = loadZoom();
        const pages = session.defaultSession;
        pages.setSpellCheckerEnabled(false);
        // no prompts in this window, so every permission is answered as a person declining it; device names stay hidden
        pages.setPermissionRequestHandler((contents, permission, callback) => callback(false));
        pages.setPermissionCheckHandler(() => false);
        pages.on('will-download', (event, download, contents) => {
            const item = itemFor(contents);
            if (item) {
                const waited = item.clickedAt ? now() - item.clickedAt : 0;
                finishLater(item.id, 'download', item.id, download.getURL(), download.getFilename());
                send('timing', 'download', Math.round(waited));
            }
            event.preventDefault(); // BetterNMM downloads the file itself, so this window can close right away
        });
        keepNexusLogin(pages.cookies);
        readInput();
    });
}
