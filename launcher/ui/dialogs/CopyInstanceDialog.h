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

#pragma once

#include <QDialog>
#include "InstanceCopyPrefs.h"

class GameInstance;

namespace Ui {
class CopyInstanceDialog;
}

class CopyInstanceDialog : public QDialog {
    Q_OBJECT

   public:
    explicit CopyInstanceDialog(GameInstance* original, QWidget* parent = nullptr);
    ~CopyInstanceDialog() override;

    void updateDialogState();

    QString instName() const;
    QString instGroup() const;
    QString iconKey() const;
    InstanceCopyPrefs getChosenOptions() const;

   public slots:
    void help();

   private slots:
    void on_iconButton_clicked();
    void on_instNameTextBox_textChanged(const QString& arg1);

   private:
    Ui::CopyInstanceDialog* ui;
    QString m_iconKey;
    GameInstance* m_original;
};
