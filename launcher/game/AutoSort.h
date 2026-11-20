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

#include <QFutureWatcher>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <functional>
#include <optional>

#include "Result.h"
#include "game/loot/Condition.h"
#include "game/loot/Masterlist.h"
#include "tasks/Task.h"

/// Automatic load order: plugins sorted with LOOT's rules and masterlist, mods put in an order that agrees with them
namespace AutoSort {

/// The game as LOOT knows it; nullopt for games LOOT does not sort
std::optional<Loot::GameType> gameType(const QString& nexusDomain);
/// LOOT's masterlist repository for the game ("skyrimse", "fallout4", ...)
QString masterlistRepository(Loot::GameType game);
QUrl masterlistUrl(const QString& repository);
QUrl preludeUrl();

/// The plugins that always load first, in order: the game's own (libloadorder's list), then its Creation Club list
QStringList earlyLoadingPlugins(Loot::GameType game, const QString& gameDir, const QString& myGamesDir);

struct PluginState {
    QString name;
    bool active = false;
};

/// The order LOOT gives the installed plugins; @p current is the current load order of all of them
Result<QStringList> sortPlugins(Loot::GameType game, const QString& dataPath, const QStringList& earlyLoaders,
                                const QList<PluginState>& current, const Loot::Masterlist& masterlist);

struct ModRules {
    QHash<QString, QSet<QString>> requirements;  ///< mod id -> ids of installed mods it needs (Nexus Mods requirements)
    QHash<QString, int> pluginRank;          ///< mod id -> position of its last plugin in the load order
};

/**
 * A new mod order. Of two mods in @p conflicts (every pair that has a file in common, winner first) the one that needs the
 * other wins; failing that, the one whose plugins load later; failing that, the one that wins now. Every other mod
 * keeps its place as far as those rules allow.
 */
QStringList sortMods(const QStringList& currentOrder, const QList<std::pair<QString, QString>>& conflicts, const ModRules& rules);

/**
 * sortMods for a list that @p separators split into groups the person ordered: each group is sorted on its own and the
 * groups and separators keep their places, so no mod leaves its group. @p acrossGroups gets how many pairs of
 * @p conflicts have their mods in different groups (the later group wins those).
 */
QStringList sortModsInGroups(const QStringList& currentOrder,
                             const QSet<QString>& separators,
                             const QList<std::pair<QString, QString>>& conflicts,
                             const ModRules& rules,
                             int* acrossGroups = nullptr);

/// How many items of @p before have to move to get @p after (both hold the same items)
int movedCount(const QStringList& before, const QStringList& after);

/// Runs the plugin sort on a worker thread
class SortTask : public Task {
    Q_OBJECT
   public:
    explicit SortTask(std::function<Result<QStringList>()> work) : m_work(std::move(work)) {}
    const QStringList& result() const { return m_result; }

   protected:
    void executeTask() override;

   private:
    std::function<Result<QStringList>()> m_work;
    QFutureWatcher<Result<QStringList>> m_watcher;
    QStringList m_result;
};

}  // namespace AutoSort
