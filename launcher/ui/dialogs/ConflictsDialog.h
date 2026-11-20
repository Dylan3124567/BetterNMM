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

#include <QDialog>

class ConflictFiles;
class ConflictFilter;
class ModList;
class QLabel;
class QPushButton;
class QTreeView;

/// The files of some mods, or of every enabled mod: which mod's copy the game gets, which other mods have the file too,
/// and a way to hide a file so it is not deployed at all
class ConflictsDialog : public QDialog {
    Q_OBJECT
   public:
    /// @p ids: the mods whose files are shown; every enabled mod when empty
    ConflictsDialog(ModList* mods, QStringList ids, QWidget* parent = nullptr);

   private:
    void reload();
    void setSelectedHidden(bool hidden);
    void updateButtons();

    ModList* m_mods;
    QStringList m_ids;
    ConflictFiles* m_files;
    ConflictFilter* m_filter;
    QTreeView* m_view;
    QLabel* m_summary;
    QPushButton* m_hide;
    QPushButton* m_unhide;
};
