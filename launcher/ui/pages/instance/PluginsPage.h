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
class PluginList;

/// Plugin load order of Bethesda games
class PluginsPage : public ListPage {
    Q_OBJECT
   public:
    explicit PluginsPage(GameInstance* instance, QWidget* parent = nullptr);

    QString displayName() const override { return tr("Plugins"); }
    QIcon icon() const override { return QIcon::fromTheme("plugins"); }
    QString id() const override { return "plugins"; }
    QString helpPage() const override { return "Plugins"; }
    bool shouldDisplay() const override;
    void openedImpl() override;

   private:
    void refresh();
    void updateCounts();

    GameInstance* m_instance;
    PluginList* m_plugins;
};
