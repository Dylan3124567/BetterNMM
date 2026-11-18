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
#include <QHash>

#include "Result.h"
#include "game/GameDefinition.h"

struct PluginEntry {
    QString name;
    bool active = false;
    bool master = false;    ///< .esm/.esl or the master flag in the file header
    bool light = false;     ///< .esl or the light flag
    bool implicit = false;  ///< loaded by the game no matter what (base game, DLC, Creation Club)
    QStringList masters;    ///< the plugins it needs loaded before it
};

/**
 * Plugin load order of an instance. BetterNMM keeps its own copy (loadorder.txt in the instance folder) and writes the
 * game's files from it, because games rewrite plugins.txt themselves and would otherwise lose the user's choices.
 */
class PluginList : public QAbstractTableModel {
    Q_OBJECT
   public:
    enum Column { ActiveColumn, NameColumn, FlagsColumn, ModColumn, ColumnCount };

    struct Paths {
        QString dataDir;        ///< where the game loads plugins from
        QString gameDir;        ///< holds the Creation Club list
        QString orderFile;      ///< BetterNMM's own load order
        QString gameListDir;    ///< folder of the game's plugins.txt (AppData/Local/<game>)
    };

    PluginList(GameDefinition game, Paths paths, QObject* parent = nullptr);

    /// Rescans the data folder and merges it with the saved order
    Result<> refresh();
    /// Saves BetterNMM's order and writes the game's plugin files
    Result<> save();
    /// Writes only the game's plugin files from the current order
    Result<> writeGameFiles() const;

    const QList<PluginEntry>& plugins() const { return m_plugins; }

    /// Active plugins by the slot they take: light ones only in games that have light plugins, all others full
    struct SlotUse {
        int full = 0;
        int light = 0;
    };
    SlotUse slotUse() const;
    /// More active plugins than the game has slots for, said for the person; empty when they fit
    QStringList slotProblems() const;
    /// What makes the game crash or leave plugins out while it loads: active plugins whose masters are missing or
    /// inactive, and slotProblems(). Empty when the load order loads.
    QStringList loadProblems() const;
    const GameDefinition& game() const { return m_game; }
    void setOwners(QHash<QString, QString> owners);  ///< lower-case plugin name -> mod name
    void setActive(const QList<int>& rows, bool active);
    void moveRows(QList<int> rows, int offset);
    /// Puts the plugins in the order of @p names (any not named stay after them) and saves; masters still lead
    void setOrder(const QStringList& names);

    /// Reads the master/light flags from a plugin's header
    static void readHeaderFlags(const QString& path, bool& master, bool& light);
    /// The plugins a plugin needs loaded before it (its MAST entries); empty when it needs none or is unreadable
    static QStringList readMasters(const QString& path);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;

   private:
    struct SavedOrder {
        QStringList names;
        QSet<QString> active;  ///< lower-case
        bool found = false;
    };
    SavedOrder readOrderFile(const QString& path, bool activeMarkers) const;
    QStringList implicitNames() const;
    void sortMastersFirst();
    void changed();
    /// What keeps an active plugin from loading: masters that are not there or not active; empty when it loads
    QString masterProblem(const PluginEntry& plugin) const;

    GameDefinition m_game;
    Paths m_paths;
    QList<PluginEntry> m_plugins;
    QHash<QString, QString> m_owners;
    mutable QHash<QString, bool> m_activeByName;  ///< lower-case name -> active; rebuilt on demand after any change
};
