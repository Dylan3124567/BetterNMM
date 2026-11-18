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

#include "PluginList.h"

#include <QColor>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QTimeZone>

#include <algorithm>
#include <climits>

#include "FileSystem.h"

static bool isPluginFile(const QString& name)
{
    return name.endsWith(".esp", Qt::CaseInsensitive) || name.endsWith(".esm", Qt::CaseInsensitive) ||
           name.endsWith(".esl", Qt::CaseInsensitive);
}

PluginList::PluginList(GameDefinition game, Paths paths, QObject* parent)
    : QAbstractTableModel(parent), m_game(std::move(game)), m_paths(std::move(paths))
{
    // connected before any view, so the master check never sees an old list
    connect(this, &QAbstractItemModel::modelReset, this, [this] { m_activeByName.clear(); });
    connect(this, &QAbstractItemModel::dataChanged, this, [this] { m_activeByName.clear(); });
}

void PluginList::readHeaderFlags(const QString& path, bool& master, bool& light)
{
    master = path.endsWith(".esm", Qt::CaseInsensitive) || path.endsWith(".esl", Qt::CaseInsensitive);
    light = path.endsWith(".esl", Qt::CaseInsensitive);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const auto header = file.read(12);
    if (header.size() < 12 || !header.startsWith("TES4")) {
        return;
    }
    const auto* p = reinterpret_cast<const quint8*>(header.constData() + 8);
    const quint32 flags = p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<quint32>(p[3]) << 24);
    master |= (flags & 0x1) != 0;
    light |= (flags & 0x200) != 0;
}

QStringList PluginList::readMasters(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const auto head = file.read(24);
    if (head.size() < 24 || !head.startsWith("TES4")) {
        return {};
    }
    auto u32 = [](const QByteArray& data, qsizetype at) {
        const auto* p = reinterpret_cast<const quint8*>(data.constData() + at);
        return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<quint32>(p[3]) << 24);
    };
    const quint32 size = u32(head, 4);
    // the record header is 24 bytes since Fallout 3 and 20 in Oblivion; the HEDR field always comes first
    const qsizetype headerSize = head.mid(20, 4) == "HEDR" ? 20 : 24;
    if (!file.seek(headerSize)) {
        return {};
    }
    const auto data = file.read(size);
    QStringList masters;
    qsizetype at = 0;
    quint32 bigSize = 0;  // an XXXX field gives the size of the field after it when that is over 65535
    while (at + 6 <= data.size()) {
        const auto type = data.mid(at, 4);
        quint32 length = static_cast<quint8>(data[at + 4]) | (static_cast<quint8>(data[at + 5]) << 8);
        at += 6;
        if (bigSize > 0) {
            length = bigSize;
            bigSize = 0;
        }
        if (at + length > data.size()) {
            break;
        }
        if (type == "XXXX" && length == 4) {
            bigSize = u32(data, at);
        } else if (type == "MAST") {
            masters << QString::fromLatin1(data.mid(at, length)).section(QChar('\0'), 0, 0);
        }
        at += length;
    }
    return masters;
}

QStringList PluginList::implicitNames() const
{
    auto names = m_game.implicitPlugins;
    if (!m_game.cccFile.isEmpty()) {
        QFile ccc(FS::PathCombine(m_paths.gameDir, m_game.cccFile));
        if (ccc.open(QIODevice::ReadOnly | QIODevice::Text)) {
            for (const auto& line : QString::fromUtf8(ccc.readAll()).split('\n')) {
                if (const auto name = line.trimmed(); !name.isEmpty()) {
                    names << name;
                }
            }
        }
    }
    return names;
}

PluginList::SavedOrder PluginList::readOrderFile(const QString& path, bool activeMarkers) const
{
    SavedOrder order;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return order;
    }
    order.found = true;
    for (auto line : QString::fromUtf8(file.readAll()).split('\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        bool active = !activeMarkers;
        if (line.startsWith('*')) {
            active = true;
            line = line.mid(1);
        }
        order.names << line;
        if (active) {
            order.active.insert(line.toLower());
        }
    }
    return order;
}

