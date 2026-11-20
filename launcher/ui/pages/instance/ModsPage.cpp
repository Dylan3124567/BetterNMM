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

#include "ModsPage.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>
#include <QTimer>
#include <QTreeView>
#include <QUrl>

#include "Application.h"
#include "DesktopServices.h"
#include "FileSystem.h"
#include "game/GameDefinition.h"
#include "game/Deployer.h"
#include "game/GameInstance.h"
#include "game/ModList.h"
#include "game/ModListExport.h"
#include "game/PluginList.h"
#include "nexus/NexusAccount.h"
#include "nexus/NexusApi.h"
#include "settings/SettingsObject.h"
#include "ui/dialogs/ConflictsDialog.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ModInstallFlow.h"
#include "ui/dialogs/NexusBrowseDialog.h"
#include "ui/widgets/WideBar.h"

ModsPage::ModsPage(GameInstance* instance, QWidget* parent) : ListPage(parent), m_instance(instance), m_mods(instance->modList())
{
    setModel(m_mods, ModList::NameColumn);
    m_view->setDragDropMode(QAbstractItemView::DragDrop);
    m_view->setDefaultDropAction(Qt::MoveAction);
    m_view->setDragEnabled(true);
    m_view->setAcceptDrops(true);
    m_view->setDropIndicatorShown(true);
    // names stay readable in a narrow window; the other columns scroll instead
    m_view->header()->setSectionResizeMode(ModList::NameColumn, QHeaderView::ResizeToContents);
    m_view->header()->setStretchLastSection(true);
    m_view->setToolTip(tr("Mods lower in the list win when two mods have the same file. Drag mods to change their order."));

    auto* install = addAction(tr("&Install File"), tr("Install a mod archive (zip, 7z, rar) you downloaded."), "new");
    auto* browse = addAction(tr("&Browse Nexus"), tr("Search Nexus Mods for this game."), "centralmods");
    auto* extender = addAction(tr("Install &Script Extender"), tr("Install the script extender build made for this game's version."), "plugins");
    extender->setVisible(GameRegistry::byDomain(m_instance->settings()->get("GameDomain").toString()).scriptExtenderMod > 0);
    connect(extender, &QAction::triggered, this, [this] { ModInstallFlow::installScriptExtender(m_instance, this); });
    addSeparator();
    m_enable = addAction(tr("&Enable"), tr("Enable the selected mods."), "status-good");
    m_disable = addAction(tr("&Disable"), tr("Disable the selected mods."), "status-bad");
    m_remove = addAction(tr("&Remove"), tr("Delete the selected mods."), "delete");
    auto* separator = addAction(tr("Add Se&parator"), tr("Insert a heading above the selected mod that groups the mods below it. Click "
                                                       "its arrow to hide or show them; double-click its name to rename it."));
    addSeparator();
    m_up = addAction(tr("Move &Up"), tr("Lower the priority of the selected mods."));
    m_down = addAction(tr("Move D&own"), tr("Raise the priority of the selected mods: their files win over the mods above."));
    auto* autoSort = addAction(tr("&Auto Sort"),
                               tr("Sort the plugins with LOOT's rules and masterlist, and order mods that have the same files: a mod "
                                  "wins over the mods it needs, then the mod whose plugin loads later wins."),
                               "refresh");
    auto* patches = addAction(tr("Check &Patches"), tr("Find compatibility patches in your mods' installers that the other mods you have "
                                                       "call for, and add them."));
    connect(patches, &QAction::triggered, this, [this] { ModInstallFlow::checkPatches(m_instance, this); });
    addSeparator();
    auto* check = addAction(tr("Check for &Updates"), tr("Ask Nexus Mods for the newest version of every mod installed from it."), "refresh");
    m_update = addAction(tr("U&pdate"), tr("Install the newest file of the selected mods."));
    m_ignoreUpdates = addAction(tr("&Ignore Updates"), tr("Stop pointing out newer versions of the selected mods, and leave them out of "
                                                         "Update. Choose it again to stop ignoring them."));
    m_ignoreUpdates->setCheckable(true);
    m_nexusPage = addAction(tr("View on &Nexus"), tr("Open the selected mods' pages on Nexus Mods."));
    auto* folder = addAction(tr("View &Folder"), tr("Open the folder of the selected mod, or of all mods."), "viewfolder");
    auto* redeploy = addAction(tr("Re&deploy"), tr("Make the game folder match the enabled mods again."));
    auto* conflicts = addAction(tr("Con&flicts..."), tr("See which mod wins each file the selected mods (or all enabled mods) "
                                                       "share with others, and hide files you do not want deployed."));
    auto* changed = addAction(tr("Changed F&iles..."), tr("Mod files that xEdit or another tool replaced in the game folder: keep "
                                                         "the change with its mod, or put the mod's file back."));
    auto* exportList = addAction(tr("E&xport List..."), tr("Save the mods (with versions and Nexus Mods pages) and the load order as "
                                                          "Markdown, CSV or text, to share them."));

    connect(install, &QAction::triggered, this, [this] {
        QList<QUrl> urls;
        for (const auto& file : ModInstallFlow::pickArchives(m_instance, this)) {
            urls << QUrl::fromLocalFile(file);
        }
        installFiles(urls);
    });
    connect(browse, &QAction::triggered, this, [this] {
        NexusBrowseDialog dialog(m_instance, this);
        dialog.exec();
    });
    connect(separator, &QAction::triggered, this, &ModsPage::addModSeparator);
    connect(m_ignoreUpdates, &QAction::triggered, this, [this](bool ignore) { m_mods->setIgnoreUpdates(selectedRows(), ignore); updateActions(); });
    m_proxy->setRowHider([this](int row) { return m_mods->hiddenByCollapse(row); });
    connect(m_view, &QAbstractItemView::clicked, this, [this](const QModelIndex& index) {
        const auto source = m_proxy->mapToSource(index);
        if (source.column() == ModList::EnabledColumn && source.row() < m_mods->mods().size() && m_mods->mods()[source.row()].separator) {
            m_mods->setCollapsed(source.row(), !m_mods->mods()[source.row()].collapsed);
        }
    });
    connect(m_enable, &QAction::triggered, this, [this] { m_mods->setEnabled(selectedRows(), true); });
    connect(m_disable, &QAction::triggered, this, [this] { m_mods->setEnabled(selectedRows(), false); });
    connect(m_remove, &QAction::triggered, this, [this] {
        QStringList ids;
        QStringList modNames;
        int separators = 0;
        for (int row : selectedRows()) {
            const auto& entry = m_mods->mods()[row];
            ids << entry.id;
            if (entry.separator) {
                separators++;
            } else {
                modNames << entry.name;
            }
        }
        if (ids.isEmpty()) {
            return;
        }
        QString question;
        if (modNames.isEmpty()) {
            question = tr("Remove %n separator(s)? The mods under them stay.", nullptr, separators);
        } else {
            question = modNames.size() == 1 ? tr("Delete %1 and its files from this instance?").arg(modNames.first())
                                            : tr("Delete %n mods and their files from this instance?", nullptr, static_cast<int>(modNames.size()));
            if (separators > 0) {
                question += "\n\n" + tr("%n selected separator(s) go too; the mods under them stay.", nullptr, separators);
            }
        }
        if (QMessageBox::question(this, tr("Remove mods"), question) != QMessageBox::Yes) {
            return;
        }
        // the list can change while the question is open (a finished download installs a mod), so rows follow the ids
        QList<int> rows;
        for (const auto& id : std::as_const(ids)) {
            if (const int row = m_mods->indexOf(id); row >= 0) {
                rows << row;
            }
        }
        if (auto removed = m_mods->remove(rows); !removed) {
            CustomMessageBox::selectable(this, tr("Remove mods"), removed.error(), QMessageBox::Critical)->exec();
        }
    });
    connect(m_up, &QAction::triggered, this, [this] { moveSelection(-1); });
    connect(m_down, &QAction::triggered, this, [this] { moveSelection(1); });
    connect(autoSort, &QAction::triggered, this, [this] { ModInstallFlow::autoSort(m_instance, this); });
    connect(check, &QAction::triggered, this, &ModsPage::checkForUpdates);
    connect(m_update, &QAction::triggered, this, &ModsPage::updateSelected);
    connect(m_nexusPage, &QAction::triggered, this, [this] {
        for (int row : selectedRows()) {
            const auto& mod = m_mods->mods()[row];
            if (mod.nexusModId > 0) {
                DesktopServices::openUrl(Nexus::modPageUrl(mod.nexusGame, mod.nexusModId));
            }
        }
    });
    connect(folder, &QAction::triggered, this, [this] {
        const auto rows = selectedRows();
        const bool oneMod = rows.size() == 1 && !m_mods->mods()[rows.first()].separator;
        DesktopServices::openPath(oneMod ? m_mods->modDir(m_mods->mods()[rows.first()].id) : m_mods->modsDir(), true);
    });
    connect(redeploy, &QAction::triggered, this, [this] { ModInstallFlow::deploy(m_instance, this); });
    connect(exportList, &QAction::triggered, this, &ModsPage::exportList);
    connect(changed, &QAction::triggered, this, &ModsPage::reviewChangedFiles);
    connect(conflicts, &QAction::triggered, this, &ModsPage::showConflicts);
    connect(m_view, &QAbstractItemView::doubleClicked, this, [this](const QModelIndex& index) {
        const auto source = m_proxy->mapToSource(index);
        if (source.column() == ModList::ConflictColumn && source.row() < m_mods->mods().size() && !m_mods->mods()[source.row()].separator) {
            ConflictsDialog dialog(m_mods, { m_mods->mods()[source.row()].id }, this);
            dialog.exec();
        }
    });

    connect(m_mods, &ModList::deploymentChanged, this, &ModsPage::scheduleDeploy);
    connect(m_mods, &ModList::filesDropped, this, &ModsPage::installFiles);
    // which rows a collapsed separator hides follows every change of the list
    const auto refresh = [this] {
        m_proxy->refilter();
        updateCounts();
    };
    connect(m_mods, &QAbstractItemModel::modelReset, this, refresh);
    connect(m_mods, &QAbstractItemModel::dataChanged, this, refresh);
    connect(m_mods, &QAbstractItemModel::rowsInserted, this, refresh);
    connect(m_mods, &QAbstractItemModel::rowsRemoved, this, refresh);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ModsPage::updateActions);
    connect(m_instance, &BaseInstance::runningStatusChanged, this, &ModsPage::updateActions);
    updateCounts();
    updateActions();
}

