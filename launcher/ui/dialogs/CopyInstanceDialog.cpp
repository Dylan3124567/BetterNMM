// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2023 TheKodeToad <TheKodeToad@proton.me>
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

#include <QLayout>
#include <QPushButton>

#include "Application.h"
#include "BuildConfig.h"
#include "CopyInstanceDialog.h"
#include "ui_CopyInstanceDialog.h"

#include "ui/dialogs/IconPickerDialog.h"

#include "DesktopServices.h"
#include "InstanceList.h"
#include "game/GameInstance.h"
#include "icons/IconList.h"

CopyInstanceDialog::CopyInstanceDialog(GameInstance* original, QWidget* parent)
    : QDialog(parent), ui(new Ui::CopyInstanceDialog), m_original(original)
{
    ui->setupUi(this);
    resize(minimumSizeHint());
    layout()->setSizeConstraint(QLayout::SetFixedSize);

    m_iconKey = original->iconKey();
    ui->iconButton->setIcon(APPLICATION->icons()->getIcon(m_iconKey));
    ui->instNameTextBox->setText(original->name());
    ui->instNameTextBox->setFocus();
    auto groupList = APPLICATION->instances()->getGroups();
    QStringList groups(groupList.begin(), groupList.end());
    groups.removeAll("");
    groups.sort(Qt::CaseInsensitive);
    groups.prepend("");
    ui->groupBox->addItems(groups);
    int index = groups.indexOf(APPLICATION->instances()->getInstanceGroup(m_original->id()));
    ui->groupBox->setCurrentIndex(index < 0 ? 0 : index);
    ui->groupBox->lineEdit()->setPlaceholderText(tr("No group"));

    const InstanceCopyPrefs defaults;
    ui->keepPlaytimeCheckbox->setChecked(defaults.keepPlaytime);
    // copying from the Steam instance reads the user's real saves; only do that when asked
    ui->copySavesCheckbox->setChecked(defaults.copySaves && !original->isMain());
    ui->copySettingsCheckbox->setChecked(defaults.copySettings);
    ui->copyModsCheckbox->setChecked(defaults.copyMods);
    ui->linkModFilesCheckbox->setChecked(defaults.linkModFiles);
    connect(ui->copyModsCheckbox, &QCheckBox::toggled, ui->linkModFilesCheckbox, &QWidget::setEnabled);

    auto* helpButton = ui->buttonBox->button(QDialogButtonBox::Help);
    helpButton->setVisible(!BuildConfig.HELP_URL.isEmpty());
    connect(helpButton, &QPushButton::clicked, this, &CopyInstanceDialog::help);
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    ui->buttonBox->button(QDialogButtonBox::Ok)->setText(tr("OK"));
    updateDialogState();
}

CopyInstanceDialog::~CopyInstanceDialog()
{
    delete ui;
}

void CopyInstanceDialog::updateDialogState()
{
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!instName().isEmpty());
}

QString CopyInstanceDialog::instName() const
{
    return ui->instNameTextBox->text().trimmed();
}

QString CopyInstanceDialog::iconKey() const
{
    return m_iconKey;
}

QString CopyInstanceDialog::instGroup() const
{
    return ui->groupBox->currentText();
}

InstanceCopyPrefs CopyInstanceDialog::getChosenOptions() const
{
    InstanceCopyPrefs prefs;
    prefs.keepPlaytime = ui->keepPlaytimeCheckbox->isChecked();
    prefs.copySaves = ui->copySavesCheckbox->isChecked();
    prefs.copySettings = ui->copySettingsCheckbox->isChecked();
    prefs.copyMods = ui->copyModsCheckbox->isChecked();
    prefs.linkModFiles = prefs.copyMods && ui->linkModFilesCheckbox->isChecked();
    return prefs;
}

void CopyInstanceDialog::help()
{
    DesktopServices::openUrl(QUrl(BuildConfig.HELP_URL.arg("instance-copy")));
}

void CopyInstanceDialog::on_iconButton_clicked()
{
    IconPickerDialog dlg(this);
    dlg.execWithSelection(m_iconKey);

    if (dlg.result() == QDialog::Accepted) {
        m_iconKey = dlg.selectedIconKey;
        ui->iconButton->setIcon(APPLICATION->icons()->getIcon(m_iconKey));
    }
}

void CopyInstanceDialog::on_instNameTextBox_textChanged([[maybe_unused]] const QString& arg1)
{
    updateDialogState();
}
