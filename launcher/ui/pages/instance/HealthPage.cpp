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

#include "HealthPage.h"

#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QMap>
#include <QSet>
#include <QStandardItemModel>
#include <QTreeView>

#include "DesktopServices.h"
#include "FileSystem.h"
#include "game/GameInstance.h"
#include "game/ModList.h"
#include "game/PluginList.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ModInstallFlow.h"
#include "ui/dialogs/NexusBrowseDialog.h"

using HealthCheck::Kind;

HealthPage::HealthPage(GameInstance* instance, QWidget* parent) : ListPage(parent), m_instance(instance), m_model(new QStandardItemModel(this))
{
    m_model->setHorizontalHeaderLabels({ tr("Problem"), tr("Fix") });
    setModel(m_model, 0);
    m_view->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setWordWrap(true);
    m_view->setToolTip(tr("Problems that keep the mods from working. Select some and click Fix, or double-click one."));

    auto* recheck = addAction(tr("&Check"), tr("Look for problems again, asking Nexus Mods what each mod needs."), "refresh");
    m_fix = addAction(tr("&Fix"), tr("Do what the Fix column says for the selected problems."), "status-good");
    m_page = addAction(tr("Find on &Nexus"), tr("Open the Nexus Mods pages of the selected requirements, or search Nexus Mods for "
                                                "a selected missing master."));
    connect(recheck, &QAction::triggered, this, [this] { check(true); });
    connect(m_fix, &QAction::triggered, this, &HealthPage::fixSelected);
    connect(m_page, &QAction::triggered, this, &HealthPage::openPages);
    connect(m_view, &QAbstractItemView::doubleClicked, this, &HealthPage::fixSelected);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &HealthPage::updateActions);
    connect(m_instance, &BaseInstance::runningStatusChanged, this, &HealthPage::updateActions);
    updateActions();
}

void HealthPage::openedImpl()
{
    // what Nexus Mods said stays until Check asks again: opening the page costs no requests
    check(false);
}

void HealthPage::check(bool askNexus)
{
    auto* mods = m_instance->modList();
    if (askNexus) {
        QMap<QString, QList<int>> byGame;
        for (const auto& mod : mods->mods()) {
            if (mod.enabled && mod.nexusModId > 0 && !byGame[mod.nexusGame].contains(mod.nexusModId)) {
                byGame[mod.nexusGame] << mod.nexusModId;
            }
        }
        m_details.clear();
        m_nexusError.clear();
        for (auto it = byGame.cbegin(); it != byGame.cend(); ++it) {
            const auto details = ModInstallFlow::fetchModDetails(it.key(), it.value(), this);
            if (!details) {
                m_nexusError = details.error();
                break;
            }
            m_details << *details;
        }
        m_askedNexus = true;
    }

    m_problems.clear();
    if (m_instance->definition().hasPlugins()) {
        // a master no enabled mod has may be in a disabled one
        QHash<QString, std::pair<QString, QString>> disabled;
        const auto dataPath = m_instance->definition().dataPath;
        for (const auto& mod : mods->mods()) {
            if (mod.enabled || mod.separator) {
                continue;
            }
            const auto dir = dataPath.isEmpty() ? mods->modDir(mod.id) : FS::PathCombine(mods->modDir(mod.id), dataPath);
            for (const auto& name : QDir(dir).entryList({ "*.esp", "*.esm", "*.esl" }, QDir::Files)) {
                if (!disabled.contains(name.toLower())) {
                    disabled.insert(name.toLower(), { mod.id, mod.name });
                }
            }
        }
        if (auto* plugins = m_instance->pluginList(); plugins->refresh()) {
            m_problems << HealthCheck::pluginProblems(plugins->plugins(), disabled);
        }
    }
    const auto& game = m_instance->definition();
    const HealthCheck::ScriptExtender extender{ .exists = !game.scriptExtender.isEmpty(),
                                                .nexusModId = game.scriptExtenderMod,
                                                .installed = !game.scriptExtender.isEmpty() &&
                                                             QFileInfo::exists(FS::PathCombine(m_instance->gameRoot(), game.scriptExtender)) };
    m_problems << HealthCheck::requirementProblems(mods->mods(), m_details, extender);

    m_model->removeRows(0, m_model->rowCount());
    for (int i = 0; i < m_problems.size(); i++) {
        const auto& problem = m_problems[i];
        auto* what = new QStandardItem(HealthCheck::describe(problem));
        what->setData(i, Qt::UserRole);
        what->setIcon(QIcon::fromTheme("status-bad"));
        if (!problem.notes.isEmpty()) {
            what->setToolTip(tr("Its mod page says: %1").arg(problem.notes.join("\n")));
        }
        m_model->appendRow({ what, new QStandardItem(HealthCheck::remedy(problem)) });
    }
    m_view->resizeColumnToContents(1);

    QStringList status;
    status << (m_problems.isEmpty() ? tr("No problems found") : tr("%n problem(s)", nullptr, static_cast<int>(m_problems.size())));
    if (!m_askedNexus) {
        status << tr("Nexus Mods requirements not checked yet: click Check");
    } else if (!m_nexusError.isEmpty()) {
        status << tr("Nexus Mods could not be asked: %1").arg(m_nexusError);
    }
    setCounts(status.join(" · "));
    updateActions();
}

