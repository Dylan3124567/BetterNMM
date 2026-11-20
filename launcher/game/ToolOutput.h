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

#include <QHash>
#include <QSet>
#include <QStringList>

#include "Result.h"
#include "game/Deployer.h"

/**
 * Files a tool (BodySlide, Nemesis, xEdit, ...) writes into the game folder, moved into a mod of their own: the game
 * folder is noted before the tool runs, and what is new or rewritten afterwards goes into the tool's output mod, where
 * it can be disabled, removed or ordered like any other mod.
 */
namespace ToolOutput {

struct Stamp {
    qint64 size = 0;
    qint64 modified = 0;  ///< ms since epoch, UTC
};

/// The game folder before a tool ran
struct Snapshot {
    QHash<QString, Stamp> files;  ///< by lower-case path relative to the game folder
    QSet<QString> dirs;           ///< lower-case relative paths
};

/// Reads the sizes and times of every file in @p gameDir; it opens none of them
Snapshot take(const QString& gameDir);

/// Files left where they are
struct Filter {
    QStringList excludePatterns;  ///< PatternMatcher patterns (logs, crash dumps)
    QStringList ownDirs;          ///< folders relative to the game folder that hold a tool's own state ('/' separated)
};

/// What a tool did in the game folder, as paths relative to it
struct Changes {
    QStringList created;           ///< new files no mod put there
    QStringList replaced;          ///< mod files the tool wrote anew: another file is at the path now
    QStringList changedInPlace;    ///< mod files the tool wrote into: their mod's own copy changed with them
    QStringList changedGameFiles;  ///< files no mod put there that the tool changed; they stay
    QHash<QString, QString> modOf; ///< changedInPlace path -> id of the mod it belongs to
    bool isEmpty() const { return created.isEmpty() && replaced.isEmpty() && changedInPlace.isEmpty(); }
};

/**
 * What changed in @p gameDir since @p before. @p manifest says which files mods put there; files of @p outputModId (the
 * tool's own output mod) written into in place are its output already and are left out.
 */
Changes find(const QString& gameDir, const Snapshot& before, const DeploymentManifest& manifest, const Filter& filter, const QString& outputModId);

/**
 * Moves the created and replaced files into @p modDir (replacing what it had at those paths) and copies the files
 * changed in place there; folders the tool created and left empty go. The game folder is right again after the next
 * deployment, which links the files from the mod.
 */
Result<> collect(const QString& gameDir, const Snapshot& before, const Changes& changes, const QString& modDir);

}  // namespace ToolOutput
