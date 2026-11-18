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

#include <QStringList>
#include <vector>

#include "Masterlist.h"
#include "Plugin.h"
#include "Result.h"

namespace Loot {

/// One plugin's input to sorting (libloot's PluginSortingData)
struct SortingPlugin {
    const Plugin* plugin = nullptr;
    QString group = QStringLiteral("default");
    QStringList after;         ///< plugins it loads after (conditions already evaluated)
    QStringList requirements;  ///< plugins it needs (conditions already evaluated)
    int loadOrderIndex = 0;    ///< position in the current load order
};

/**
 * libloot's plugin sorting, ported 1:1: masters, non-masters and Starfield blueprint masters sorted apart, each as a
 * graph of master, masterlist, early-loading, group, record and asset overlap edges, with tie-breaks that keep the
 * current load order wherever nothing says otherwise. Errors name the plugins of a cyclic interaction.
 */
Result<QStringList> sortPlugins(std::vector<SortingPlugin> plugins, const QList<Group>& groups, const QStringList& earlyLoadingPlugins);

}  // namespace Loot