QList<HealthCheck::Problem> HealthPage::selectedProblems() const
{
    QList<HealthCheck::Problem> out;
    for (int row : selectedRows()) {
        if (const auto index = m_model->index(row, 0).data(Qt::UserRole).toInt(); index >= 0 && index < m_problems.size()) {
            out << m_problems[index];
        }
    }
    return out;
}

void HealthPage::fixSelected()
{
    if (m_instance->isRunning()) {
        return;
    }
    const auto problems = selectedProblems();
    if (problems.isEmpty()) {
        return;
    }
    auto* plugins = m_instance->pluginList();
    auto* mods = m_instance->modList();
    const auto rowOf = [plugins](const QString& name) {
        for (int i = 0; i < plugins->plugins().size(); i++) {
            if (plugins->plugins()[i].name.compare(name, Qt::CaseInsensitive) == 0) {
                return i;
            }
        }
        return -1;
    };

    QList<int> activate;
    QList<int> deactivate;
    QList<int> enable;
    QMap<QString, QList<int>> install;  // Nexus Mods game -> mod ids
    bool extender = false;
    QStringList order;
    for (const auto& plugin : plugins->plugins()) {
        order << plugin.name;
    }
    bool reorder = false;
    for (const auto& problem : problems) {
        switch (problem.kind) {
            case Kind::MissingMaster:
                deactivate << rowOf(problem.plugin);
                break;
            case Kind::InactiveMaster:
                activate << rowOf(problem.master);
                break;
            case Kind::MasterLoadsLater: {
                const auto master = order.indexOf(problem.master);
                const auto plugin = order.indexOf(problem.plugin);
                if (master > plugin && plugin >= 0) {
                    order.move(master, plugin);
                    reorder = true;
                }
                break;
            }
            case Kind::MissingRequirement:
                if (!install[problem.game].contains(problem.nexusModId)) {
                    install[problem.game] << problem.nexusModId;
                }
                break;
            case Kind::DisabledRequirement:
            case Kind::MasterInDisabledMod:
                if (const int row = mods->indexOf(problem.modId); !enable.contains(row)) {
                    enable << row;
                }
                break;
            case Kind::MissingScriptExtender:
                extender = true;
                break;
        }
    }
    if (reorder) {
        plugins->setOrder(order);
    }
    plugins->setActive(activate, true);
    plugins->setActive(deactivate, false);
    if (!enable.isEmpty()) {
        // deployed before the check below, so the plugins the mods bring are there for it
        mods->setEnabled(enable, true);
        ModInstallFlow::deploy(m_instance, this);
    }
    if (extender) {
        ModInstallFlow::installScriptExtender(m_instance, this);
    }
    for (auto it = install.cbegin(); it != install.cend(); ++it) {
        ModInstallFlow::installNexusMods(m_instance, it.key(), it.value(), this);
    }
    check(false);
}

void HealthPage::openPages()
{
    QString master;
    for (const auto& problem : selectedProblems()) {
        if (problem.nexusModId > 0) {
            DesktopServices::openUrl(Nexus::modPageUrl(problem.game, problem.nexusModId));
        } else if (problem.kind == Kind::MissingMaster && master.isEmpty()) {
            master = problem.master;
        }
    }
    if (!master.isEmpty()) {
        // mods are found by name, which a plugin's usually is without its extension
        NexusBrowseDialog dialog(m_instance, this);
        dialog.searchFor(QFileInfo(master).completeBaseName());
        dialog.exec();
        check(false);
    }
}

void HealthPage::updateActions()
{
    const bool running = m_instance->isRunning();
    const auto problems = selectedProblems();
    const bool anyPage = std::any_of(problems.begin(), problems.end(),
                                     [](const HealthCheck::Problem& p) { return p.nexusModId > 0 || p.kind == Kind::MissingMaster; });
    m_fix->setEnabled(!running && !problems.isEmpty());
    m_page->setEnabled(anyPage);
}
