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

#include "game/HealthCheck.h"
#include "ui/pages/instance/ListPage.h"

class GameInstance;
class QStandardItemModel;

/// What keeps the instance's mods from working, with a fix for each problem: plugins whose masters do not load before
/// them, and Nexus Mods requirements of enabled mods that are not installed or not enabled
class HealthPage : public ListPage {
    Q_OBJECT
   public:
    explicit HealthPage(GameInstance* instance, QWidget* parent = nullptr);

    QString displayName() const override { return tr("Health"); }
    QIcon icon() const override { return QIcon::fromTheme("status-good"); }
    QString id() const override { return "health"; }
    QString helpPage() const override { return "Health"; }
    void openedImpl() override;

   private:
    /// Checks again; asks Nexus Mods for the mods' requirements when @p askNexus, else uses what it said last time
    void check(bool askNexus);
    void fixSelected();
    void openPages();
    QList<HealthCheck::Problem> selectedProblems() const;
    void updateActions();

    GameInstance* m_instance;
    QStandardItemModel* m_model;
    QList<HealthCheck::Problem> m_problems;
    QList<Nexus::ModDetails> m_details;
    bool m_askedNexus = false;
    QString m_nexusError;

    QAction* m_fix = nullptr;
    QAction* m_page = nullptr;
};