void ModsPage::openedImpl()
{
    for (int c = 0; c < ModList::ColumnCount; c++) {
        if (c != ModList::NameColumn) {
            m_view->resizeColumnToContents(c);
        }
    }
}

void ModsPage::installFiles(const QList<QUrl>& urls)
{
    for (const auto& url : urls) {
        if (url.isLocalFile()) {
            ModInstallFlow::installArchive(m_instance, url.toLocalFile(), ModEntry{}, this);
        }
    }
}

void ModsPage::reviewChangedFiles()
{
    const auto title = tr("Changed files");
    const auto manifestPath = m_instance->deploymentManifestPath();
    const auto gameDir = m_instance->gameRoot();
    const auto modsDir = m_instance->modsRoot();
    auto found = DeployedChanges::find(manifestPath, gameDir);
    if (!found) {
        CustomMessageBox::selectable(this, title, found.error(), QMessageBox::Critical)->exec();
        return;
    }
    if (found->isEmpty()) {
        CustomMessageBox::selectable(this, title, tr("No mod file in the game folder was changed outside BetterNMM."), QMessageBox::Information)
            ->exec();
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(700, 400);
    auto* layout = new QVBoxLayout(&dialog);
    auto* about = new QLabel(tr("These files of mods were replaced in the game folder after BetterNMM put them there, for example by "
                                "xEdit saving a cleaned plugin; the game reads the changed file. <b>Save into Mod</b> makes the change "
                                "part of its mod, so it stays when the mod is disabled and enabled again. <b>Restore Mod's File</b> "
                                "moves the changed file to the Recycle Bin and puts the mod's own file back."),
                             &dialog);
    about->setWordWrap(true);
    layout->addWidget(about);
    auto* list = new QTreeWidget(&dialog);
    list->setHeaderLabels({ tr("File"), tr("Mod") });
    list->setRootIsDecorated(false);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (const auto& file : *found) {
        const int row = m_mods->indexOf(file.mod);
        auto* item = new QTreeWidgetItem(list, { QDir::toNativeSeparators(file.path), row >= 0 ? m_mods->mods()[row].name : file.mod });
        item->setData(0, Qt::UserRole, file.path);
    }
    list->resizeColumnToContents(0);
    list->selectAll();
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* save = buttons->addButton(tr("Save into Mod"), QDialogButtonBox::ActionRole);
    auto* restore = buttons->addButton(tr("Restore Mod's File"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto apply = [&](const std::function<Result<>(const QString&)>& action) {
        QStringList problems;
        for (auto* item : list->selectedItems()) {
            if (auto done = action(item->data(0, Qt::UserRole).toString()); done) {
                delete item;
            } else {
                problems << done.error();
            }
        }
        if (!problems.isEmpty()) {
            CustomMessageBox::selectable(&dialog, title, problems.join("\n\n"), QMessageBox::Warning)->exec();
        }
        if (list->topLevelItemCount() == 0) {
            dialog.accept();
        }
    };
    connect(save, &QPushButton::clicked, &dialog, [&] {
        apply([&](const QString& path) { return DeployedChanges::saveIntoMod(manifestPath, gameDir, modsDir, path); });
    });
    connect(restore, &QPushButton::clicked, &dialog, [&] {
        apply([&](const QString& path) {
            return DeployedChanges::restore(manifestPath, gameDir, modsDir, path, [](const QString& file) { return FS::trash(file); });
        });
    });
    connect(list, &QTreeWidget::itemSelectionChanged, &dialog, [&] {
        save->setEnabled(!list->selectedItems().isEmpty());
        restore->setEnabled(!list->selectedItems().isEmpty());
    });
    dialog.exec();
}

void ModsPage::showConflicts()
{
    QStringList ids;
    for (int row : selectedRows()) {
        // a separator stands for the mods of its group
        for (int member = row; member <= m_mods->groupEnd(row); member++) {
            if (const auto& entry = m_mods->mods()[member]; !entry.separator && !ids.contains(entry.id)) {
                ids << entry.id;
            }
        }
    }
    ConflictsDialog dialog(m_mods, ids, this);
    dialog.exec();
}

void ModsPage::exportList()
{
    const auto suggested = FS::PathCombine(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                           FS::RemoveInvalidFilenameChars(m_instance->name()) + " mods.md");
    QString filter;
    auto path = QFileDialog::getSaveFileName(this, tr("Export mod list"), suggested, tr("Markdown (*.md);;CSV (*.csv);;Text (*.txt)"), &filter);
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path += filter.contains("*.csv") ? ".csv" : filter.contains("*.txt") ? ".txt" : ".md";
    }
    QStringList plugins;
    if (m_instance->definition().hasPlugins()) {
        for (const auto& plugin : m_instance->pluginList()->plugins()) {
            if (plugin.active || plugin.implicit) {
                plugins << plugin.name;
            }
        }
    }
    const auto text = ModListExport::write(tr("%1 (%2)").arg(m_instance->name(), m_instance->definition().name), m_mods->mods(), plugins,
                                           ModListExport::formatFor(path));
    if (auto written = FS::write(path, text.toUtf8()); !written) {
        CustomMessageBox::selectable(this, tr("Export mod list"), written.error(), QMessageBox::Critical)->exec();
    }
}

void ModsPage::scheduleDeploy()
{
    // several changes in one go (a multi-selection toggle, a drag) deploy once
    if (m_deployScheduled) {
        return;
    }
    m_deployScheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_deployScheduled = false;
        ModInstallFlow::deploy(m_instance, this);
    });
}

