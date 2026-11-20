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

#include "ModList.h"

#include <QDir>
#include <QDirListing>
#include <QFileInfo>
#include <QFont>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QScopeGuard>
#include <QTimeZone>
#include <QUrl>

#include <climits>

#include <algorithm>

#include "FileSystem.h"

static const QString g_rowsMime = QStringLiteral("application/x-betternmm-mod-rows");

ModList::ModList(QString modsDir, QString listFile, QObject* parent)
    : QAbstractTableModel(parent), m_modsDir(std::move(modsDir)), m_listFile(std::move(listFile))
{}

QString ModList::modDir(const QString& id) const
{
    return FS::PathCombine(m_modsDir, id);
}

Result<> ModList::load()
{
    beginResetModel();
    m_mods.clear();
    auto finish = qScopeGuard([this] {
        endResetModel();
        refreshConflicts();
    });

    if (QFile::exists(m_listFile)) {
        TRY_INTO(auto data, FS::read(m_listFile));
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError) {
            return std::unexpected(tr("%1 is corrupt: %2").arg(m_listFile, error.errorString()));
        }
        for (const auto& value : doc.object().value("mods").toArray()) {
            const auto obj = value.toObject();
            ModEntry entry;
            entry.id = obj.value("id").toString();
            if (entry.id.isEmpty() || !QFileInfo(modDir(entry.id)).isDir()) {
                continue;
            }
            entry.name = obj.value("name").toString(entry.id);
            entry.enabled = obj.value("enabled").toBool();
            entry.version = obj.value("version").toString();
            const auto nexus = obj.value("nexus").toObject();
            entry.nexusGame = nexus.value("game").toString();
            entry.nexusModId = nexus.value("modId").toInt();
            entry.nexusFileId = nexus.value("fileId").toInt();
            entry.latestVersion = obj.value("latestVersion").toString();
            entry.archive = obj.value("archive").toString();
            entry.stamp = obj.value("stamp").toInteger();
            for (const auto& choice : obj.value("choices").toArray()) {
                entry.choices << choice.toVariant().toStringList();
            }
            m_mods << entry;
        }
    }

    // folders put into mods/ by hand become disabled mods
    for (const auto& dirEntry : QDirListing(m_modsDir, QDirListing::IteratorFlag::DirsOnly)) {
        const auto id = dirEntry.fileName();
        if (id.startsWith('.') || indexOf(id) >= 0) {
            continue;
        }
        ModEntry entry;
        entry.id = id;
        entry.name = id;
        entry.stamp = dirEntry.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
        m_mods << entry;
    }
    return {};
}

Result<> ModList::save() const
{
    QJsonArray mods;
    for (const auto& entry : m_mods) {
        QJsonObject obj{ { "id", entry.id }, { "name", entry.name }, { "enabled", entry.enabled }, { "stamp", entry.stamp } };
        if (!entry.version.isEmpty()) {
            obj.insert("version", entry.version);
        }
        if (!entry.latestVersion.isEmpty()) {
            obj.insert("latestVersion", entry.latestVersion);
        }
        if (!entry.archive.isEmpty()) {
            obj.insert("archive", entry.archive);
        }
        if (!entry.choices.isEmpty()) {
            QJsonArray choices;
            for (const auto& choice : entry.choices) {
                choices.append(QJsonArray::fromStringList(choice));
            }
            obj.insert("choices", choices);
        }
        if (entry.nexusModId > 0) {
            obj.insert("nexus", QJsonObject{ { "game", entry.nexusGame }, { "modId", entry.nexusModId }, { "fileId", entry.nexusFileId } });
        }
        mods.append(obj);
    }
    return FS::write(m_listFile, QJsonDocument(QJsonObject{ { "mods", mods } }).toJson(QJsonDocument::Indented));
}

QList<ModEntry> ModList::enabledMods() const
{
    QList<ModEntry> out;
    for (const auto& entry : m_mods) {
        if (entry.enabled) {
            out << entry;
        }
    }
    return out;
}

