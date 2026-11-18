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

#include "ui/pages/BasePage.h"
#include "ui/widgets/GameSettingsWidget.h"

/// Global defaults for launching games; instances can override them
class GamesPage : public GameSettingsWidget, public BasePage {
    Q_OBJECT

   public:
    explicit GamesPage(QWidget* parent = nullptr) : GameSettingsWidget(nullptr, parent) {}
    ~GamesPage() override = default;

    QString displayName() const override { return tr("Games"); }
    QIcon icon() const override { return QIcon::fromTheme("launch"); }
    QString id() const override { return "game-settings"; }
    QString helpPage() const override { return "Game-settings"; }
    bool apply() override
    {
        saveSettings();
        return true;
    }
};