Result<> PluginList::refresh()
{
    // names on disk keyed by lower case
    QHash<QString, QString> onDisk;
    for (const auto& entry : QDirListing(m_paths.dataDir, QDirListing::IteratorFlag::FilesOnly)) {
        if (isPluginFile(entry.fileName())) {
            onDisk.insert(entry.fileName().toLower(), entry.fileName());
        }
    }

    // BetterNMM's own order first; on the first run import whatever the game had
    auto saved = readOrderFile(m_paths.orderFile, true);
    if (!saved.found) {
        const auto gamePlugins = FS::PathCombine(m_paths.gameListDir, "plugins.txt");
        switch (m_game.pluginFormat) {
            case PluginFormat::Asterisk:
                saved = readOrderFile(gamePlugins, true);
                break;
            case PluginFormat::Textfile: {
                saved = readOrderFile(FS::PathCombine(m_paths.gameListDir, "loadorder.txt"), false);
                saved.active = readOrderFile(gamePlugins, false).active;
                break;
            }
            case PluginFormat::Timestamp: {
                saved.active = readOrderFile(gamePlugins, false).active;
                auto names = onDisk.values();
                std::sort(names.begin(), names.end(), [this](const QString& a, const QString& b) {
                    return QFileInfo(FS::PathCombine(m_paths.dataDir, a)).lastModified(QTimeZone::UTC) <
                           QFileInfo(FS::PathCombine(m_paths.dataDir, b)).lastModified(QTimeZone::UTC);
                });
                saved.names = names;
                break;
            }
            case PluginFormat::None:
                break;
        }
    }

    QList<PluginEntry> plugins;
    QSet<QString> placed;
    auto add = [&](const QString& name, bool active, bool implicit) {
        const auto key = name.toLower();
        if (placed.contains(key) || !onDisk.contains(key)) {
            return;
        }
        placed.insert(key);
        PluginEntry entry;
        entry.name = onDisk.value(key);
        entry.implicit = implicit;
        entry.active = implicit || active;
        readHeaderFlags(FS::PathCombine(m_paths.dataDir, entry.name), entry.master, entry.light);
        entry.masters = readMasters(FS::PathCombine(m_paths.dataDir, entry.name));
        plugins << entry;
    };
    for (const auto& name : implicitNames()) {
        add(name, true, true);
    }
    for (const auto& name : saved.names) {
        add(name, saved.active.contains(name.toLower()), false);
    }
    // new plugins go to the end, inactive, in a stable order
    auto remaining = onDisk.keys();
    std::sort(remaining.begin(), remaining.end());
    for (const auto& key : remaining) {
        add(onDisk.value(key), false, false);
    }

    beginResetModel();
    m_plugins = plugins;
    sortMastersFirst();
    endResetModel();
    return {};
}

void PluginList::sortMastersFirst()
{
    std::stable_partition(m_plugins.begin(), m_plugins.end(), [](const PluginEntry& p) { return p.implicit; });
    auto firstRegular = std::find_if(m_plugins.begin(), m_plugins.end(), [](const PluginEntry& p) { return !p.implicit; });
    std::stable_partition(firstRegular, m_plugins.end(), [](const PluginEntry& p) { return p.master; });
}

Result<> PluginList::save()
{
    QStringList lines{ "# BetterNMM load order. Lines starting with * are active." };
    for (const auto& plugin : m_plugins) {
        if (!plugin.implicit) {
            lines << (plugin.active ? "*" : "") + plugin.name;
        }
    }
    TRY(FS::write(m_paths.orderFile, (lines.join("\r\n") + "\r\n").toUtf8()));
    return writeGameFiles();
}

