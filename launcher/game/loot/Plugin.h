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

#include <QByteArray>
#include <QHash>
#include <QStringList>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "Condition.h"
#include "Result.h"

namespace Loot {

/// What a plugin's header record (TES4) says
struct PluginHeader {
    quint32 flags = 0;
    QStringList masters;
    std::optional<QString> description;
};

/// Reads only the header record, as esplugin's header-only parse
Result<PluginHeader> readPluginHeader(GameType game, const QString& path);
/// esplugin's is_master_file: the master flag, or for Skyrim SE, Fallout 4 and Starfield also the .esm/.esl extension
bool isMasterFile(GameType game, const QString& path, quint32 flags);

/// Files an archive holds: lower-case folder -> lower-case file names, '\\' separated, as raw bytes like libloot
using ArchiveAssets = std::map<QByteArray, std::set<QByteArray>>;

/// The assets of Bethesda archives (BSA 103/104/105, BA2); archives that can not be read are skipped
ArchiveAssets assetsInArchives(const QStringList& archivePaths);
bool doAssetsOverlap(const ArchiveAssets& a, const ArchiveAssets& b);

/// A record a plugin has, resolved to the plugin that first defines it
struct RecordId {
    quint32 objectIndex = 0;
    quint64 definedBy = 0;  ///< hash of the lower-case name of the plugin that defines the record
    bool overridden = false;
    auto operator<=>(const RecordId& other) const
    {
        return objectIndex != other.objectIndex ? objectIndex <=> other.objectIndex : definedBy <=> other.definedBy;
    }
    bool operator==(const RecordId& other) const { return objectIndex == other.objectIndex && definedBy == other.definedBy; }
};

/// A plugin loaded for sorting: libloot's Plugin with esplugin's data
struct Plugin {
    QString name;  ///< file name, without ".ghost"
    QString path;
    PluginHeader header;
    bool master = false;
    bool blueprint = false;  ///< Starfield's blueprint flag
    std::optional<QString> version;  ///< found in the description
    std::vector<RecordId> records;   ///< sorted
    int overrideCount = 0;
    ArchiveAssets assets;
    int assetCount = 0;

    bool recordsOverlap(const Plugin& other) const;
};

/**
 * Loads plugins for sorting, in parallel: headers, every record's FormID resolved against the plugin's masters
 * (Starfield needs the masters' scale, so they are read together), and the assets of the archives each plugin loads.
 */
Result<std::vector<Plugin>> loadPlugins(GameType game, const QString& dataPath, const QStringList& pluginNames);

/// Version number found in a plugin description, as libloot extracts it
std::optional<QString> extractVersion(const QString& description);

}  // namespace Loot
