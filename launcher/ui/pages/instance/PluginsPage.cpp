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

#include "PluginsPage.h"

#include <QHeaderView>
#include <QMessageBox>
#include <QTreeView>

#include "DesktopServices.h"
#include "game/Deployer.h"
#include "game/GameInstance.h"
#include "game/ModList.h"
#include "game/PluginList.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/widgets/WideBar.h"

PluginsPage::PluginsPage(GameInstance* instance, QWidget* parent) : ListPage(parent), m_instance(instance), m_plugins(instance->pluginList())
{
    setModel(m_plugins, PluginList::NameColumn);
    // names stay readable in a narrow window; the other columns scroll instead
    m_view->header()->setSectionResizeMode(PluginList::NameColumn, QHeaderView::ResizeToContents);
    m_view->header()->setStretchLastSection(true);
    m_view->setToolTip(tr("Plugins load from top to bottom; later plugins override earlier ones. The game always loads masters first."));

    auto* enable = addAction(tr("&Enable"), tr("Activate the selected plugins."), "status-good");
    auto* disable = addAction(tr("&Disable"), tr("Deactivate the selected plugins."), "status-bad");
    addSeparator();
    auto* up = addAction(tr("Move &Up"), tr("Load the selected plugins earlier."));
    auto* down = addAction(tr("Move D&own"), tr("Load the selected plugins later."));
    addSeparator();
    auto* reload = addAction(tr("&Refresh"), tr("Rescan the data folder for plugins."), "refresh");
    auto* folder = addAction(tr("View &Folder"), tr("Open the folder with the game's plugins.txt."), "viewfolder");

    connect(enable, &QAction::triggered, this, [this] { m_plugins->setActive(selectedRows(), true); updateCounts(); });
    connect(disable, &QAction::triggered, this, [this] { m_plugins->setActive(selectedRows(), false); updateCounts(); });
    connect(up, &QAction::triggered, this, [this] { m_plugins->moveRows(selectedRows(), -1); });
    connect(down, &QAction::triggered, this, [this] { m_plugins->moveRows(selectedRows(), 1); });
    connect(reload, &QAction::triggered, this, &PluginsPage::refresh);
    connect(folder, &QAction::triggered, this, [this] { DesktopServices::openPath(m_instance->pluginListDir(), true); });
    connect(m_plugins, &QAbstractItemModel::dataChanged, this, &PluginsPage::updateCounts);
    connect(m_plugins, &QAbstractItemModel::modelReset, this, &PluginsPage::updateCounts);
    connect(m_instance, &BaseInstance::runningStatusChanged, this, [this](bool running) {
        m_view->setEnabled(!running);
        m_toolbar->setEnabled(!running);
    });
    updateCounts();
}

bool PluginsPage::shouldDisplay() const
{
    return m_instance->definition().hasPlugins();
}

void PluginsPage::openedImpl()
{
    refresh();
}

void PluginsPage::refresh()
{
    if (auto refreshed = m_plugins->refresh(); !refreshed) {
        CustomMessageBox::selectable(this, tr("Plugins"), refreshed.error(), QMessageBox::Critical)->exec();
        return;
    }
    // show which mod provides each plugin
    QHash<QString, QString> owners;
    if (auto manifest = DeploymentManifest::load(m_instance->deploymentManifestPath())) {
        const auto* mods = m_instance->modList();
        for (const auto& file : manifest->files) {
            const auto name = file.path.mid(file.path.lastIndexOf('/') + 1).toLower();
            const int row = mods->indexOf(file.mod);
            owners.insert(name, row >= 0 ? mods->mods()[row].name : file.mod);
        }
    }
    m_plugins->setOwners(owners);
    for (int c = 0; c < PluginList::ColumnCount; c++) {
        if (c != PluginList::NameColumn) {
            m_view->resizeColumnToContents(c);
        }
    }
    updateCounts();
}

void PluginsPage::updateCounts()
{
    int active = 0;
    for (const auto& plugin : m_plugins->plugins()) {
        active += plugin.active ? 1 : 0;
    }
    auto text = tr("%1 plugins, %2 active").arg(m_plugins->plugins().size()).arg(active);
    const auto& game = m_plugins->game();
    if (game.fullPluginSlots > 0) {
        // the base game and Creation Club plugins take slots too
        const auto use = m_plugins->slotUse();
        auto count = [](int used, int limit) {
            const auto shown = QString("%1/%2").arg(used).arg(limit);
            return used > limit ? QString("<b style=\"color:#e04040\">%1</b>").arg(shown) : shown;
        };
        text = text.toHtmlEscaped() + " &middot; " +
               (game.lightPluginSlots > 0 ? tr("slots: %1 full, %2 light").arg(count(use.full, game.fullPluginSlots), count(use.light, game.lightPluginSlots))
                                          : tr("slots: %1").arg(count(use.full, game.fullPluginSlots)));
    }
    setCounts(text);
}
