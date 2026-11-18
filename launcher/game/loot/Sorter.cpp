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

// A port of libloot's sorting (src/sorting/{plugins,groups,search,validate}.rs). Iteration orders matter: they decide
// which paths the searches find and so which tie-break edges get added, so they follow petgraph exactly: a node's
// neighbours come newest edge first, while the depth-first searches walk a node's edges oldest first.

#include "Sorter.h"

#include <QDebug>
#include <QHash>
#include <QObject>
#include <QSet>

#include <algorithm>
#include <unordered_set>

namespace Loot {

namespace {

enum class EdgeType {
    Hardcoded,
    MasterFlag,
    Master,
    BlueprintMaster,
    MasterlistRequirement,
    MasterlistLoadAfter,
    MasterlistGroup,
    RecordOverlap,
    AssetOverlap,
    TieBreak,
};

QString edgeName(EdgeType type)
{
    switch (type) {
        case EdgeType::Hardcoded: return QObject::tr("Hardcoded");
        case EdgeType::MasterFlag: return QObject::tr("Master Flag");
        case EdgeType::Master: return QObject::tr("Master");
        case EdgeType::BlueprintMaster: return QObject::tr("Blueprint Master");
        case EdgeType::MasterlistRequirement: return QObject::tr("Masterlist Requirement");
        case EdgeType::MasterlistLoadAfter: return QObject::tr("Masterlist Load After");
        case EdgeType::MasterlistGroup: return QObject::tr("Masterlist Group");
        case EdgeType::RecordOverlap: return QObject::tr("Record Overlap");
        case EdgeType::AssetOverlap: return QObject::tr("Asset Overlap");
        case EdgeType::TieBreak: return QObject::tr("Tie Break");
    }
    return {};
}

struct Vertex {
    QString name;
    EdgeType outEdge;
};

QString cycleMessage(const std::vector<Vertex>& cycle)
{
    QString path;
    for (const auto& vertex : cycle) {
        path += QStringLiteral("%1 --[%2]--> ").arg(vertex.name, edgeName(vertex.outEdge));
    }
    if (!cycle.empty()) {
        path += cycle.front().name;
    }
    return QObject::tr("Cyclic interaction detected between \"%1\" and \"%2\": %3")
        .arg(cycle.empty() ? QString() : cycle.front().name, cycle.empty() ? QString() : cycle.back().name, path);
}

bool utf8Less(const QString& a, const QString& b)
{
    return a.toUtf8() < b.toUtf8();
}

/// A petgraph-like directed graph: edges remembered per node in the order they were added
struct Digraph {
    struct Edge {
        int from;
        int to;
        EdgeType type;
    };
    std::vector<Edge> edges;
    std::vector<std::vector<int>> out;  ///< edge indices, oldest first
    std::vector<std::vector<int>> in;
    std::unordered_set<quint64> pairs;

    explicit Digraph(int nodes = 0) : out(nodes), in(nodes) {}

    int nodeCount() const { return static_cast<int>(out.size()); }
    int addNode()
    {
        out.emplace_back();
        in.emplace_back();
        return nodeCount() - 1;
    }
    void addEdge(int from, int to, EdgeType type)
    {
        const int index = static_cast<int>(edges.size());
        edges.push_back({ from, to, type });
        out[from].push_back(index);
        in[to].push_back(index);
        pairs.insert(key(from, to));
    }
    bool containsEdge(int from, int to) const { return pairs.contains(key(from, to)); }
    static quint64 key(int from, int to) { return (static_cast<quint64>(static_cast<quint32>(from)) << 32) | static_cast<quint32>(to); }
};

enum class Colour : quint8 { White, Grey, Black };

/// libloot's depth_first_search: iterative, a node's edges oldest first
template <typename Visitor>
void depthFirstSearch(const Digraph& graph, std::vector<Colour>& colours, int start, Visitor& visitor)
{
    struct Frame {
        int node;
        size_t next;
    };
    std::vector<Frame> stack{ { start, 0 } };
    colours[start] = Colour::Grey;
    visitor.discover(start);
    while (!stack.empty()) {
        const int node = stack.back().node;
        const auto& edges = graph.out[node];
        if (stack.back().next < edges.size()) {
            const int edge = edges[stack.back().next++];
            const int target = graph.edges[edge].to;
            switch (colours[target]) {
                case Colour::White:
                    visitor.treeEdge(edge);
                    colours[target] = Colour::Grey;
                    visitor.discover(target);
                    stack.push_back({ target, 0 });
                    break;
                case Colour::Grey:
                    visitor.backEdge(edge);
                    break;
                case Colour::Black:
                    visitor.forwardOrCrossEdge(edge);
                    break;
            }
        } else {
            colours[node] = Colour::Black;
            visitor.finish(node);
            stack.pop_back();
        }
    }
}

struct CycleDetector {
    const Digraph& graph;
    const std::function<QString(int)>& name;
    std::vector<Vertex> trail;
    bool found = false;