int ModList::indexOf(const QString& id) const
{
    for (int i = 0; i < m_mods.size(); i++) {
        if (m_mods[i].id.compare(id, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

int ModList::indexOfInstalled(const ModEntry& metadata, const QString& archiveName) const
{
    for (int i = 0; i < m_mods.size(); i++) {
        const auto& existing = m_mods[i];
        const bool sameName = existing.name.compare(metadata.name, Qt::CaseInsensitive) == 0 &&
                              (metadata.nexusModId > 0 ? existing.nexusModId == metadata.nexusModId : existing.nexusModId <= 0);
        const bool sameArchive = metadata.nexusModId <= 0 && !existing.archive.isEmpty() &&
                                 existing.archive.compare(archiveName, Qt::CaseInsensitive) == 0;
        if (sameName || sameArchive) {
            return i;
        }
    }
    return -1;
}

QString ModList::uniqueId(const QString& name) const
{
    auto base = FS::RemoveInvalidFilenameChars(name.trimmed()).trimmed();
    while (base.endsWith('.')) {
        base.chop(1);
    }
    if (base.isEmpty() || base.startsWith('.')) {
        base = "mod" + base;
    }
    auto candidate = base;
    for (int n = 2; indexOf(candidate) >= 0 || QFileInfo::exists(modDir(candidate)); n++) {
        candidate = QString("%1 (%2)").arg(base).arg(n);
    }
    return candidate;
}

void ModList::upsert(const ModEntry& entry)
{
    if (const int row = indexOf(entry.id); row >= 0) {
        const bool enabled = m_mods[row].enabled;
        m_mods[row] = entry;
        m_mods[row].enabled = enabled;
        emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
    } else {
        beginInsertRows({}, static_cast<int>(m_mods.size()), static_cast<int>(m_mods.size()));
        m_mods << entry;
        endInsertRows();
    }
    changed(true);
}

void ModList::setEnabled(const QList<int>& rows, bool enabled)
{
    bool any = false;
    for (int row : rows) {
        if (row >= 0 && row < m_mods.size() && m_mods[row].enabled != enabled) {
            m_mods[row].enabled = enabled;
            emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
            any = true;
        }
    }
    if (any) {
        changed(true);
    }
}

void ModList::moveRows(QList<int> rows, int offset)
{
    if (rows.isEmpty() || offset == 0) {
        return;
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    if (rows.first() + offset < 0 || rows.last() + offset >= m_mods.size()) {
        return;
    }
    beginResetModel();
    QList<ModEntry> moving;
    for (int i = static_cast<int>(rows.size()) - 1; i >= 0; i--) {
        moving.prepend(m_mods.takeAt(rows[i]));
    }
    const int insertAt = rows.first() + offset;
    for (int i = 0; i < moving.size(); i++) {
        m_mods.insert(insertAt + i, moving[i]);
    }
    endResetModel();
    changed(true);
}

void ModList::setOrder(const QStringList& ids)
{
    QHash<QString, int> position;
    for (int i = 0; i < ids.size(); i++) {
        position.insert(ids[i], i);
    }
    auto sorted = m_mods;
    std::stable_sort(sorted.begin(), sorted.end(), [&position](const ModEntry& a, const ModEntry& b) {
        return position.value(a.id, INT_MAX) < position.value(b.id, INT_MAX);
    });
    bool same = true;
    for (int i = 0; i < sorted.size() && same; i++) {
        same = sorted[i].id == m_mods[i].id;
    }
    if (same) {
        return;
    }
    beginResetModel();
    m_mods = sorted;
    endResetModel();
    changed(true);
}

Result<> ModList::remove(const QList<int>& rows)
{
    auto sorted = rows;
    std::sort(sorted.begin(), sorted.end(), std::greater<>());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    bool wasEnabled = false;
    for (int row : sorted) {
        if (row < 0 || row >= m_mods.size()) {
            continue;
        }
        const auto dir = modDir(m_mods[row].id);
        if (!FS::deletePath(dir)) {
            return std::unexpected(tr("Could not delete %1").arg(dir));
        }
        wasEnabled |= m_mods[row].enabled;
        beginRemoveRows({}, row, row);
        m_mods.removeAt(row);
        endRemoveRows();
    }
    changed(wasEnabled);
    return {};
}

void ModList::setLatestVersion(const QString& id, const QString& version)
{
    if (const int row = indexOf(id); row >= 0) {
        m_mods[row].latestVersion = version;
        emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
        changed(false);
    }
}

void ModList::refreshConflicts()
{
    m_overrides.clear();
    m_overriddenBy.clear();
    QHash<QString, QString> owner;  // lower-case relative path -> mod id
    for (const auto& entry : m_mods) {
        if (!entry.enabled) {
            continue;
        }
        const QDir root(modDir(entry.id));
        for (const auto& file : QDirListing(root.path(), QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly |
                                                             QDirListing::IteratorFlag::IncludeHidden)) {
            const auto key = root.relativeFilePath(file.absoluteFilePath()).toLower();
            if (const auto previous = owner.value(key); !previous.isEmpty() && previous != entry.id) {
                if (!m_overrides[entry.id].contains(previous)) {
                    m_overrides[entry.id] << previous;
                }
                if (!m_overriddenBy[previous].contains(entry.id)) {
                    m_overriddenBy[previous] << entry.id;
                }
            }
            owner.insert(key, entry.id);
        }
    }
    if (!m_mods.isEmpty()) {
        emit dataChanged(index(0, ConflictColumn), index(static_cast<int>(m_mods.size()) - 1, ConflictColumn));
    }
}

QList<ModList::Overwrite> ModList::overwrites() const
{
    QHash<QString, QString> owner;  // lower-case relative path -> mod id providing it so far
    QHash<QString, QString> original;  // lower-case relative path -> as written
    QMap<std::pair<QString, QString>, Overwrite> pairs;
    for (const auto& entry : m_mods) {
        if (!entry.enabled) {
            continue;
        }
        const QDir root(modDir(entry.id));
        for (const auto& file : QDirListing(root.path(), QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly |
                                                             QDirListing::IteratorFlag::IncludeHidden)) {
            const auto rel = root.relativeFilePath(file.absoluteFilePath());
            const auto key = rel.toLower();
            if (const auto previous = owner.value(key); !previous.isEmpty() && previous != entry.id) {
                auto& pair = pairs[{ entry.id, previous }];
                pair.winner = entry.id;
                pair.loser = previous;
                pair.files++;
                if (pair.examples.size() < 5) {
                    pair.examples << rel;
                }
            }
            owner.insert(key, entry.id);
        }
    }
    auto list = pairs.values();
    std::stable_sort(list.begin(), list.end(), [](const Overwrite& a, const Overwrite& b) { return a.files > b.files; });
    return list;
}

QList<std::pair<QString, QString>> ModList::sharingPairs(const QString& modsDir, const QStringList& enabledIds)
{
    QHash<QString, QStringList> providers;  // lower-case relative path -> enabled mods with it, in priority order
    for (const auto& id : enabledIds) {
        const QDir root(FS::PathCombine(modsDir, id));
        for (const auto& file : QDirListing(root.path(), QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly |
                                                             QDirListing::IteratorFlag::IncludeHidden)) {
            auto& mods = providers[root.relativeFilePath(file.absoluteFilePath()).toLower()];
            if (mods.isEmpty() || mods.last() != id) {
                mods << id;
            }
        }
    }
    QSet<std::pair<QString, QString>> seen;
    QList<std::pair<QString, QString>> pairs;
    for (const auto& mods : std::as_const(providers)) {
        for (qsizetype later = 1; later < mods.size(); later++) {
            for (qsizetype earlier = 0; earlier < later; earlier++) {
                const std::pair pair{ mods[later], mods[earlier] };
                if (!seen.contains(pair)) {
                    seen.insert(pair);
                    pairs << pair;
                }
            }
        }
    }
    return pairs;
}

void ModList::changed(bool affectsDeployment)
{
    if (auto saved = save(); !saved) {
        qWarning() << "Could not save the mod list:" << saved.error();
    }
    if (affectsDeployment) {
        refreshConflicts();
        emit deploymentChanged();
    }
}

int ModList::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_mods.size());
}

int ModList::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ModList::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_mods.size()) {
        return {};
    }
    const auto& entry = m_mods[index.row()];
    auto names = [this](const QStringList& ids) {
        QStringList out;
        for (const auto& id : ids) {
            const int row = indexOf(id);
            out << (row >= 0 ? m_mods[row].name : id);
        }
        return out;
    };
    switch (role) {
        case Qt::CheckStateRole:
            if (index.column() == EnabledColumn) {
                return entry.enabled ? Qt::Checked : Qt::Unchecked;
            }
            return {};
        case Qt::DisplayRole:
            switch (index.column()) {
                case NameColumn:
                    return entry.name;
                case VersionColumn:
                    return entry.version;
                case LatestColumn:
                    return entry.latestVersion;
                case ConflictColumn: {
                    QStringList parts;
                    if (const auto wins = m_overrides.value(entry.id); !wins.isEmpty()) {
                        parts << tr("Overrides %n mod(s)", nullptr, static_cast<int>(wins.size()));
                    }
                    if (const auto loses = m_overriddenBy.value(entry.id); !loses.isEmpty()) {
                        parts << tr("Overridden by %n mod(s)", nullptr, static_cast<int>(loses.size()));
                    }
                    return parts.join(", ");
                }
                case SourceColumn:
                    return entry.nexusModId > 0 ? tr("Nexus #%1").arg(entry.nexusModId) : tr("Local archive");
                case InstalledColumn:
                    return entry.stamp > 0 ? QDateTime::fromMSecsSinceEpoch(entry.stamp).toString(QLocale().dateTimeFormat(QLocale::ShortFormat))
                                           : QString();
                default:
                    return {};
            }
        case Qt::ToolTipRole: {
            QStringList lines;
            if (const auto wins = m_overrides.value(entry.id); !wins.isEmpty()) {
                lines << tr("Replaces files of: %1").arg(names(wins).join(", "));
            }
            if (const auto loses = m_overriddenBy.value(entry.id); !loses.isEmpty()) {
                lines << tr("Files replaced by: %1").arg(names(loses).join(", "));
            }
            if (entry.hasUpdate()) {
                lines << tr("Version %1 is available on Nexus Mods").arg(entry.latestVersion);
            }
            if (!entry.archive.isEmpty()) {
                lines << tr("Installed from %1").arg(entry.archive);
            }
            return lines.join('\n');
        }
        case Qt::FontRole:
            if (index.column() == LatestColumn && entry.hasUpdate()) {
                QFont font;
                font.setBold(true);
                return font;
            }
            return {};
        default:
            return {};
    }
}

QVariant ModList::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
        case EnabledColumn:
            return tr("Enable");
        case NameColumn:
            return tr("Name");
        case VersionColumn:
            return tr("Version");
        case LatestColumn:
            return tr("Latest");
        case ConflictColumn:
            return tr("Conflicts");
        case SourceColumn:
            return tr("Source");
        case InstalledColumn:
            return tr("Installed");
        default:
            return {};
    }
}

