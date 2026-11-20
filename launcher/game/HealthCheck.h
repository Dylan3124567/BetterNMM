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
#include <QStringList>

#include "game/ModList.h"
#include "game/PluginList.h"
#include "nexus/NexusApi.h"

/// What keeps a mod setup from working: plugins whose masters do not load before them, and mods whose Nexus Mods
/// requirements are not installed or not enabled
namespace HealthCheck {

enum class Kind {
    MissingMaster,          ///< an active plugin needs a plugin that is not there
    InactiveMaster,         ///< an active plugin needs a plugin that is there but not active
    MasterLoadsLater,       ///< an active plugin loads before a master it needs
    MissingRequirement,     ///< an enabled mod needs a Nexus Mods mod that is not installed
    DisabledRequirement,    ///< an enabled mod needs a mod that is installed but disabled
    MissingScriptExtender,  ///< an enabled mod needs the script extender, which is not in the game folder
};

struct Problem {
    Kind kind = Kind::MissingMaster;
    QString plugin;        ///< plugin problems: the active plugin
    QString master;        ///< plugin problems: the master it needs
    QStringList neededBy;  ///< requirement problems: names of the enabled mods that need it
    QString requirement;   ///< requirement problems: the name Nexus Mods gives the required mod
    QStringList notes;     ///< requirement problems: what the mod pages say about it
    int nexusModId = 0;    ///< requirement problems: the required mod on Nexus Mods, 0 when it is not there
    QString game;          ///< requirement problems: the Nexus Mods game domain
    QString modId;         ///< DisabledRequirement: the installed mod to enable
};

/// The script extender of the game, for requirement problems
struct ScriptExtender {
    bool exists = false;     ///< the game has one
    int nexusModId = 0;      ///< its mod on Nexus Mods, 0 when it is not there
    bool installed = false;  ///< its loader is in the game folder
};

/// Active plugins (implicit ones count as active) whose masters are not there, not active or load after them;
/// @p plugins in load order
QList<Problem> pluginProblems(const QList<PluginEntry>& plugins);

/**
 * Requirements of the enabled mods of @p mods that are not met, from @p details (what Nexus Mods says about the
 * installed mods). Requirements of other games are left out, and so are those outside Nexus Mods, except the script
 * extender, whose loader @p extender says is there or not. One problem per required mod, with every mod that needs it.
 */
QList<Problem> requirementProblems(const QList<ModEntry>& mods, const QList<Nexus::ModDetails>& details, const ScriptExtender& extender);

/// What is wrong, as a sentence
QString describe(const Problem& problem);
/// What the Fix action does about it
QString remedy(const Problem& problem);

}  // namespace HealthCheck
