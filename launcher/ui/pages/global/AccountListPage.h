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

#include <QMainWindow>

#include "ui/pages/BasePage.h"

class AccountList;
class QTreeView;

/// Nexus Mods accounts; the default one is used for downloads and update checks
class AccountListPage : public QMainWindow, public BasePage {
    Q_OBJECT
   public:
    explicit AccountListPage(QWidget* parent = nullptr);
    ~AccountListPage() override = default;

    QString displayName() const override { return tr("Accounts"); }
    QIcon icon() const override { return QIcon::fromTheme("accounts"); }
    QString id() const override { return "accounts"; }
    QString helpPage() const override { return "Nexus-accounts"; }

   private:
    void updateButtons();
    int selectedRow() const;

    AccountList* m_accounts;
    QTreeView* m_view = nullptr;
    QAction* m_refresh = nullptr;
    QAction* m_remove = nullptr;
    QAction* m_default = nullptr;
};
