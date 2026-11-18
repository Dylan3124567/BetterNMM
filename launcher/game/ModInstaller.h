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

#include <QString>
#include <QStringList>

#include "Result.h"
#include "game/Fomod.h"
#include "game/GameDefinition.h"

/// Turns an extracted mod archive into a staging folder laid out relative to the game folder
namespace ModInstaller {

enum class Layout {
    GameRoot,    ///< the archive's files go into the game folder (script extenders, ENB, archives with a Data folder)
    DataFolder,  ///< the archive's files go into the game's data folder (plugins, meshes, textures...)
};

struct Analysis {
    QString contentRoot;  ///< folder inside the extracted archive holding the mod's files
    Layout layout = Layout::GameRoot;
    QString fomodRoot;    ///< folder holding the "fomod" folder, empty without one
    QString fomodConfig;  ///< fomod/ModuleConfig.xml, empty when the mod has no installer
    QString name;         ///< from fomod/info.xml
    QString version;
    QStringList skipped;  ///< top-level folders left out (script extender sources)
};

/**
 * Looks at an extracted archive. @p gameDir is used to tell real game folders from wrapper folders some archives put
 * everything in.
 */
Analysis analyze(const QString& extractedDir, const GameDefinition& game, const QString& gameDir);

/// Moves a mod without installer into @p stagingDir
Result<> stage(const Analysis& analysis, const GameDefinition& game, const QString& stagingDir);

/// Copies the files a FOMOD installer chose into @p stagingDir; FOMOD destinations are relative to the data folder
Result<> stageFomod(const QList<Fomod::FileItem>& files, const Analysis& analysis, const GameDefinition& game, const QString& stagingDir);

/// Finds @p relPath under @p root ignoring case, as FOMOD configurations rarely match the archive's case
QString resolvePath(const QString& root, const QString& relPath);

}  // namespace ModInstaller
