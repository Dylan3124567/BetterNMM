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

#include "ui/pages/instance/ListPage.h"

class GameInstance;
class ModList;

/// The instance's mods: install, enable, order and update them
class ModsPage : public ListPage {
    Q_OBJECT
   public:
    explicit ModsPage(GameInstance* instance, QWidget* parent = nullptr);

    QString displayName() const override { return tr("Mods"); }
    QIcon icon() const override { return QIcon::fromTheme("mods"); }
    QString id() const override { return "mods"; }
    QString helpPage() const override { return "Mods"; }
    void openedImpl() override;

   private:
    void installFiles(const QList<QUrl>& urls);
    void scheduleDeploy();
    void checkForUpdates();
    void updateSelected();
    void updateActions();
    void updateCounts();
    /// Moves the selection one place up (-1) or down (1) among the rows that can be seen: a collapsed separator's mods are
    /// passed over as a whole
    void moveSelection(int direction);
    void addModSeparator();
    void exportList();
    void reviewChangedFiles();
    /// The files of the selected mods, or of every enabled mod when none is selected, with which mod wins each
    void showConflicts();

    GameInstance* m_instance;
    ModList* m_mods;
    bool m_deployScheduled = false;

    QAction* m_enable = nullptr;
    QAction* m_disable = nullptr;
    QAction* m_remove = nullptr;
    QAction* m_up = nullptr;
    QAction* m_down = nullptr;
    QAction* m_update = nullptr;
    QAction* m_nexusPage = nullptr;
    QAction* m_ignoreUpdates = nullptr;
};
