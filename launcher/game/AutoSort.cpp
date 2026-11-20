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

#include "AutoSort.h"

#include <QDir>
#include <QFile>
#include <QtConcurrent>

#include <queue>

#include "game/loot/Plugin.h"
#include "game/loot/Sorter.h"

namespace AutoSort {

using Loot::GameType;

std::optional<GameType> gameType(const QString& nexusDomain)
{
    static const QHash<QString, GameType> types{
        { "skyrimspecialedition", GameType::SkyrimSE }, { "skyrim", GameType::Skyrim },       { "fallout4", GameType::Fallout4 },
        { "newvegas", GameType::FalloutNV },            { "fallout3", GameType::Fallout3 }, { "oblivion", GameType::Oblivion },
        { "starfield", GameType::Starfield },
    };
    const auto it = types.constFind(nexusDomain);
    return it == types.constEnd() ? std::nullopt : std::optional(*it);
}

QString masterlistRepository(GameType game)
{
    switch (game) {
        case GameType::Oblivion: return "oblivion";
        case GameType::Skyrim: return "skyrim";
        case GameType::SkyrimSE: return "skyrimse";
        case GameType::Fallout3: return "fallout3";
        case GameType::FalloutNV: return "falloutnv";
        case GameType::Fallout4: return "fallout4";
        case GameType::Starfield: return "starfield";
    }
    return {};
}

// the branch for the metadata syntax of libloot 0.29, which this sorter follows
QUrl masterlistUrl(const QString& repository)
{
    return QUrl(QStringLiteral("https://raw.githubusercontent.com/loot/%1/v0.29/masterlist.yaml").arg(repository));
}

QUrl preludeUrl()
{
    return QUrl(QStringLiteral("https://raw.githubusercontent.com/loot/prelude/v0.29/prelude.yaml"));
}

QStringList earlyLoadingPlugins(GameType game, const QString& gameDir, const QString& myGamesDir)
{
    QStringList names;
    QStringList cccFiles;
    switch (game) {
        case GameType::Skyrim:
            names = { "Skyrim.esm" };
            break;
        case GameType::SkyrimSE:
            names = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };
            cccFiles = { QDir(gameDir).filePath("Skyrim.ccc") };
            break;
        case GameType::Fallout4:
            names = { "Fallout4.esm",      "DLCRobot.esm",       "DLCworkshop01.esm", "DLCCoast.esm",
                      "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm",  "DLCUltraHighResolution.esm" };
            cccFiles = { QDir(gameDir).filePath("Fallout4.ccc") };
            break;
        case GameType::Starfield:
            names = { "Starfield.esm", "Constellation.esm", "OldMars.esm", "ShatteredSpace.esm", "SFBGS003.esm", "SFBGS004.esm",
                      "SFBGS006.esm",  "SFBGS007.esm",      "SFBGS008.esm", "SFBGS00D.esm",      "SFBGS047.esm", "SFBGS050.esm" };
            // the My Games copy wins even when it is empty
            if (!myGamesDir.isEmpty()) {
                cccFiles << QDir(myGamesDir).filePath("Starfield.ccc");
            }
            cccFiles << QDir(gameDir).filePath("Starfield.ccc");
            break;
        case GameType::Oblivion:
        case GameType::Fallout3:
        case GameType::FalloutNV:
            break;
    }
    for (const auto& path : cccFiles) {
        QFile file(path);
        if (!file.exists()) {
            continue;
        }
        if (file.open(QIODevice::ReadOnly)) {
            for (auto line : file.readAll().split('\n')) {
                if (line.endsWith('\r')) {
                    line.chop(1);
                }
                if (!line.isEmpty()) {
                    names << QString::fromUtf8(line);
                }
            }
        }
        break;
    }
    QStringList unique;
    QSet<QString> seen;
    for (const auto& name : names) {
        if (!seen.contains(name.toCaseFolded())) {
            seen.insert(name.toCaseFolded());
            unique << name;
        }
    }
    return unique;
}

