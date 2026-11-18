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
class QStandardItemModel;

/// Lists the instance's saves. It never writes to them: saves are only read, to show them.
class SavesPage : public ListPage {
    Q_OBJECT
   public:
    explicit SavesPage(GameInstance* instance, QWidget* parent = nullptr);

    QString displayName() const override { return tr("Saves"); }
    QIcon icon() const override { return QIcon::fromTheme("saves"); }
    QString id() const override { return "saves"; }
    QString helpPage() const override { return "Saves"; }
    bool shouldDisplay() const override;
    void openedImpl() override;

   private:
    void refresh();

    GameInstance* m_instance;
    QStandardItemModel* m_model;
};