void ModsPage::checkForUpdates()
{
    // Nexus Mods answers this without an API key, every installed mod of a game in one go
    QMap<QString, QList<int>> byGame;
    for (const auto& mod : m_mods->mods()) {
        if (mod.nexusModId > 0 && !byGame[mod.nexusGame].contains(mod.nexusModId)) {
            byGame[mod.nexusGame] << mod.nexusModId;
        }
    }
    if (byGame.isEmpty()) {
        CustomMessageBox::selectable(this, tr("Check for updates"), tr("None of the mods were installed from Nexus Mods."), QMessageBox::Information)
            ->exec();
        return;
    }
    for (auto it = byGame.cbegin(); it != byGame.cend(); ++it) {
        const auto details = ModInstallFlow::fetchModDetails(it.key(), it.value(), this);
        if (!details) {
            CustomMessageBox::selectable(this, tr("Check for updates"), details.error(), QMessageBox::Critical)->exec();
            return;
        }
        for (const auto& mod : m_mods->mods()) {
            for (const auto& found : *details) {
                if (mod.nexusGame == it.key() && mod.nexusModId == found.modId) {
                    m_mods->setLatestVersion(mod.id, found.version);
                }
            }
        }
    }
    int updates = 0;
    int ignored = 0;
    for (const auto& mod : m_mods->mods()) {
        updates += mod.nexusModId > 0 && mod.hasUpdate() ? 1 : 0;
        ignored += mod.nexusModId > 0 && mod.ignoreUpdates && !mod.latestVersion.isEmpty() && mod.latestVersion != mod.version ? 1 : 0;
    }
    auto text = updates > 0 ? tr("%n mod(s) can be updated; their newest version is shown in bold.", nullptr, updates)
                            : tr("All mods from Nexus Mods are up to date.");
    if (ignored > 0) {
        text += "\n\n" + tr("%n mod(s) with a newer version are left alone because you ignore their updates.", nullptr, ignored);
    }
    CustomMessageBox::selectable(this, tr("Check for updates"), text, QMessageBox::Information)->exec();
    updateActions();
}

