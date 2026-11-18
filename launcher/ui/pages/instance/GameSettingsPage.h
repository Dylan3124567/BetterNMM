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

#include "game/GameInstance.h"
#include "ui/pages/BasePage.h"
#include "ui/widgets/GameSettingsWidget.h"

/// The instance's launch settings and game definition
class GameSettingsPage : public GameSettingsWidget, public BasePage {
    Q_OBJECT

   public:
    explicit GameSettingsPage(GameInstance* instance, QWidget* parent = nullptr) : GameSettingsWidget(instance, parent) {}
    ~GameSettingsPage() override = default;

    QString displayName() const override { return tr("Settings"); }
    QIcon icon() const override { return QIcon::fromTheme("instance-settings"); }
    QString id() const override { return "settings"; }
    QString helpPage() const override { return "Instance-settings"; }
    bool apply() override
    {
        if (!confirmIniChanges()) {
            return false;  // keeps the window open with the edits
        }
        saveSettings();
        return true;
    }
    void openedImpl() override { loadSettings(); }
    // opening the page reloads it (the Launch list and INI files change meanwhile), so edits are kept when it is left
    void closedImpl() override { saveSettings(); }
};
