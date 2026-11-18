// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2023 TheKodeToad <TheKodeToad@proton.me>
 *  Copyright (C) 2025 Yihe Li <winmikedows@hotmail.com>
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

#include "CreateShortcutDialog.h"
#include "ui_CreateShortcutDialog.h"

#include "Application.h"
#include "DesktopServices.h"
#include "FileSystem.h"
#include "ShortcutUtils.h"
#include "game/GameInstance.h"
#include "icons/IconList.h"
#include "ui/dialogs/IconPickerDialog.h"

CreateShortcutDialog::CreateShortcutDialog(GameInstance* instance, QWidget* parent)
    : QDialog(parent), ui(new Ui::CreateShortcutDialog), m_instance(instance)
{
    ui->setupUi(this);

    m_iconKey = instance->iconKey();
    ui->iconButton->setIcon(APPLICATION->icons()->getIcon(m_iconKey));
    ui->instNameTextBox->setPlaceholderText(instance->name());

    if (!DesktopServices::isFlatpak()) {
        if (!FS::getDesktopDir().isEmpty()) {
            ui->saveTargetSelectionBox->addItem(tr("Desktop"), QVariant::fromValue(ShortcutTarget::Desktop));
        }
        if (!FS::getApplicationsDir().isEmpty()) {
            ui->saveTargetSelectionBox->addItem(tr("Start Menu"), QVariant::fromValue(ShortcutTarget::Applications));
        }
    }
    ui->saveTargetSelectionBox->addItem(tr("Other..."), QVariant::fromValue(ShortcutTarget::Other));

    ui->executableSelectionBox->addItem(tr("The instance's default (%1)").arg(instance->defaultExecutableName()), QString());
    for (const auto& exe : instance->executables()) {
        ui->executableSelectionBox->addItem(exe.name, exe.name);
    }
}

CreateShortcutDialog::~CreateShortcutDialog()
{
    delete ui;
}

void CreateShortcutDialog::on_iconButton_clicked()
{
    IconPickerDialog dlg(this);
    dlg.execWithSelection(m_iconKey);

    if (dlg.result() == QDialog::Accepted) {
        m_iconKey = dlg.selectedIconKey;
        ui->iconButton->setIcon(APPLICATION->icons()->getIcon(m_iconKey));
    }
}

void CreateShortcutDialog::createShortcut()
{
    QStringList extraArgs;
    QString targetString = tr("instance");
    if (const auto executable = ui->executableSelectionBox->currentData().toString(); !executable.isEmpty()) {
        extraArgs = { "--executable", executable };
        targetString = executable;
    }

    auto target = ui->saveTargetSelectionBox->currentData().value<ShortcutTarget>();
    auto name = ui->instNameTextBox->text();
    if (name.isEmpty()) {
        name = ui->instNameTextBox->placeholderText();
    }

    ShortcutUtils::Shortcut args{ m_instance, name, targetString, this, extraArgs, m_iconKey, target };
    if (target == ShortcutTarget::Desktop) {
        ShortcutUtils::createInstanceShortcutOnDesktop(args);
    } else if (target == ShortcutTarget::Applications) {
        ShortcutUtils::createInstanceShortcutInApplications(args);
    } else {
        ShortcutUtils::createInstanceShortcutInOther(args);
    }
}