Qt::ItemFlags ModList::flags(const QModelIndex& index) const
{
    auto out = QAbstractTableModel::flags(index) | Qt::ItemIsDropEnabled;
    if (!index.isValid()) {
        return out;
    }
    out |= Qt::ItemIsDragEnabled;
    if (index.column() == EnabledColumn) {
        out |= Qt::ItemIsUserCheckable;
    } else if (index.column() == NameColumn) {
        out |= Qt::ItemIsEditable;
    }
    return out;
}

bool ModList::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.row() >= m_mods.size()) {
        return false;
    }
    if (role == Qt::CheckStateRole && index.column() == EnabledColumn) {
        setEnabled({ index.row() }, value.toInt() == Qt::Checked);
        return true;
    }
    if (role == Qt::EditRole && index.column() == NameColumn) {
        const auto name = value.toString().trimmed();
        if (name.isEmpty()) {
            return false;
        }
        m_mods[index.row()].name = name;
        emit dataChanged(index, index);
        changed(false);
        return true;
    }
    return false;
}

Qt::DropActions ModList::supportedDropActions() const
{
    return Qt::MoveAction | Qt::CopyAction;
}

QStringList ModList::mimeTypes() const
{
    return { g_rowsMime, "text/uri-list" };
}

QMimeData* ModList::mimeData(const QModelIndexList& indexes) const
{
    QSet<int> rows;
    for (const auto& index : indexes) {
        rows.insert(index.row());
    }
    QStringList encoded;
    for (int row : rows) {
        encoded << QString::number(row);
    }
    auto* data = new QMimeData();
    data->setData(g_rowsMime, encoded.join(',').toUtf8());
    return data;
}

