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

#include "AccountListPage.h"

#include <QHeaderView>
#include <QMessageBox>
#include <QTreeView>

#include "Application.h"
#include "nexus/NexusAccount.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/NexusLoginDialog.h"
#include "ui/widgets/WideBar.h"

AccountListPage::AccountListPage(QWidget* parent) : QMainWindow(parent), m_accounts(APPLICATION->accounts())
{
    m_view = new QTreeView(this);
    m_view->setModel(m_accounts);
    m_view->setRootIsDecorated(false);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->header()->setSectionResizeMode(AccountList::NameColumn, QHeaderView::Stretch);
    setCentralWidget(m_view);

    auto* toolbar = new WideBar(tr("Actions"), this);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->setMovable(false);
    addToolBar(Qt::RightToolBarArea, toolbar);
    auto* add = new QAction(tr("&Add Account"), this);
    add->setToolTip(tr("Optional: a Nexus Mods personal API key lets Premium members download without clicking."));
    m_refresh = new QAction(tr("&Refresh"), this);
    m_refresh->setToolTip(tr("Check the key again, for example after your membership changed."));
    m_remove = new QAction(tr("Remo&ve"), this);
    m_default = new QAction(tr("&Set Default"), this);
    m_default->setToolTip(tr("Use this account's key for Premium downloads."));
    toolbar->addAction(add);
    toolbar->addAction(m_refresh);
    toolbar->addAction(m_remove);
    toolbar->addAction(m_default);

    connect(add, &QAction::triggered, this, [this] {
        if (auto account = NexusLoginDialog::ask(this)) {
            m_accounts->upsert(*account);
        }
    });
    connect(m_refresh, &QAction::triggered, this, [this] {
        const int row = selectedRow();
        if (row < 0) {
            return;
        }
        auto account = NexusLoginDialog::validate(m_accounts->accounts()[row].apiKey, this);
        if (!account) {
            CustomMessageBox::selectable(this, tr("Refresh account"), account.error(), QMessageBox::Warning)->exec();
            return;
        }
        m_accounts->upsert(*account);
    });
    connect(m_remove, &QAction::triggered, this, [this] {
        const int row = selectedRow();
        if (row < 0) {
            return;
        }
        const auto answer = QMessageBox::question(
            this, tr("Remove account"), tr("Remove %1? Its API key is deleted from this computer.").arg(m_accounts->accounts()[row].name));
        if (answer == QMessageBox::Yes) {
            m_accounts->remove(row);
        }
    });
    connect(m_default, &QAction::triggered, this, [this] { m_accounts->setDefault(selectedRow()); });
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &AccountListPage::updateButtons);
    connect(m_accounts, &AccountList::listChanged, this, &AccountListPage::updateButtons);
    updateButtons();
}

int AccountListPage::selectedRow() const
{
    const auto rows = m_view->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

void AccountListPage::updateButtons()
{
    const int row = selectedRow();
    m_refresh->setEnabled(row >= 0);
    m_remove->setEnabled(row >= 0);
    m_default->setEnabled(row >= 0 && row != m_accounts->defaultIndex());
}
