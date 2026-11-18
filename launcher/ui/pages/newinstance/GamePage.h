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

#include "game/GameDefinition.h"
#include "ui/pages/BasePage.h"

class NewInstanceDialog;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QRadioButton;

/// New instance from a game installation: one Steam found, or any folder with any Nexus Mods game
class GamePage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    explicit GamePage(NewInstanceDialog* dialog, QWidget* parent = nullptr);

    QString displayName() const override { return tr("Game"); }
    QIcon icon() const override { return QIcon::fromTheme("launch"); }
    QString id() const override { return "game"; }
    QString helpPage() const override { return "Game-instance"; }
    void openedImpl() override;

    /// Whether the script extender should be installed into the new instance once it exists
    bool installScriptExtender() const;

   private:
    void populate();
    void gameSelected();
    void findNexusGame();
    void update();

    NewInstanceDialog* m_dialog;
    QListWidget* m_games = nullptr;
    QLineEdit* m_folder = nullptr;
    QLineEdit* m_domain = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_executable = nullptr;
    QRadioButton* m_isolated = nullptr;
    QRadioButton* m_main = nullptr;
    QCheckBox* m_installExtender = nullptr;
    QLabel* m_info = nullptr;
    int m_appId = 0;
    bool m_populated = false;
};