    void treeEdge(int edge)
    {
        if (!found) {
            trail.push_back({ name(graph.edges[edge].from), graph.edges[edge].type });
        }
    }
    void backEdge(int edge)
    {
        if (found) {
            return;
        }
        treeEdge(edge);
        const auto target = name(graph.edges[edge].to);
        const auto at = std::find_if(trail.begin(), trail.end(), [&target](const Vertex& v) { return v.name == target; });
        if (at != trail.end()) {
            trail.erase(trail.begin(), at);
            found = true;
        }
    }
    void forwardOrCrossEdge(int) {}
    void discover(int) {}
    void finish(int)
    {
        if (!found && !trail.empty()) {
            trail.pop_back();
        }
    }
};

std::optional<std::vector<Vertex>> findCycle(const Digraph& graph, const std::function<QString(int)>& name)
{
    CycleDetector detector{ graph, name, {}, false };
    std::vector<Colour> colours(graph.nodeCount(), Colour::White);
    for (int node = 0; node < graph.nodeCount(); node++) {
        depthFirstSearch(graph, colours, node, detector);
        if (detector.found) {
            return detector.trail;
        }
    }
    return std::nullopt;
}

////// Groups (groups.rs) //////

struct GroupsGraph {
    Digraph graph;
    QStringList names;
};

Result<GroupsGraph> buildGroupsGraph(QList<Group> groups)
{
    std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return utf8Less(a.name, b.name); });
    GroupsGraph out;
    QHash<QString, int> nodes;
    for (const auto& group : groups) {
        if (!nodes.contains(group.name)) {
            nodes.insert(group.name, out.graph.addNode());
            out.names << group.name;
        }
    }
    for (const auto& group : groups) {
        const int node = nodes.value(group.name);
        auto after = group.after;
        std::sort(after.begin(), after.end(), utf8Less);
        for (const auto& other : after) {
            const auto it = nodes.constFind(other);
            if (it == nodes.constEnd()) {
                return std::unexpected(QObject::tr("The group \"%1\" does not exist").arg(other));
            }
            if (!out.graph.containsEdge(*it, node)) {
                out.graph.addEdge(*it, node, EdgeType::MasterlistLoadAfter);
            }
        }
    }
    const std::function<QString(int)> name = [&out](int node) { return out.names[node]; };
    if (const auto cycle = findCycle(out.graph, name)) {
        return std::unexpected(cycleMessage(*cycle));
    }
    return out;
}

/// Root groups first, deepest first; otherwise in name order
std::vector<int> sortedGroupNodes(const GroupsGraph& groups)
{
    struct PathLength {
        int current = 0;
        int max = 0;
        void treeEdge(int) {}
        void backEdge(int) {}
        void forwardOrCrossEdge(int) {}
        void discover(int)
        {
            max = qMax(max, ++current);
        }
        void finish(int) { current--; }
    };
    struct Item {
        int node;
        bool root;
        int length;
    };
    std::vector<Item> items;
    for (int node = 0; node < groups.graph.nodeCount(); node++) {
        if (groups.graph.in[node].empty()) {
            PathLength visitor;
            std::vector<Colour> colours(groups.graph.nodeCount(), Colour::White);
            depthFirstSearch(groups.graph, colours, node, visitor);
            items.push_back({ node, true, visitor.max });
        } else {
            items.push_back({ node, false, 0 });
        }
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.root != b.root ? a.root : a.length > b.length;
    });
    std::vector<int> out;
    for (const auto& item : items) {
        out.push_back(item.node);
    }
    return out;
}

