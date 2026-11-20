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

#include <QAbstractTableModel>
#include <QDateTime>
#include <QHash>
#include <QSet>

#include "Result.h"

/// One installed mod: its files live in mods/<id>/, laid out relative to the game folder
struct ModEntry {
    QString id;
    QString name;
    bool enabled = false;
    QString version;
    QString nexusGame;
    int nexusModId = 0;
    int nexusFileId = 0;
    QString latestVersion;  ///< from the last update check
    QString archive;        ///< file name of the archive it was installed from
    qint64 stamp = 0;       ///< install time in ms since epoch; changes when the files change
    QList<QStringList> choices;  ///< options picked in its FOMOD installer, as {step, group, option} names
    bool ignoreUpdates = false;  ///< newer versions are not pointed out or installed by Update
    bool separator = false;      ///< a heading that groups the mods below it; it has no folder and no files
    bool collapsed = false;      ///< a separator whose mods are hidden in the list

    bool hasUpdate() const { return !ignoreUpdates && !latestVersion.isEmpty() && !version.isEmpty() && latestVersion != version; }
};

/// The mods of an instance in priority order: a later mod's files win over an earlier mod's
class ModList : public QAbstractTableModel {
    Q_OBJECT
   public:
    enum Column { EnabledColumn, NameColumn, VersionColumn, LatestColumn, ConflictColumn, SourceColumn, InstalledColumn, ColumnCount };

    ModList(QString modsDir, QString listFile, QObject* parent = nullptr);

    Result<> load();
    Result<> save() const;

    QString modsDir() const { return m_modsDir; }
    QString modDir(const QString& id) const;
    const QList<ModEntry>& mods() const { return m_mods; }
    QList<ModEntry> enabledMods() const;
    int indexOf(const QString& id) const;
    /// The installed mod that installing @p metadata from @p archiveName replaces: the same Nexus mod with the same name,
    /// or for a file without Nexus details (from disk) the mod installed from a file of that name; -1 when none
    int indexOfInstalled(const ModEntry& metadata, const QString& archiveName) const;

    /// A folder name under mods/ not used yet, derived from @p name
    QString uniqueId(const QString& name) const;

    /// Adds a mod or replaces the one with the same id (keeping its position and enabled state)
    void upsert(const ModEntry& entry);
    void setEnabled(const QList<int>& rows, bool enabled);
    /// Moves the rows by @p offset positions (negative = higher priority loses, i.e. up)
    void moveRows(QList<int> rows, int offset);
    /// Puts the mods in the order of @p ids (any not named stay after them)
    void setOrder(const QStringList& ids);
    /// Deletes the mods and their files; a separator goes alone, its mods stay
    Result<> remove(const QList<int>& rows);
    void setLatestVersion(const QString& id, const QString& version);
    void setIgnoreUpdates(const QList<int>& rows, bool ignore);

    /// Inserts a separator called @p name before @p row (at the end when @p row is out of range); returns its row
    int addSeparator(const QString& name, int row);
    void setCollapsed(int row, bool collapsed);
    /// Whether the entry at @p row is a mod under a collapsed separator
    bool hiddenByCollapse(int row) const;
    /// @p rows plus the mods of the collapsed separators among them, which move with them; sorted
    QList<int> withGroups(const QList<int>& rows) const;
    /// The last row of the group that starts at @p row: the separator's last mod, or @p row itself for a mod
    int groupEnd(int row) const;
    /// Mods in the list, separators left out
    int modCount() const;

    /// Recomputes which mods replace each other's files
    void refreshConflicts();

    /// Two enabled mods with the same files: the winner's copies are the ones deployed
    struct Overwrite {
        QString winner;  ///< mod id
        QString loser;   ///< mod id
        int files = 0;
        QStringList examples;  ///< a few of the files, relative to the mod folder
    };
    /// Every pair of enabled mods that share files, the most shared files first
    QList<Overwrite> overwrites() const;
    /// Every pair of the mods @p enabledIds (in priority order, folders in @p modsDir) that have a file in common, the one
    /// that wins now first. Unlike overwrites() the pairs do not depend on the order: three mods with the same file are
    /// three pairs. Reads only the folders, so it can run on any thread.
    static QList<std::pair<QString, QString>> sharingPairs(const QString& modsDir, const QStringList& enabledIds);
    /// Ending of a mod's file that the person hid: it stays in the mod folder, but is not deployed and is in no conflict
    static inline const QString HiddenSuffix = QStringLiteral(".mohidden");
    static bool isHidden(const QString& path) { return path.endsWith(HiddenSuffix, Qt::CaseInsensitive); }

    /// One file of a mod, as the conflicts view shows it
    struct ModFile {
        QString path;        ///< relative to the mod folder, without HiddenSuffix
        QString mod;         ///< id
        QStringList others;  ///< the other enabled mods with this file and it not hidden, lowest priority first
        bool wins = false;   ///< this mod's copy is the one deployed
        bool hidden = false;
    };
    /// The files of the mods @p ids, or of every enabled mod when it is empty, with which mods have each and which wins
    QList<ModFile> files(const QStringList& ids = {}) const;
    /// Hides (renames to <path>.mohidden) or shows again the files {mod id, path relative to its folder without the
    /// suffix}; the mods are redeployed once. Files that can not be renamed are named in the error, the others are done.
    Result<> setFilesHidden(const QList<std::pair<QString, QString>>& files, bool hidden);

    QStringList overrides(const QString& id) const { return m_overrides.value(id); }
    QStringList overriddenBy(const QString& id) const { return m_overriddenBy.value(id); }

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;

   signals:
    /// The enabled set or the order changed, so the instance needs to be redeployed
    void deploymentChanged();
    /// Files were dropped on the list from outside
    void filesDropped(const QList<QUrl>& urls);

   private:
    void changed(bool affectsDeployment);

    QString m_modsDir;
    QString m_listFile;
    QList<ModEntry> m_mods;
    QHash<QString, QStringList> m_overrides;
    QHash<QString, QStringList> m_overriddenBy;
};
