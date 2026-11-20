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
#include <functional>
#include <optional>

#include "Result.h"
#include "game/Fomod.h"

/**
 * Compatibility patches in the FOMOD installers of installed mods: options, and files an installer adds on its own,
 * that the installer now recommends because of plugins the game has, but that are not installed. Mods installed later
 * than the mod with the installer are the usual reason.
 *
 * Which options a mod was installed with comes from what BetterNMM recorded at install time; for mods installed before
 * it recorded them, from the files in the mod's folder that match the archive's.
 */
namespace FomodPatches {

/// What the check needs of an installed mod
struct Mod {
    QString id;
    QString name;
    QString archive;             ///< the archive it was installed from
    QString folder;              ///< where the installer's destinations start (the mod's data folder)
    QList<QStringList> choices;  ///< options picked in its installer as {step, group, option} names; empty when not recorded
};

struct Patch {
    QString modId;
    QString modName;
    QString title;          ///< the option's name; empty for files the installer adds on its own
    QStringList files;      ///< plugins it adds, or else the files and folders it adds
    QStringList replaces;   ///< options of the same group it takes the place of
    std::optional<Fomod::Selection> option;
    int conditional = -1;   ///< the installer's conditional install, when it is not an option
};

struct Check {
    QList<Patch> patches;
    QStringList unchecked;  ///< mods whose archive is not in the downloads folder (anymore)
    QStringList errors;     ///< "Mod: reason" for archives or installers that could not be read
};

/// The state of a plugin file in the game, as FOMOD conditions ask for it
using FileStates = std::function<Fomod::FileState(const QString&)>;

/**
 * Reads the installer of every mod in @p mods, several archives at once. What an archive holds is kept in @p cacheDir
 * (nowhere when empty), so only new or changed archives are read again. Runs on worker threads: @p fileState is called
 * from several at once and must not touch live models.
 */
Check find(const QList<Mod>& mods, const FileStates& fileState, const QString& cacheDir);

struct Installed {
    QList<QStringList> choices;  ///< the mod's installer options now, for Mod::choices
    QStringList plugins;         ///< plugin files added, lower-case
};

/**
 * Copies the files of @p patches (all of @p mod) from its archive into its folder, the way its installer would have with
 * them picked: files of options they replace, and of conditional installs they turn off, are removed unless changed
 * since. A file is only replaced once its new version is completely unpacked. Runs on a worker thread.
 */
Result<Installed> install(const Mod& mod, const QList<Patch>& patches, const FileStates& fileState, const QString& cacheDir);

/// The options picked in an installer, of the steps it showed, as Mod::choices holds them
QList<QStringList> choiceNames(const Fomod::Module& module, const QList<Fomod::Selection>& selections, const Fomod::Context& base);

}  // namespace FomodPatches