bool ModList::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int /*column*/, const QModelIndex& parent)
{
    if (action == Qt::IgnoreAction) {
        return true;
    }
    if (data->hasUrls() && !data->hasFormat(g_rowsMime)) {
        emit filesDropped(data->urls());
        return true;
    }
    if (!data->hasFormat(g_rowsMime)) {
        return false;
    }
    int target = row >= 0 ? row : (parent.isValid() ? parent.row() : static_cast<int>(m_mods.size()));
    QList<int> rows;
    for (const auto& part : QString::fromUtf8(data->data(g_rowsMime)).split(',', Qt::SkipEmptyParts)) {
        rows << part.toInt();
    }
    if (rows.isEmpty()) {
        return false;
    }
    std::sort(rows.begin(), rows.end());
    // rows above the drop target shift it up once they are taken out
    const int above = static_cast<int>(std::count_if(rows.begin(), rows.end(), [target](int r) { return r < target; }));
    target = std::clamp(target - above, 0, static_cast<int>(m_mods.size() - rows.size()));
    beginResetModel();
    QList<ModEntry> moving;
    for (int i = static_cast<int>(rows.size()) - 1; i >= 0; i--) {
        moving.prepend(m_mods.takeAt(rows[i]));
    }
    for (int i = 0; i < moving.size(); i++) {
        m_mods.insert(target + i, moving[i]);
    }
    endResetModel();
    changed(true);
    return false;  // the move is done here, the view must not remove the source rows
}