void ModsPage::updateSelected()
{
    QMap<QString, QList<ModEntry>> byGame;
    QStringList ignored;
    for (int row : selectedRows()) {
        if (const auto& mod = m_mods->mods()[row]; mod.nexusModId > 0) {
            if (mod.ignoreUpdates) {
                ignored << mod.name;
            } else {
                byGame[mod.nexusGame] << mod;
            }
        }
    }
    if (byGame.isEmpty() && !ignored.isEmpty()) {
        CustomMessageBox::selectable(this, tr("Update mods"),
                                     tr("You ignore the updates of %1. Choose Ignore Updates again to stop ignoring them, then Update.")
                                         .arg(ignored.join(", ")),
                                     QMessageBox::Information)
            ->exec();
        return;
    }
    QStringList problems;
    if (!ignored.isEmpty()) {
        problems << tr("Left alone because you ignore their updates: %1").arg(ignored.join(", "));
    }
    for (auto it = byGame.cbegin(); it != byGame.cend(); ++it) {
        QList<int> ids;
        for (const auto& mod : it.value()) {
            ids << mod.nexusModId;
        }
        // newest files without an API key; only Premium downloads need one
        const auto details = ModInstallFlow::fetchModDetails(it.key(), ids, this);
        const auto gameId = details && !details->isEmpty() ? details->first().gameId : 0;
        const auto files = gameId > 0 ? ModInstallFlow::fetchModFiles(gameId, ids, this) : Result<QMap<int, QList<Nexus::ModFile>>>{};
        if (!details || !files) {
            problems << (details ? files.error() : details.error());
            continue;
        }
        QList<ModInstallFlow::NexusFile> fromWebsite;
        for (const auto& mod : it.value()) {
            const auto newest = Nexus::bestFile(files->value(mod.nexusModId));
            if (newest.fileId <= 0) {
                problems << tr("%1: Nexus Mods has no files for it any more.").arg(mod.name);
            } else if (APPLICATION->accounts()->isPremium()) {
                ModInstallFlow::installNexusFile(m_instance, it.key(), mod.nexusModId, newest.fileId, this);
            } else {
                fromWebsite << ModInstallFlow::NexusFile{ .modId = mod.nexusModId, .modName = mod.name, .modVersion = mod.latestVersion, .file = newest };
            }
        }
        if (!fromWebsite.isEmpty()) {
            ModInstallFlow::downloadFromWebsite(m_instance, it.key(), fromWebsite, this);
        }
    }
    if (!problems.isEmpty()) {
        CustomMessageBox::selectable(this, tr("Update mods"), problems.join("\n\n"), QMessageBox::Warning)->exec();
    }
}

