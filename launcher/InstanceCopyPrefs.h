// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (C) 2022 Marcelo Hernandez <marcelohdez.inq@gmail.com>
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

#pragma once

/// What to carry over when copying a game instance
struct InstanceCopyPrefs {
    bool keepPlaytime = true;
    bool copySaves = true;     ///< the source instance's own saves (never the user's real saves unless the source is the Steam instance)
    bool copySettings = true;  ///< INIs and everything else in the instance's profile
    bool copyMods = true;
    bool linkModFiles = false;  ///< hard link mod files instead of copying them
};