////// Plugins (plugins.rs) //////

class PluginsGraph {
   public:
    explicit PluginsGraph(std::vector<const SortingPlugin*> plugins)
        : m_plugins(std::move(plugins)),
          m_graph(static_cast<int>(m_plugins.size())),
          m_count(m_plugins.size()),
          m_cache((m_count * m_count + 63) / 64, 0),
          m_forwardMark(m_count, 0),
          m_reverseMark(m_count, 0),
          m_forwardParent(m_count, -1),
          m_reverseChild(m_count, -1)
    {
        for (int i = 0; i < static_cast<int>(m_plugins.size()); i++) {
            m_byName.tryEmplace(name(i).toCaseFolded(), i);  // node_index_by_name finds the first node with the name
        }
    }

    QString name(int node) const { return m_plugins[node]->plugin->name; }
    int count() const { return static_cast<int>(m_count); }

    void addSpecificEdges()
    {
        for (int node = 0; node < count(); node++) {
            const auto& plugin = *m_plugins[node];
            // a safety net kept from libloot: partitions never mix masters and non-masters
            for (int other = node + 1; other < count(); other++) {
                const auto& otherPlugin = *m_plugins[other];
                if (plugin.plugin->master == otherPlugin.plugin->master) {
                    continue;
                }
                if (otherPlugin.plugin->master) {
                    addEdge(other, node, EdgeType::MasterFlag);
                } else {
                    addEdge(node, other, EdgeType::MasterFlag);
                }
            }
            for (const auto& master : plugin.plugin->header.masters) {
                if (const int other = nodeByName(master); other >= 0) {
                    addEdge(other, node, EdgeType::Master);
                }
            }
            for (const auto& file : plugin.requirements) {
                if (const int other = nodeByName(file); other >= 0) {
                    addEdge(other, node, EdgeType::MasterlistRequirement);
                }
            }
            for (const auto& file : plugin.after) {
                if (const int other = nodeByName(file); other >= 0) {
                    addEdge(other, node, EdgeType::MasterlistLoadAfter);
                }
            }
        }
    }

