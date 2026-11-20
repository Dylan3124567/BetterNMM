// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (c) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
 *  Copyright (c) 2022 dada513 <dada513@protonmail.com>
 *  Copyright (C) 2022 Tayou <git@tayou.org>
 *  Copyright (C) 2024 TheKodeToad <TheKodeToad@proton.me>
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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "LauncherPage.h"
#include "ui_LauncherPage.h"

#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QTextCharFormat>

#include <FileSystem.h>
#include "Application.h"
#include "BuildConfig.h"
#include "DesktopServices.h"
#include "Json.h"
#include "game/GameInstance.h"
#include "net/DownloadManager.h"
#include "settings/SettingsObject.h"
#include "ui/themes/ITheme.h"
#include "ui/themes/ThemeManager.h"
#include "nexus/NxmLink.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/VortexImportDialog.h"
#include "vortex/VortexState.h"

#include <QApplication>
#include <QProcess>

// FIXME: possibly move elsewhere
enum InstSortMode {
    // Sort alphabetically by name.
    Sort_Name,
    // Sort by which instance was launched most recently.
    Sort_LastLaunch,
    // Sort by which instance has the most playtime.
    Sort_Playtime,
};

LauncherPage::LauncherPage(QWidget* parent) : QWidget(parent), ui(new Ui::LauncherPage)
{
    ui->setupUi(this);

    ui->sortingModeGroup->setId(ui->sortByNameBtn, Sort_Name);
    ui->sortingModeGroup->setId(ui->sortLastLaunchedBtn, Sort_LastLaunch);
    ui->sortingModeGroup->setId(ui->sortByPlaytimeBtn, Sort_Playtime);

    loadSettings();

    connect(ui->nxmRegisterBtn, &QPushButton::clicked, this, [this] {
        if (auto registered = Nxm::registerHandler(); !registered) {
            CustomMessageBox::selectable(this, tr("Nexus Mods links"), registered.error(), QMessageBox::Critical)->exec();
        }
        updateNxmStatus();
    });
    updateNxmStatus();

    connect(ui->savesIsolateBtn, &QPushButton::clicked, this, [this] {
        const bool isolate = !GameInstance::sharedProfileFolders().isEmpty();
        const auto question =
            isolate ? tr("Windows only gives a program its own Saved Games and AppData\\LocalLow folders when your Windows user folder "
                         "settings name them relative to your user profile, the way they name Documents.\n\n"
                         "BetterNMM records their current places in that form. Nothing moves and every other program keeps "
                         "using the same folders; isolated instances get their own from their next launch.\n\nContinue?")
                    : tr("Let isolated instances use your normal Saved Games and AppData\\LocalLow again? Their own copies stay in "
                         "their profile folders.");
        if (QMessageBox::question(this, tr("Saves"), question) != QMessageBox::Yes) {
            return;
        }
        if (auto changed = GameInstance::setProfileFoldersIsolated(isolate); !changed) {
            CustomMessageBox::selectable(this, tr("Saves"), changed.error(), QMessageBox::Warning)->exec();
        }
        updateSavesStatus();
    });
    updateSavesStatus();

    // usually done once, after moving over from Vortex, so it lives here rather than among the everyday actions
    const auto vortex = QString::fromStdWString(VortexState::defaultDatabase());
    const bool hasVortex = !vortex.isEmpty() && QFileInfo::exists(FS::PathCombine(QDir::fromNativeSeparators(vortex), "CURRENT"));
    ui->vortexStatusLabel->setText(hasVortex ? tr("Bring Vortex profiles over as instances, with their mods, load order, settings and "
                                                  "copies of their saves. Vortex itself is only read and keeps working as before.")
                                             : tr("Vortex is not set up on this PC, so there is nothing to bring over."));
    ui->vortexImportBtn->setEnabled(hasVortex);
    connect(ui->vortexImportBtn, &QPushButton::clicked, this, [this] { VortexImportDialog::run({}, this); });
}

void LauncherPage::updateSavesStatus()
{
    const auto shared = GameInstance::sharedProfileFolders();
    ui->savesStatusLabel->setText(
        shared.isEmpty() ? tr("Each isolated instance has its own Documents, AppData, Saved Games and AppData\\LocalLow.")
                         : tr("Each isolated instance has its own Documents and AppData, but shares %1 with your normal profile, so games "
                              "that save there (Cyberpunk 2077, many Unity games) share those saves.")
                               .arg(shared.join(tr(" and "))));
    ui->savesIsolateBtn->setText(shared.isEmpty() ? tr("&Share Saved Games and LocalLow again")
                                                  : tr("Give each instance its own &Saved Games and LocalLow"));
}