namespace {

/// The files whose condition and then constraint hold
Result<QStringList> applicableFiles(const std::vector<Loot::MetadataFile>& files, Loot::ConditionState& state)
{
    QStringList names;
    for (const auto& file : files) {
        if (file.condition) {
            TRY_INTO(const bool holds, file.condition->eval(state));
            if (!holds) {
                continue;
            }
        }
        if (file.constraint) {
            TRY_INTO(const bool holds, file.constraint->eval(state));
            if (!holds) {
                continue;
            }
        }
        names << file.name;
    }
    return names;
}

}  // namespace

Result<QStringList> sortPlugins(GameType game, const QString& dataPath, const QStringList& earlyLoaders, const QList<PluginState>& current,
                                const Loot::Masterlist& masterlist)
{
    QStringList names;
    for (const auto& plugin : current) {
        names << plugin.name;
    }
    TRY_INTO(const auto plugins, Loot::loadPlugins(game, dataPath, names));

    Loot::ConditionState state;
    state.game = game;
    state.dataPath = dataPath;
    for (const auto& plugin : current) {
        if (plugin.active) {
            state.activePlugins.insert(plugin.name.toLower());
        }
    }
    for (const auto& plugin : plugins) {
        if (plugin.version) {
            state.pluginVersions.insert(plugin.name.toLower(), *plugin.version);
        }
    }
    std::vector<Loot::SortingPlugin> input;
    input.reserve(plugins.size());
    for (size_t i = 0; i < plugins.size(); i++) {
        const auto metadata = masterlist.find(plugins[i].name);
        Loot::SortingPlugin sorting;
        sorting.plugin = &plugins[i];
        sorting.group = metadata.group.value_or(QStringLiteral("default"));
        sorting.loadOrderIndex = static_cast<int>(i);
        TRY_INTO(sorting.after, applicableFiles(metadata.after, state));
        TRY_INTO(sorting.requirements, applicableFiles(metadata.requirements, state));
        input.push_back(std::move(sorting));
    }
    return Loot::sortPlugins(std::move(input), masterlist.groups(), earlyLoaders);
}

QStringList sortMods(const QStringList& currentOrder, const QList<std::pair<QString, QString>>& conflicts, const ModRules& rules)
{
    const int count = static_cast<int>(currentOrder.size());
    QHash<QString, int> index;
    for (int i = 0; i < count; i++) {
        index.insert(currentOrder[i], i);
    }
    // everything a mod needs, directly or through the mods it needs
    auto needsMod = [&rules](const QString& mod, const QString& other) {
        QSet<QString> seen{ mod };
        QStringList pending{ mod };
        while (!pending.isEmpty()) {
            for (const auto& next : rules.requirements.value(pending.takeLast())) {
                if (next == other) {
                    return true;
                }
                if (!seen.contains(next)) {
                    seen.insert(next);
                    pending << next;
                }
            }
        }
        return false;
    };
    struct Edge {
        int rule;  ///< 0 requirement, 1 plugin order, 2 current order
        int first;
        int second;  ///< wins: comes later
    };
    std::vector<Edge> edges;
    for (const auto& [winner, loser] : conflicts) {
        if (!index.contains(winner) || !index.contains(loser)) {
            continue;
        }
        const int w = index.value(winner);
        const int l = index.value(loser);
        const bool winnerNeedsLoser = needsMod(winner, loser);
        const bool loserNeedsWinner = needsMod(loser, winner);
        if (winnerNeedsLoser != loserNeedsWinner) {
            edges.push_back(winnerNeedsLoser ? Edge{ 0, l, w } : Edge{ 0, w, l });
        } else if (rules.pluginRank.contains(winner) && rules.pluginRank.contains(loser) &&
                   rules.pluginRank.value(winner) != rules.pluginRank.value(loser)) {
            edges.push_back(rules.pluginRank.value(winner) > rules.pluginRank.value(loser) ? Edge{ 1, l, w } : Edge{ 1, w, l });
        } else {
            edges.push_back({ 2, l, w });
        }
    }
    // stronger rules first, and within a rule an order that does not depend on the current one, so that sorting again
    // keeps the same edges and changes nothing
    std::sort(edges.begin(), edges.end(), [&currentOrder](const Edge& a, const Edge& b) {
        if (a.rule != b.rule) {
            return a.rule < b.rule;
        }
        const auto& a1 = currentOrder[a.first];
        const auto& b1 = currentOrder[b.first];
        return a1 != b1 ? a1 < b1 : currentOrder[a.second] < currentOrder[b.second];
    });

    // stronger rules first; an edge that would close a cycle is left out
    std::vector<std::vector<int>> successors(count);
    std::vector<int> inDegree(count, 0);
    auto reaches = [&successors, count](int from, int to) {
        std::vector<bool> seen(count, false);
        std::vector<int> stack{ from };
        while (!stack.empty()) {
            const int node = stack.back();
            stack.pop_back();
            if (node == to) {
                return true;
            }
            for (const int next : successors[node]) {
                if (!seen[next]) {
                    seen[next] = true;
                    stack.push_back(next);
                }
            }
        }
        return false;
    };
    for (const auto& edge : edges) {
        if (!reaches(edge.second, edge.first)) {
            successors[edge.first].push_back(edge.second);
            inDegree[edge.second]++;
        }
    }
    // topological order that takes mods in their current order whenever it may
    std::priority_queue<int, std::vector<int>, std::greater<>> ready;
    for (int i = 0; i < count; i++) {
        if (inDegree[i] == 0) {
            ready.push(i);
        }
    }
    QStringList order;
    while (!ready.empty()) {
        const int node = ready.top();
        ready.pop();
        order << currentOrder[node];
        for (const int next : successors[node]) {
            if (--inDegree[next] == 0) {
                ready.push(next);
            }
        }
    }
    return order;
}

