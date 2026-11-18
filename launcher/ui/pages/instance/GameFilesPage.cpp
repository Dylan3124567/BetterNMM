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

#include "GameFilesPage.h"

#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "DesktopServices.h"
#include "game/Deployer.h"
#include "game/GameFiles.h"
#include "game/GameInstance.h"
#include "settings/SettingsObject.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ModInstallFlow.h"
#include "ui/dialogs/ProgressDialog.h"

GameFilesPage::GameFilesPage(GameInstance* instance, QWidget* parent) : QWidget(parent), m_instance(instance)
{
    auto* layout = new QVBoxLayout(this);

    auto* where = new QGroupBox(tr("Locations"), this);
    auto* whereLayout = new QFormLayout(where);
    auto addPath = [&](const QString& label, const QString& path) {
        auto* row = new QHBoxLayout();
        // a wrapped label clips long paths inside a form row; a read-only line edit scrolls and copies
        auto* text = new QLineEdit(QDir::toNativeSeparators(path), where);
        text->setReadOnly(true);
        text->setCursorPosition(0);
        text->setToolTip(text->text());
        auto* open = new QPushButton(tr("Open"), where);
        connect(open, &QPushButton::clicked, this, [path] { DesktopServices::openPath(path); });
        row->addWidget(text, 1);
        row->addWidget(open);
        whereLayout->addRow(label, row);
    };
    addPath(tr("Game folder:"), instance->gameRoot());
    if (!instance->isMain()) {
        addPath(tr("Built from:"), instance->sourceGameDir());
        addPath(tr("Profile (Documents, AppData):"), instance->profileRoot());
    }
    addPath(tr("Mod files:"), instance->modsRoot());
    layout->addWidget(where);

    if (!instance->isMain()) {
        auto* sharing = new QGroupBox(tr("Sharing with the Steam installation"), this);
        auto* sharingLayout = new QVBoxLayout(sharing);
        auto* explain = new QLabel(tr("Game files are hard links to the Steam installation: they use no extra disk space. Files matching "
                                      "the local patterns are real copies instead, so patching them (executables, DLLs, configs) never "
                                      "changes the Steam installation or other instances. Files matching the exclude patterns are left "
                                      "out of this instance."),
                                   sharing);
        explain->setWordWrap(true);
        sharingLayout->addWidget(explain);
        auto* form = new QFormLayout();
        m_local = new QLineEdit(instance->settings()->get("LocalFilePatterns").toString(), sharing);
        m_local->setToolTip(tr("Semicolon separated. Patterns with a / match the path inside the game folder, e.g. Data/SKSE/*."));
        // left empty, the game's defaults apply: never no copies at all, which would share them with Steam
        m_local->setPlaceholderText(GameInstance::defaultLocalPatterns(instance->definition()));
        m_exclude = new QLineEdit(instance->settings()->get("ExcludeFilePatterns").toString(), sharing);
        m_exclude->setToolTip(m_local->toolTip());
        form->addRow(tr("Local copies:"), m_local);
        form->addRow(tr("Exclude:"), m_exclude);
        sharingLayout->addLayout(form);
        m_stats = new QLabel(sharing);
        m_stats->setWordWrap(true);
        sharingLayout->addWidget(m_stats);
        auto* resync = new QPushButton(tr("&Update from Steam Installation"), sharing);
        resync->setToolTip(tr("Bring in game updates and new files from the Steam installation, apply the patterns above, and keep "
                              "everything you or your mods changed in this instance."));
        connect(resync, &QPushButton::clicked, this, &GameFilesPage::resync);
        auto* buttons = new QHBoxLayout();
        buttons->addWidget(resync);
        buttons->addStretch(1);
        sharingLayout->addLayout(buttons);
        layout->addWidget(sharing);
    }

    auto* deployment = new QGroupBox(tr("Deployed mods"), this);
    auto* deploymentLayout = new QHBoxLayout(deployment);
    auto* redeploy = new QPushButton(tr("&Redeploy"), deployment);
    redeploy->setToolTip(tr("Make the game folder match the enabled mods again."));
    auto* purge = new QPushButton(tr("Remove &All Mod Files"), deployment);
    purge->setToolTip(tr("Take every mod file out of the game folder and put back the files they replaced. The mods stay installed "
                         "and are deployed again on the next launch."));
    connect(redeploy, &QPushButton::clicked, this, [this] {
        ModInstallFlow::deploy(m_instance, this);
        refreshStats();
    });
    connect(purge, &QPushButton::clicked, this, [this] {
        ModInstallFlow::purge(m_instance, this);
        refreshStats();
    });
    deploymentLayout->addWidget(redeploy);
    deploymentLayout->addWidget(purge);
    deploymentLayout->addStretch(1);
    layout->addWidget(deployment);
    layout->addStretch(1);
}

void GameFilesPage::openedImpl()
{
    refreshStats();
}

void GameFilesPage::refreshStats()
{
    if (!m_stats) {
        return;
    }
    auto manifest = GameFiles::BaseManifest::load(m_instance->baseManifestPath());
    if (!manifest) {
        m_stats->setText(manifest.error());
        return;
    }
    int linked = 0;
    int copied = 0;
    quint64 copiedBytes = 0;
    for (const auto& entry : manifest->files) {
        if (entry.linked) {
            linked++;
        } else {
            copied++;
            copiedBytes += entry.size;
        }
    }
    int deployed = 0;
    if (auto deployment = DeploymentManifest::load(m_instance->deploymentManifestPath())) {
        deployed = static_cast<int>(deployment->files.size());
    }
    m_stats->setText(tr("%1 game files shared with Steam, %2 local copies using %3, %4 mod files deployed.")
                         .arg(linked)
                         .arg(copied)
                         .arg(QLocale().formattedDataSize(static_cast<qint64>(copiedBytes)))
                         .arg(deployed));
}

bool GameFilesPage::apply()
{
    if (m_local) {
        m_instance->settings()->set("LocalFilePatterns", m_local->text().trimmed());
        m_instance->settings()->set("ExcludeFilePatterns", m_exclude->text().trimmed());
    }
    return true;
}

void GameFilesPage::resync()
{
    if (m_instance->isRunning()) {
        CustomMessageBox::selectable(this, tr("Instance is running"), tr("Close the game first."), QMessageBox::Warning)->exec();
        return;
    }
    apply();
    auto task = m_instance->createBaseSyncTask();
    ProgressDialog dialog(this);
    dialog.execWithTask(task.get());
    if (!task->wasSuccessful()) {
        CustomMessageBox::selectable(this, tr("Update failed"), task->failReason(), QMessageBox::Critical)->exec();
    } else {
        CustomMessageBox::selectable(this, tr("Game files updated"), task->messages().join("\n"), QMessageBox::Information)->exec();
    }
    refreshStats();
}