    void addEarlyLoadingPluginEdges(const QStringList& earlyLoaders)
    {
        if (earlyLoaders.isEmpty()) {
            return;
        }
        QStringList folded;
        for (const auto& name : earlyLoaders) {
            folded << name.toCaseFolded();
        }
        std::vector<std::pair<qsizetype, int>> early;
        std::vector<int> others;
        for (int node = 0; node < count(); node++) {
            if (const auto at = folded.indexOf(name(node).toCaseFolded()); at >= 0) {
                early.emplace_back(at, node);
            } else {
                others.push_back(node);
            }
        }
        std::stable_sort(early.begin(), early.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (size_t i = 1; i < early.size(); i++) {
            addEdge(early[i - 1].second, early[i].second, EdgeType::Hardcoded);
        }
        if (!early.empty()) {
            for (const int node : others) {
                addEdge(early.back().second, node, EdgeType::Hardcoded);
            }
        }
    }

    Result<> checkForCycles() const
    {
        const std::function<QString(int)> nodeName = [this](int node) { return name(node); };
        if (const auto cycle = findCycle(m_graph, nodeName)) {
            return std::unexpected(cycleMessage(*cycle));
        }
        return {};
    }

    Result<> addGroupEdges(const GroupsGraph& groups)
    {
        QHash<QString, std::vector<int>> pluginsInGroups;
        for (int node = 0; node < count(); node++) {
            pluginsInGroups[m_plugins[node]->group].push_back(node);
        }
        const int defaultGroup = static_cast<int>(groups.names.indexOf("default"));
        if (defaultGroup < 0) {
            return std::unexpected(QObject::tr("The group \"default\" does not exist"));
        }
        std::unordered_set<int> finished;
        for (const int groupNode : sortedGroupNodes(groups)) {
            GroupsPathVisitor visitor{ *this, groups, pluginsInGroups, finished, defaultGroup };
            std::vector<Colour> colours(groups.graph.nodeCount(), Colour::White);
            depthFirstSearch(groups.graph, colours, groupNode, visitor);
        }
        GroupsPathVisitor visitor{ *this, groups, pluginsInGroups, finished, -1 };
        std::vector<Colour> colours(groups.graph.nodeCount(), Colour::White);
        depthFirstSearch(groups.graph, colours, defaultGroup, visitor);
        return {};
    }

    void addOverlapEdges()
    {
        for (int node = 0; node < count(); node++) {
            const auto& plugin = *m_plugins[node]->plugin;
            if (plugin.overrideCount == 0 && plugin.assetCount == 0) {
                continue;
            }
            for (int other = node + 1; other < count(); other++) {
                const auto& otherPlugin = *m_plugins[other]->plugin;
                if (m_graph.containsEdge(node, other) || m_graph.containsEdge(other, node)) {
                    continue;
                }
                // overlapping records decide when the override counts differ, otherwise overlapping assets do
                bool nodeFirst = false;
                EdgeType type = EdgeType::RecordOverlap;
                if (plugin.overrideCount == otherPlugin.overrideCount || !plugin.recordsOverlap(otherPlugin)) {
                    if (plugin.assetCount == otherPlugin.assetCount || !doAssetsOverlap(plugin.assets, otherPlugin.assets)) {
                        continue;
                    }
                    nodeFirst = plugin.assetCount > otherPlugin.assetCount;
                    type = EdgeType::AssetOverlap;
                } else {
                    nodeFirst = plugin.overrideCount > otherPlugin.overrideCount;
                }
                const int from = nodeFirst ? node : other;
                const int to = nodeFirst ? other : node;
                if (!isPathCached(from, to) && !pathExists(to, from)) {
                    addEdge(from, to, type);
                }
            }
        }
    }

    void addTieBreakEdges()
    {
        std::vector<int> newLoadOrder;
        std::vector<bool> processed(m_count, false);
        std::vector<int> nodes(m_count);
        for (int i = 0; i < count(); i++) {
            nodes[i] = i;
        }
        std::stable_sort(nodes.begin(), nodes.end(),
                         [this](int a, int b) { return m_plugins[a]->loadOrderIndex < m_plugins[b]->loadOrderIndex; });
        for (size_t i = 0; i + 1 < nodes.size(); i++) {
            const int current = nodes[i];
            const int next = nodes[i + 1];
            auto path = findPath(next, current);
            if (path.empty()) {
                addEdge(current, next, EdgeType::TieBreak);
                if (!processed[current]) {
                    newLoadOrder.push_back(current);
                    processed[current] = true;
                } else if (newLoadOrder.empty() || newLoadOrder.back() != current) {
                    pinNodePosition(processed, newLoadOrder, next, 0);
                }
                continue;
            }
            if (nodes.front() == current) {
                for (const int node : path) {
                    newLoadOrder.push_back(node);
                    processed[node] = true;
                }
                continue;
            }
            path.pop_back();  // current, which goes at the end below
            size_t rangeStart = 0;
            for (const int node : path) {
                rangeStart = pinNodePosition(processed, newLoadOrder, node, rangeStart);
            }
            if (!processed[current]) {
                newLoadOrder.push_back(current);
                processed[current] = true;
            }
        }
    }

    /// petgraph's toposort
    std::vector<int> topologicalSort() const
    {
        std::vector<bool> discovered(m_count, false);
        std::vector<bool> finished(m_count, false);
        std::vector<int> finishStack;
        std::vector<int> stack;
        for (int i = count() - 1; i >= 0; i--) {
            if (discovered[i]) {
                continue;
            }
            stack.push_back(i);
            while (!stack.empty()) {
                const int node = stack.back();
                if (!discovered[node]) {
                    discovered[node] = true;
                    const auto& edges = m_graph.out[node];
                    for (auto it = edges.rbegin(); it != edges.rend(); ++it) {
                        const int successor = m_graph.edges[*it].to;
                        if (!discovered[successor]) {
                            stack.push_back(successor);
                        }
                    }
                } else {
                    stack.pop_back();
                    if (!finished[node]) {
                        finished[node] = true;
                        finishStack.push_back(node);
                    }
                }
            }
        }
        std::reverse(finishStack.begin(), finishStack.end());
        return finishStack;
    }

    void checkPathIsHamiltonian(const std::vector<int>& path) const
    {
        for (size_t i = 0; i + 1 < path.size(); i++) {
            if (!m_graph.containsEdge(path[i], path[i + 1])) {
                qWarning() << "Auto sort: the path is not unique. No edge exists between" << name(path[i]) << "and" << name(path[i + 1]);
                return;
            }
        }
    }

   private:
    struct GroupsPathVisitor {
        PluginsGraph& plugins;
        const GroupsGraph& groups;
        const QHash<QString, std::vector<int>>& pluginsInGroups;
        std::unordered_set<int>& finished;
        int ignoredSource;  ///< -1 for none
        std::vector<const std::vector<int>*> edgeStack;  ///< plugins of each tree edge's source group
        std::unordered_set<int> unfinishable;
        std::vector<int> stackSources;

        const std::vector<int>* pluginsIn(int group) const
        {
            static const std::vector<int> none;
            const auto it = pluginsInGroups.constFind(groups.names[group]);
            return it == pluginsInGroups.constEnd() ? &none : &*it;
        }
        void treeEdge(int edge)
        {
            static const std::vector<int> none;
            const int source = groups.graph.edges[edge].from;
            const int target = groups.graph.edges[edge].to;
            const bool ignore = source == ignoredSource || finished.contains(source);
            edgeStack.push_back(ignore ? &none : pluginsIn(source));
            stackSources.push_back(source);
            const auto* targets = pluginsIn(target);
            if (targets->empty()) {
                return;
            }
            for (const auto* sources : edgeStack) {
                for (const int from : *sources) {
                    for (const int to : *targets) {
                        if (!plugins.isPathCached(from, to) && !plugins.pathExists(to, from)) {
                            plugins.addEdge(from, to, EdgeType::MasterlistGroup);
                        }
                    }
                }
            }
        }
        void forwardOrCrossEdge(int edge)
        {
            // nothing on the current path may pass edges on beyond the target any more
            for (const int source : stackSources) {
                unfinishable.insert(source);
            }
            unfinishable.insert(groups.graph.edges[edge].from);
        }
        void backEdge(int) {}
        void discover(int) {}
        void finish(int node)
        {
            if (node != ignoredSource && !unfinishable.contains(node)) {
                finished.insert(node);
            }
            if (!edgeStack.empty()) {
                edgeStack.pop_back();
                stackSources.pop_back();
            }
        }
    };

    void addEdge(int from, int to, EdgeType type)
    {
        if (isPathCached(from, to)) {
            return;
        }
        m_graph.addEdge(from, to, type);
        cachePath(from, to);
    }

    int nodeByName(const QString& name) const { return m_byName.value(name.toCaseFolded(), -1); }

    void cachePath(int from, int to)
    {
        const size_t bit = static_cast<size_t>(from) * m_count + to;
        m_cache[bit / 64] |= quint64(1) << (bit % 64);
    }
    bool isPathCached(int from, int to) const
    {
        const size_t bit = static_cast<size_t>(from) * m_count + to;
        return (m_cache[bit / 64] >> (bit % 64)) & 1;
    }

    /// libloot's bidirectional_bfs; @p findingPath records the path for findPath
    bool bidirectionalBfs(int from, int to, bool findingPath)
    {
        m_stamp++;
        m_forwardQueue.clear();
        m_reverseQueue.clear();
        m_forwardQueue.push_back(from);
        m_reverseQueue.push_back(to);
        m_forwardMark[from] = m_stamp;
        m_reverseMark[to] = m_stamp;
        size_t forwardHead = 0;
        size_t reverseHead = 0;
        while (forwardHead < m_forwardQueue.size() && reverseHead < m_reverseQueue.size()) {
            const int forward = m_forwardQueue[forwardHead++];
            const int reverse = m_reverseQueue[reverseHead++];
            if (forward == to || m_reverseMark[forward] == m_stamp) {
                m_intersection = forward;
                return true;
            }
            const auto& outEdges = m_graph.out[forward];
            for (auto it = outEdges.rbegin(); it != outEdges.rend(); ++it) {
                const int adjacent = m_graph.edges[*it].to;
                if (m_forwardMark[adjacent] != m_stamp) {
                    cachePath(from, adjacent);
                    if (findingPath) {
                        m_forwardParent[adjacent] = forward;
                    }
                    m_forwardMark[adjacent] = m_stamp;
                    m_forwardQueue.push_back(adjacent);
                }
            }
            if (reverse == from || m_forwardMark[reverse] == m_stamp) {
                m_intersection = reverse;
                return true;
            }
            const auto& inEdges = m_graph.in[reverse];
            for (auto it = inEdges.rbegin(); it != inEdges.rend(); ++it) {
                const int adjacent = m_graph.edges[*it].from;
                if (m_reverseMark[adjacent] != m_stamp) {
                    cachePath(adjacent, to);
                    if (findingPath) {
                        m_reverseChild[adjacent] = reverse;
                    }
                    m_reverseMark[adjacent] = m_stamp;
                    m_reverseQueue.push_back(adjacent);
                }
            }
        }
        return false;
    }

    bool pathExists(int from, int to)
    {
        return isPathCached(from, to) || bidirectionalBfs(from, to, false);
    }

    /// The path from @p from to @p to, empty when there is none
    std::vector<int> findPath(int from, int to)
    {
        if (!bidirectionalBfs(from, to, true)) {
            return {};
        }
        std::vector<int> path{ m_intersection };
        for (int node = m_intersection; node != from;) {
            node = m_forwardParent[node];
            path.push_back(node);
        }
        std::reverse(path.begin(), path.end());
        for (int node = m_intersection; node != to;) {
            node = m_reverseChild[node];
            path.push_back(node);
        }
        return path;
    }

    size_t pinNodePosition(std::vector<bool>& processed, std::vector<int>& newLoadOrder, int node, size_t rangeStart)
    {
        if (processed[node]) {
            return rangeStart;
        }
        // the last plugin of the new order so far that this one can load after
        std::optional<size_t> previous;
        for (size_t p = newLoadOrder.size(); p > rangeStart; p--) {
            if (!pathExists(node, newLoadOrder[p - 1])) {
                previous = p - 1;
                break;
            }
        }
        if (previous) {
            addEdge(newLoadOrder[*previous], node, EdgeType::TieBreak);
        }
        const size_t insert = previous ? *previous + 1 : rangeStart;
        if (insert < newLoadOrder.size()) {
            addEdge(node, newLoadOrder[insert], EdgeType::TieBreak);
        }
        newLoadOrder.insert(newLoadOrder.begin() + static_cast<std::ptrdiff_t>(insert), node);
        processed[node] = true;
        return insert + 1;
    }

    std::vector<const SortingPlugin*> m_plugins;
    Digraph m_graph;
    size_t m_count;
    std::vector<quint64> m_cache;  ///< bit from * count + to: a path from -> to is known
    QHash<QString, int> m_byName;
    quint32 m_stamp = 0;
    std::vector<quint32> m_forwardMark;
    std::vector<quint32> m_reverseMark;
    std::vector<int> m_forwardParent;
    std::vector<int> m_reverseChild;
    std::vector<int> m_forwardQueue;
    std::vector<int> m_reverseQueue;
    int m_intersection = -1;
};

Result<QStringList> sortPartition(std::vector<const SortingPlugin*> plugins, const GroupsGraph& groups, const QStringList& earlyLoaders)
{
    PluginsGraph graph(std::move(plugins));
    graph.addSpecificEdges();
    graph.addEarlyLoadingPluginEdges(earlyLoaders);
    TRY(graph.checkForCycles());
    TRY(graph.addGroupEdges(groups));
    graph.addOverlapEdges();
    graph.addTieBreakEdges();
    TRY(graph.checkForCycles());
    const auto sorted = graph.topologicalSort();
    graph.checkPathIsHamiltonian(sorted);
    QStringList names;
    for (const int node : sorted) {
        names << graph.name(node);
    }
    return names;
}

/// libloot's validate_specific_and_hardcoded_edges: a master may not need a non-master, nothing may need a blueprint master
Result<> validatePartitions(const std::vector<const SortingPlugin*>& masters, const std::vector<const SortingPlugin*>& blueprintMasters,
                            const std::vector<const SortingPlugin*>& nonMasters, const QStringList& earlyLoaders)
{
    QSet<QString> nonMasterNames;
    for (const auto* p : nonMasters) {
        nonMasterNames.insert(p->plugin->name.toCaseFolded());
    }
    QSet<QString> blueprintNames;
    for (const auto* p : blueprintMasters) {
        blueprintNames.insert(p->plugin->name.toCaseFolded());
    }
    auto cycle = [](const QString& first, EdgeType firstEdge, const QString& second, EdgeType secondEdge) {
        return std::unexpected(cycleMessage({ { first, firstEdge }, { second, secondEdge } }));
    };
    auto validate = [&](const SortingPlugin& p, const QSet<QString>& nonMastersToCheck) -> Result<> {
        for (const auto& master : p.plugin->header.masters) {
            if (nonMastersToCheck.contains(master.toCaseFolded())) {
                return cycle(master, EdgeType::Master, p.plugin->name, EdgeType::MasterFlag);
            }
            if (blueprintNames.contains(master.toCaseFolded())) {
                qWarning() << "Auto sort:" << p.plugin->name << "has the blueprint master" << master << "as one of its masters";
            }
        }
        for (const auto& [files, type] : { std::pair{ &p.requirements, EdgeType::MasterlistRequirement }, std::pair{ &p.after, EdgeType::MasterlistLoadAfter } }) {
            for (const auto& file : *files) {
                if (nonMastersToCheck.contains(file.toCaseFolded())) {
                    return cycle(file, type, p.plugin->name, EdgeType::MasterFlag);
                }
                if (blueprintNames.contains(file.toCaseFolded())) {
                    return cycle(file, type, p.plugin->name, EdgeType::BlueprintMaster);
                }
            }
        }
        return {};
    };
    for (const auto* p : masters) {
        TRY(validate(*p, nonMasterNames));
    }
    for (const auto* p : nonMasters) {
        TRY(validate(*p, {}));
    }
    if (!masters.empty()) {
        for (const auto& plugin : earlyLoaders) {
            if (nonMasterNames.contains(plugin.toCaseFolded())) {
                return cycle(plugin, EdgeType::Hardcoded, masters.front()->plugin->name, EdgeType::MasterFlag);
            }
        }
    }
    return {};
}

}  // namespace