void ModsPage::updateActions()
{
    const bool running = m_instance->isRunning();
    const auto rows = selectedRows();
    const bool any = !rows.isEmpty();
    bool anyNexus = false;
    bool anyMod = false;
    bool allIgnored = true;
    for (int row : rows) {
        const auto& entry = m_mods->mods()[row];
        anyNexus |= entry.nexusModId > 0;
        if (!entry.separator) {
            anyMod = true;
            allIgnored &= entry.ignoreUpdates;
        }
    }
    m_view->setEnabled(!running);
    m_toolbar->setEnabled(!running);
    m_enable->setEnabled(any);
    m_disable->setEnabled(any);
    m_remove->setEnabled(any);
    m_up->setEnabled(any && filterText().isEmpty());
    m_down->setEnabled(any && filterText().isEmpty());
    m_update->setEnabled(anyNexus);
    m_nexusPage->setEnabled(anyNexus);
    m_ignoreUpdates->setEnabled(anyMod);
    m_ignoreUpdates->setChecked(anyMod && allIgnored);
}

void ModsPage::moveSelection(int direction)
{
    const auto rows = m_mods->withGroups(selectedRows());
    if (rows.isEmpty()) {
        return;
    }
    const auto& mods = m_mods->mods();
    if (direction < 0) {
        int target = rows.first() - 1;
        if (target < 0) {
            return;
        }
        // above is a mod of a collapsed group: the whole group is the one place to pass
        if (m_mods->hiddenByCollapse(target)) {
            while (target > 0 && !mods[target].separator) {
                target--;
            }
        }
        m_mods->moveRows(rows, target - rows.first());
        return;
    }
    const int next = rows.last() + 1;
    if (next >= mods.size()) {
        return;
    }
    const int end = mods[next].separator && mods[next].collapsed ? m_mods->groupEnd(next) : next;
    m_mods->moveRows(rows, end - rows.last());
}

void ModsPage::addModSeparator()
{
    bool ok = false;
    const auto name = QInputDialog::getText(this, tr("Add separator"), tr("Name of the separator:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    const auto rows = selectedRows();
    const int row = m_mods->addSeparator(name, rows.isEmpty() ? -1 : rows.first());
    m_view->selectionModel()->select(m_proxy->mapFromSource(m_mods->index(row, 0)),
                                     QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void ModsPage::updateCounts()
{
    const auto total = m_mods->modCount();
    const auto enabled = m_mods->enabledMods().size();
    setCounts(tr("%1 installed, %2 enabled").arg(total).arg(enabled));
}