QStringList sortModsInGroups(const QStringList& currentOrder,
                             const QSet<QString>& separators,
                             const QList<std::pair<QString, QString>>& conflicts,
                             const ModRules& rules,
                             int* acrossGroups)
{
    QHash<QString, int> groupOf;
    int group = 0;
    for (const auto& id : currentOrder) {
        if (separators.contains(id)) {
            group++;
        }
        groupOf.insert(id, group);
    }
    QList<std::pair<QString, QString>> inside;
    int across = 0;
    for (const auto& pair : conflicts) {
        if (!groupOf.contains(pair.first) || !groupOf.contains(pair.second)) {
            continue;
        }
        if (groupOf.value(pair.first) == groupOf.value(pair.second)) {
            inside << pair;
        } else {
            across++;
        }
    }
    if (acrossGroups) {
        *acrossGroups = across;
    }
    QStringList out;
    QStringList members;
    const auto flush = [&] {
        if (!members.isEmpty()) {
            out << sortMods(members, inside, rules);
            members.clear();
        }
    };
    for (const auto& id : currentOrder) {
        if (separators.contains(id)) {
            flush();
            out << id;
        } else {
            members << id;
        }
    }
    flush();
    return out;
}

int movedCount(const QStringList& before, const QStringList& after)
{
    // the items that keep their relative order form the longest increasing run of old positions; the rest moved
    QHash<QString, int> position;
    for (int i = 0; i < before.size(); i++) {
        position.insert(before[i], i);
    }
    std::vector<int> tails;
    for (const auto& item : after) {
        const int p = position.value(item, -1);
        if (p < 0) {
            continue;
        }
        const auto it = std::lower_bound(tails.begin(), tails.end(), p);
        if (it == tails.end()) {
            tails.push_back(p);
        } else {
            *it = p;
        }
    }
    return static_cast<int>(after.size()) - static_cast<int>(tails.size());
}

void SortTask::executeTask()
{
    setStatus(tr("Sorting plugins with LOOT's rules and finding mods with the same files"));
    connect(&m_watcher, &QFutureWatcher<Result<QStringList>>::finished, this, [this] {
        const auto result = m_watcher.result();
        if (!result) {
            emitFailed(result.error());
            return;
        }
        m_result = *result;
        emitSucceeded();
    });
    m_watcher.setFuture(QtConcurrent::run(m_work));
}

}  // namespace AutoSort