Result<> PluginList::writeGameFiles() const
{
    if (m_game.pluginFormat == PluginFormat::None || m_paths.gameListDir.isEmpty()) {
        return {};
    }
    QStringList pluginsTxt{ "# This file is used by the game to keep track of your downloaded content." };
    QStringList loadOrderTxt;
    for (const auto& plugin : m_plugins) {
        switch (m_game.pluginFormat) {
            case PluginFormat::Asterisk:
                if (!plugin.implicit) {
                    pluginsTxt << (plugin.active ? "*" : "") + plugin.name;
                }
                break;
            case PluginFormat::Textfile:
                loadOrderTxt << plugin.name;
                if (plugin.active && !plugin.implicit) {
                    pluginsTxt << plugin.name;
                }
                break;
            case PluginFormat::Timestamp:
                if (plugin.active) {
                    pluginsTxt << plugin.name;
                }
                break;
            case PluginFormat::None:
                break;
        }
    }
    TRY(FS::write(FS::PathCombine(m_paths.gameListDir, "plugins.txt"), (pluginsTxt.join("\r\n") + "\r\n").toUtf8()));
    if (m_game.pluginFormat == PluginFormat::Textfile) {
        TRY(FS::write(FS::PathCombine(m_paths.gameListDir, "loadorder.txt"), (loadOrderTxt.join("\r\n") + "\r\n").toUtf8()));
    }
    if (m_game.pluginFormat == PluginFormat::Timestamp) {
        // these games order plugins by modification time: only files out of order are touched
        QDateTime previous;
        for (const auto& plugin : m_plugins) {
            const auto path = FS::PathCombine(m_paths.dataDir, plugin.name);
            QFile file(path);
            const auto current = QFileInfo(path).lastModified(QTimeZone::UTC);
            if (!previous.isValid() || current > previous) {
                previous = current;
                continue;
            }
            previous = previous.addSecs(60);
            if (!file.open(QIODevice::ReadWrite) || !file.setFileTime(previous, QFileDevice::FileModificationTime)) {
                return std::unexpected(tr("Could not set the load order time of %1: %2").arg(plugin.name, file.errorString()));
            }
        }
    }
    return {};
}

void PluginList::setOwners(QHash<QString, QString> owners)
{
    m_owners = std::move(owners);
    if (!m_plugins.isEmpty()) {
        emit dataChanged(index(0, ModColumn), index(static_cast<int>(m_plugins.size()) - 1, ModColumn));
    }
}

void PluginList::setActive(const QList<int>& rows, bool active)
{
    bool any = false;
    for (int row : rows) {
        if (row >= 0 && row < m_plugins.size() && !m_plugins[row].implicit && m_plugins[row].active != active) {
            m_plugins[row].active = active;
            emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
            any = true;
        }
    }
    if (any) {
        // a master turned on or off changes whether the plugins that need it load
        emit dataChanged(index(0, FlagsColumn), index(static_cast<int>(m_plugins.size()) - 1, FlagsColumn));
        changed();
    }
}

PluginList::SlotUse PluginList::slotUse() const
{
    SlotUse use;
    for (const auto& plugin : m_plugins) {
        if (plugin.active || plugin.implicit) {
            (plugin.light && m_game.lightPluginSlots > 0 ? use.light : use.full)++;
        }
    }
    return use;
}

QStringList PluginList::loadProblems() const
{
    QStringList problems;
    for (const auto& plugin : m_plugins) {
        if (const auto problem = masterProblem(plugin); !problem.isEmpty()) {
            problems << tr("%1: %2").arg(plugin.name, problem);
        }
    }
    return problems + slotProblems();
}

QStringList PluginList::slotProblems() const
{
    QStringList problems;
    const auto use = slotUse();
    if (m_game.fullPluginSlots > 0 && use.full > m_game.fullPluginSlots) {
        problems << (m_game.lightPluginSlots > 0
                         ? tr("%1 full plugins are active; %2 loads at most %3. Turn some off, or use light (ESL) versions where "
                              "their mods offer them.")
                         : tr("%1 plugins are active; %2 loads at most %3. Turn some off, or merge them."))
                        .arg(use.full)
                        .arg(m_game.name)
                        .arg(m_game.fullPluginSlots);
    }
    if (m_game.lightPluginSlots > 0 && use.light > m_game.lightPluginSlots) {
        problems << tr("%1 light plugins are active; %2 loads at most %3.").arg(use.light).arg(m_game.name).arg(m_game.lightPluginSlots);
    }
    return problems;
}

QString PluginList::masterProblem(const PluginEntry& plugin) const
{
    if (!plugin.active || plugin.masters.isEmpty()) {
        return {};
    }
    if (m_activeByName.isEmpty()) {
        for (const auto& entry : m_plugins) {
            m_activeByName.insert(entry.name.toLower(), entry.active);
        }
    }
    QStringList missing;
    QStringList inactive;
    for (const auto& master : plugin.masters) {
        const auto found = m_activeByName.constFind(master.toLower());
        if (found == m_activeByName.cend()) {
            missing << master;
        } else if (!*found) {
            inactive << master;
        }
    }
    QStringList problems;
    if (!missing.isEmpty()) {
        problems << tr("Missing master: %1").arg(missing.join(", "));
    }
    if (!inactive.isEmpty()) {
        problems << tr("Inactive master: %1").arg(inactive.join(", "));
    }
    return problems.join("; ");
}

