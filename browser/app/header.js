// SPDX-License-Identifier: GPL-3.0-only
// The download window's header: which file is shown, and the Skip button
'use strict';

const { ipcRenderer } = require('electron');

ipcRenderer.on('betternmm:status', (event, line, canSkip) => {
    document.getElementById('status').textContent = line;
    document.getElementById('skip').hidden = !canSkip;
});
window.addEventListener('DOMContentLoaded', () => {
    document.getElementById('skip').addEventListener('click', () => ipcRenderer.send('betternmm:skip'));
});
