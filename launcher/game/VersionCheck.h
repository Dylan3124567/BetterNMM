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

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct GameDefinition;

/**
 * Mod files made for another version of the game: script extender plugins (DLLs) the installed script extender will
 * refuse, a script extender built for another game version, a missing Address Library, and plugins (.esp/.esm/.esl)
 * made for an older game. The DLL rules are those of the SKSE64, F4SE and SFSE release made for the game's version,
 * ported from their PluginManager.
 */
namespace VersionCheck {

enum class ScriptExtender { None, SKSE64, F4SE, SFSE };

struct Finding {
    QString file;    ///< '/' separated; relative to the data folder, except check() and checkScriptExtender: the game folder
    QString reason;  ///< what goes wrong and what to do, in plain words
};

/// MAKE_EXE_VERSION_EX of the script extenders: 8 bits major, 8 minor, 12 build, 4 store (0 Steam, 1 GOG, 2 Epic)
quint32 packVersion(int major, int minor, int build, int store = 0);
QString versionText(quint32 packed);

/// The script extender plugins in @p dataDir (SKSE/Plugins, F4SE/Plugins or SFSE/Plugins) that the script extender for
/// game version @p runtime will not load, and why. @p seVersion is the installed script extender (packed), 0 when unknown
QList<Finding> checkPlugins(ScriptExtender se, quint32 runtime, quint32 seVersion, const QString& dataDir);

/// The script extender in @p gameDir when it is built for another game version
QList<Finding> checkScriptExtender(ScriptExtender se, quint32 runtime, const QString& gameDir);

/// Plugins with a header version older than the game's (LOOT's minimum header versions): made for an older game
QList<Finding> checkPluginHeaders(const QString& gameDomain, const QString& dataDir, const QStringList& pluginNames);

/// All of the above for an installed game: the version is read from its executable
QList<Finding> check(const GameDefinition& game, const QString& gameDir, const QString& dataDir, const QStringList& pluginNames);

}  // namespace VersionCheck