void PluginList::moveRows(QList<int> rows, int offset)
{
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    rows.removeIf([this](int row) { return row < 0 || row >= m_plugins.size() || m_plugins[row].implicit; });
    if (rows.isEmpty() || offset == 0) {
        return;
    }
    const int firstMovable = static_cast<int>(std::count_if(m_plugins.begin(), m_plugins.end(), [](const PluginEntry& p) { return p.implicit; }));
    if (rows.first() + offset < firstMovable || rows.last() + offset >= m_plugins.size()) {
        return;
    }
    beginResetModel();
    QList<PluginEntry> moving;
    for (int i = static_cast<int>(rows.size()) - 1; i >= 0; i--) {
        moving.prepend(m_plugins.takeAt(rows[i]));
    }
    for (int i = 0; i < moving.size(); i++) {
        m_plugins.insert(rows.first() + offset + i, moving[i]);
    }
    sortMastersFirst();
    endResetModel();
    changed();
}

void PluginList::setOrder(const QStringList& names)
{
    QHash<QString, int> position;
    for (int i = 0; i < names.size(); i++) {
        position.insert(names[i].toLower(), i);
    }
    beginResetModel();
    std::stable_sort(m_plugins.begin(), m_plugins.end(), [&position](const PluginEntry& a, const PluginEntry& b) {
        return position.value(a.name.toLower(), INT_MAX) < position.value(b.name.toLower(), INT_MAX);
    });
    sortMastersFirst();
    endResetModel();
    changed();
}

void PluginList::changed()
{
    if (auto saved = save(); !saved) {
        qWarning() << "Could not save the load order:" << saved.error();
    }
}

int PluginList::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_plugins.size());
}

int PluginList::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant PluginList::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_plugins.size()) {
        return {};
    }
    const auto& plugin = m_plugins[index.row()];
    if (role == Qt::CheckStateRole && index.column() == ActiveColumn) {
        return plugin.active ? Qt::Checked : Qt::Unchecked;
    }
    if (role == Qt::ToolTipRole && plugin.implicit) {
        return tr("The game always loads this plugin.");
    }
    if (role == Qt::ToolTipRole || role == Qt::ForegroundRole) {
        const auto problem = masterProblem(plugin);
        if (problem.isEmpty()) {
            return {};
        }
        if (role == Qt::ForegroundRole) {
            return QColor(224, 80, 80);
        }
        return tr("%1. The game crashes while loading with this plugin active: install or activate the plugins it needs, "
                  "or deactivate it.")
            .arg(problem);
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (index.column()) {
        case ActiveColumn:
            return QString::number(index.row());
        case NameColumn:
            return plugin.name;
        case FlagsColumn: {
            QStringList flags;
            if (plugin.master) {
                flags << tr("Master");
            }
            if (plugin.light) {
                flags << tr("Light");
            }
            if (plugin.implicit) {
                flags << tr("Always loaded");
            }
            if (const auto problem = masterProblem(plugin); !problem.isEmpty()) {
                flags << problem;
            }
            return flags.join(", ");
        }
        case ModColumn:
            return m_owners.value(plugin.name.toLower(), tr("Base game"));
        default:
            return {};
    }
}

QVariant PluginList::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
        case ActiveColumn:
            return tr("Index");
        case NameColumn:
            return tr("Plugin");
        case FlagsColumn:
            return tr("Flags");
        case ModColumn:
            return tr("Provided by");
        default:
            return {};
    }
}

Qt::ItemFlags PluginList::flags(const QModelIndex& index) const
{
    auto out = QAbstractTableModel::flags(index);
    if (index.isValid() && index.column() == ActiveColumn && !m_plugins[index.row()].implicit) {
        out |= Qt::ItemIsUserCheckable;
    }
    return out;
}

bool PluginList::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role == Qt::CheckStateRole && index.isValid() && index.column() == ActiveColumn) {
        setActive({ index.row() }, value.toInt() == Qt::Checked);
        return true;
    }
    return false;
}