Result<QStringList> sortPlugins(std::vector<SortingPlugin> plugins, const QList<Group>& groups, const QStringList& earlyLoadingPlugins)
{
    if (plugins.empty()) {
        return QStringList();
    }
    TRY_INTO(const auto groupsGraph, buildGroupsGraph(groups));
    for (const auto& plugin : plugins) {
        if (!groupsGraph.names.contains(plugin.group)) {
            return std::unexpected(QObject::tr("The group \"%1\" of %2 does not exist").arg(plugin.group, plugin.plugin->name));
        }
    }
    // name order makes the graph, and so the result, independent of anything but the input data
    std::vector<std::pair<QByteArray, const SortingPlugin*>> byName;
    for (const auto& plugin : plugins) {
        byName.emplace_back(plugin.plugin->name.toUtf8(), &plugin);
    }
    std::stable_sort(byName.begin(), byName.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<const SortingPlugin*> masters;
    std::vector<const SortingPlugin*> blueprintMasters;
    std::vector<const SortingPlugin*> nonMasters;
    for (const auto& [name, plugin] : byName) {
        if (!plugin->plugin->master) {
            nonMasters.push_back(plugin);
        } else if (plugin->plugin->blueprint) {
            blueprintMasters.push_back(plugin);
        } else {
            masters.push_back(plugin);
        }
    }
    TRY(validatePartitions(masters, blueprintMasters, nonMasters, earlyLoadingPlugins));

    TRY_INTO(auto order, sortPartition(masters, groupsGraph, earlyLoadingPlugins));
    TRY_INTO(const auto blueprintOrder, sortPartition(blueprintMasters, groupsGraph, earlyLoadingPlugins));
    TRY_INTO(const auto nonMasterOrder, sortPartition(nonMasters, groupsGraph, earlyLoadingPlugins));
    order << nonMasterOrder << blueprintOrder;
    return order;
}

}  // namespace Loot
