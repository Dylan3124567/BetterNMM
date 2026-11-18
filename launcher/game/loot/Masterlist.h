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
#include <QRegularExpression>
#include <QStringList>
#include <optional>
#include <vector>

#include "Condition.h"
#include "Result.h"

namespace Loot {

/// A plugin named in an entry's "after" or "req" list, kept only when its condition and constraint hold
struct MetadataFile {
    QString name;
    std::optional<Condition> condition;
    std::optional<Condition> constraint;
};

struct PluginMetadata {
    std::optional<QString> group;
    std::vector<MetadataFile> after;
    std::vector<MetadataFile> requirements;
};

struct Group {
    QString name;
    QStringList after;
};

/**
 * LOOT's masterlist: plugin groups and per-plugin load order metadata. Entries whose name contains any of
 * ":\\*?|" are case-insensitive regular expressions matched against whole plugin names.
 */
class Masterlist {
   public:
    /// Parses a masterlist after putting @p prelude in place of its "prelude:" section, as libloot does
    static Result<Masterlist> load(const QString& masterlist, const QString& prelude);
    /// libloot's prelude substitution
    static QString replacePrelude(const QString& masterlist, const QString& prelude);

    /// Every group, "default" included
    const QList<Group>& groups() const { return m_groups; }
    /// The plugin's own entry merged with each matching regex entry in file order: a group only fills a missing one,
    /// "after" and "req" lists are appended
    PluginMetadata find(const QString& pluginName) const;

   private:
    QHash<QString, PluginMetadata> m_plugins;  ///< case-folded name
    std::vector<std::pair<QRegularExpression, PluginMetadata>> m_regexPlugins;
    QList<Group> m_groups;
};

}  // namespace Loot
