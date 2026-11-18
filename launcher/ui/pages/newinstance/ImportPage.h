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

#include <QWidget>

#include "ui/pages/BasePage.h"

class NewInstanceDialog;
class QLabel;
class QLineEdit;

/// New instance from an archive exported by BetterNMM
class ImportPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    explicit ImportPage(NewInstanceDialog* dialog, QWidget* parent = nullptr);

    QString displayName() const override { return tr("Import"); }
    QIcon icon() const override { return QIcon::fromTheme("viewfolder"); }
    QString id() const override { return "import"; }
    QString helpPage() const override { return "Import-instance"; }
    void openedImpl() override { update(); }

    void setArchive(const QString& path);

   private:
    void update();

    NewInstanceDialog* m_dialog;
    QLineEdit* m_path = nullptr;
    QLabel* m_info = nullptr;
};