void LauncherPage::updateNxmStatus()
{
    const bool handled = Nxm::isHandler();
    ui->nxmStatusLabel->setText(handled ? tr("The \"Mod Manager Download\" buttons on Nexus Mods open %1.").arg(BuildConfig.LAUNCHER_DISPLAYNAME)
                                        : tr("The \"Mod Manager Download\" buttons on Nexus Mods open another program (or none). Click "
                                             "below to have them open %1; this replaces Vortex or Mod Organizer as the handler.")
                                              .arg(BuildConfig.LAUNCHER_DISPLAYNAME));
    ui->nxmRegisterBtn->setEnabled(!handled);
}

LauncherPage::~LauncherPage()
{
    delete ui;
}

bool LauncherPage::apply()
{
    applySettings();
    return true;
}

bool LauncherPage::confirmInstanceDirPath(const QString& rawDir, const QString& cookedDir)
{
    if (DesktopServices::isFlatpak() && rawDir.startsWith("/run/user")) {
        QMessageBox warning;
        warning.setText(tr("You're trying to specify an instance folder "
                           "which was granted temporarily via Flatpak.\n"
                           "This is known to cause problems. "
                           "After a restart the launcher might break, "
                           "because it will no longer have access to that directory.\n\n"
                           "Granting %1 access to it via Flatseal is recommended.")
                            .arg(BuildConfig.LAUNCHER_DISPLAYNAME));
        warning.setInformativeText(tr("Do you want to proceed anyway?"));
        warning.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        if (warning.exec() != QMessageBox::Ok)
            return false;
    }
    return true;
}

void LauncherPage::on_instDirBrowseBtn_clicked()
{
    QString rawDir = QFileDialog::getExistingDirectory(this, tr("Instance Folder"), ui->instDirTextBox->text());
    if (!rawDir.isEmpty() && QDir(rawDir).exists()) {
        QString cookedDir = FS::NormalizePath(rawDir);
        if (confirmInstanceDirPath(rawDir, cookedDir))
            ui->instDirTextBox->setText(cookedDir);
    }
}

void LauncherPage::on_addInstDirBtn_clicked()
{
    QString rawDir = QFileDialog::getExistingDirectory(this, tr("Additional Instance Folder"));
    if (rawDir.isEmpty() || !QDir(rawDir).exists())
        return;

    QString cookedDir = FS::NormalizePath(rawDir);
    if (cookedDir == FS::NormalizePath(ui->instDirTextBox->text())) {
        QMessageBox::warning(this, tr("Duplicate directory"), tr("This is already your primary instance directory."));
        return;
    }

    if (!ui->additionalInstDirsList->findItems(cookedDir, Qt::MatchFixedString).isEmpty()) {
        QMessageBox::warning(this, tr("Duplicate directory"), tr("This directory has already been added."));
        return;
    }

    if (!confirmInstanceDirPath(rawDir, cookedDir))
        return;

    ui->additionalInstDirsList->addItem(cookedDir);
}

void LauncherPage::on_removeInstDirBtn_clicked()
{
    qDeleteAll(ui->additionalInstDirsList->selectedItems());
}

void LauncherPage::on_iconsDirBrowseBtn_clicked()
{
    QString rawDir = QFileDialog::getExistingDirectory(this, tr("Icons Folder"), ui->iconsDirTextBox->text());

    // do not allow current dir - it's dirty. Do not allow dirs that don't exist
    if (!rawDir.isEmpty() && QDir(rawDir).exists()) {
        QString cookedDir = FS::NormalizePath(rawDir);
        ui->iconsDirTextBox->setText(cookedDir);
    }
}

void LauncherPage::on_downloadsDirBrowseBtn_clicked()
{
    QString rawDir = QFileDialog::getExistingDirectory(this, tr("Downloads Folder"), ui->downloadsDirTextBox->text());

    if (!rawDir.isEmpty() && QDir(rawDir).exists()) {
        QString cookedDir = FS::NormalizePath(rawDir);
        ui->downloadsDirTextBox->setText(cookedDir);
    }
}

