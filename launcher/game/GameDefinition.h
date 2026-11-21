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

/// How a game stores its plugin load order
enum class PluginFormat {
    None,       ///< no plugin management
    Asterisk,   ///< plugins.txt lists every plugin in order, active ones prefixed with '*' (Skyrim SE, Fallout 4, Starfield)
    Textfile,   ///< plugins.txt lists active plugins, loadorder.txt lists the full order (Skyrim)
    Timestamp,  ///< plugins.txt lists active plugins, order comes from file modification times (Oblivion, Fallout 3/NV)
};

/// Everything BetterNMM knows about a moddable game. Unknown games get an instance-local definition built from user input.
struct GameDefinition {
    QString domain;  ///< Nexus Mods game domain, e.g. "skyrimspecialedition"
    QString name;
    QList<int> steamAppIds;
    QString executable;      ///< relative to the game root, '/' separated
    QString scriptExtender;  ///< loader relative to the game root, empty if the game has none
    int scriptExtenderMod = 0;  ///< the script extender's mod id on Nexus Mods (same game), 0 if it is not there
    /// a co-op launcher that starts the game with its script extender (Skyrim Together, and forks of it), relative to
    /// the game root; empty if the game has none
    QString coopLauncher;
    QString coopLauncherName;
    QString coopLauncherArguments;  ///< $INST_* variables are expanded
    QString crashLogger;        ///< the usual crash logging mod's name, empty if the game has none
    int crashLoggerMod = 0;     ///< its mod id on Nexus Mods
    QString myGamesFolder;   ///< Documents/My Games/<folder>, empty if the game keeps its settings elsewhere
    QString appDataFolder;   ///< AppData/Local/<folder> holding plugins.txt
    QStringList iniFiles;    ///< INI files in the My Games folder
    /// Folders the game saves in besides My Games/<folder>/Saves, starting with {documents}, {appdata} (Roaming),
    /// {localappdata}, {savedgames} or {game}
    QStringList saveFolders;
    QString dataPath;        ///< folder loose mod files belong in, relative to the game root; empty = game root
    PluginFormat pluginFormat = PluginFormat::None;
    QStringList implicitPlugins;  ///< always loaded first, never listed in plugins.txt
    QString cccFile;              ///< Creation Club list in the game root
    int fullPluginSlots = 0;      ///< how many full plugins the game loads at most; 0 when not known
    int lightPluginSlots = 0;     ///< how many light (ESL-flagged) plugins besides; 0 when the game has none

    bool isValid() const { return !domain.isEmpty(); }
    bool hasPlugins() const { return pluginFormat != PluginFormat::None; }

    static PluginFormat pluginFormatFromString(const QString& value);
    static QString pluginFormatToString(PluginFormat format);
};

namespace GameRegistry {
/// Built-in definitions loaded from :/games/games.json
const QList<GameDefinition>& all();
/// Definition for a Nexus domain, or an invalid definition
GameDefinition byDomain(const QString& domain);
/// Definition for a Steam app id, or an invalid definition
GameDefinition bySteamAppId(int appId);
/// Parses a games.json document; exposed for tests
QList<GameDefinition> parse(const QByteArray& json, QString* error = nullptr);
}  // namespace GameRegistry
