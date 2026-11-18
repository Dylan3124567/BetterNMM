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

#include <QWidget>

#include "ui/pages/BasePage.h"

class GameInstance;
class QLabel;
class QLineEdit;
class QPushButton;

/// What the instance's game folder shares with the Steam installation, and keeping it in sync
class GameFilesPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    explicit GameFilesPage(GameInstance* instance, QWidget* parent = nullptr);

    QString displayName() const override { return tr("Game Files"); }
    QIcon icon() const override { return QIcon::fromTheme("viewfolder"); }
    QString id() const override { return "gamefiles"; }
    QString helpPage() const override { return "Game-Files"; }
    bool apply() override;
    void openedImpl() override;

   private:
    void refreshStats();
    void resync();

    GameInstance* m_instance;
    QLabel* m_stats = nullptr;
    QLineEdit* m_local = nullptr;
    QLineEdit* m_exclude = nullptr;
};