void LauncherPage::applySettings()
{
    auto* s = APPLICATION->settings();

    s->set("MenuBarInsteadOfToolBar", ui->preferMenuBarCheckBox->isChecked());

    s->set("NumberOfConcurrentTasks", ui->numberOfConcurrentTasksSpinBox->value());
    s->set("NumberOfConcurrentDownloads", ui->numberOfConcurrentDownloadsSpinBox->value());
    APPLICATION->downloads()->setMaxRunning(ui->numberOfConcurrentDownloadsSpinBox->value());
    s->set("NetworkRetryLimit", ui->networkRetryLimitSpinBox->value());
    s->set("RequestTimeout", ui->timeoutSecondsSpinBox->value());

    // Console settings
    s->set("ConsoleMaxLines", ui->lineLimitSpinBox->value());
    s->set("ConsoleOverflowStop", ui->checkStopLogging->checkState() != Qt::Unchecked);

    // Folders
    // TODO: Offer to move instances to new instance folder.
    s->set("InstanceDir", ui->instDirTextBox->text());
    {
        QStringList additionalDirs;
        for (int i = 0; i < ui->additionalInstDirsList->count(); ++i) {
            additionalDirs << ui->additionalInstDirsList->item(i)->text();
        }
        s->set("AdditionalInstanceDirs", additionalDirs);
    }
    s->set("IconsDir", ui->iconsDirTextBox->text());
    s->set("DownloadsDir", ui->downloadsDirTextBox->text());

    // Instance
    auto sortMode = (InstSortMode)ui->sortingModeGroup->checkedId();
    switch (sortMode) {
        case Sort_LastLaunch:
            s->set("InstSortMode", "LastLaunch");
            break;
        case Sort_Playtime:
            s->set("InstSortMode", "Playtime");
            break;
        case Sort_Name:
        default:
            s->set("InstSortMode", "Name");
            break;
    }

    if (ui->askToRenameDirBtn->isChecked()) {
        s->set("InstRenamingMode", "AskEverytime");
    } else if (ui->alwaysRenameDirBtn->isChecked()) {
        s->set("InstRenamingMode", "PhysicalDir");
    } else if (ui->neverRenameDirBtn->isChecked()) {
        s->set("InstRenamingMode", "MetadataOnly");
    }

    s->set("EditInstanceOnDoubleClick", ui->editInstanceOnDoubleClick->isChecked());
}

void LauncherPage::loadSettings()
{
    auto* s = APPLICATION->settings();
    ui->preferMenuBarCheckBox->setChecked(s->get("MenuBarInsteadOfToolBar").toBool());

    ui->numberOfConcurrentTasksSpinBox->setValue(s->get("NumberOfConcurrentTasks").toInt());
    ui->numberOfConcurrentDownloadsSpinBox->setValue(s->get("NumberOfConcurrentDownloads").toInt());
    ui->networkRetryLimitSpinBox->setValue(s->get("NetworkRetryLimit").toInt());
    ui->timeoutSecondsSpinBox->setValue(s->get("RequestTimeout").toInt());

    // Console settings
    ui->lineLimitSpinBox->setValue(s->get("ConsoleMaxLines").toInt());
    ui->checkStopLogging->setChecked(s->get("ConsoleOverflowStop").toBool());

    // Folders
    ui->instDirTextBox->setText(s->get("InstanceDir").toString());
    ui->additionalInstDirsList->clear();
    ui->additionalInstDirsList->addItems(s->get("AdditionalInstanceDirs").toStringList());
    ui->iconsDirTextBox->setText(s->get("IconsDir").toString());
    ui->downloadsDirTextBox->setText(s->get("DownloadsDir").toString());

    // Instance
    QString sortMode = s->get("InstSortMode").toString();
    if (sortMode == "LastLaunch") {
        ui->sortLastLaunchedBtn->setChecked(true);
    } else if (sortMode == "Playtime") {
        ui->sortByPlaytimeBtn->setChecked(true);
    } else {
        ui->sortByNameBtn->setChecked(true);
    }

    ui->editInstanceOnDoubleClick->setChecked(s->get("EditInstanceOnDoubleClick").toBool());

    QString renamingMode = s->get("InstRenamingMode").toString();
    ui->askToRenameDirBtn->setChecked(renamingMode == "AskEverytime");
    ui->alwaysRenameDirBtn->setChecked(renamingMode == "PhysicalDir");
    ui->neverRenameDirBtn->setChecked(renamingMode == "MetadataOnly");
}

void LauncherPage::retranslate()
{
    ui->retranslateUi(this);
}
