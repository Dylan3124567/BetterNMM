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

#include "ListPage.h"

#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QTreeView>

#include <algorithm>

#include "ui/widgets/WideBar.h"

ListPage::ListPage(QWidget* parent) : QMainWindow(parent)
{
    auto* central = new QWidget(this);
    auto* layout = new QGridLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);

    m_counts = new QLabel(central);
    m_counts->setTextFormat(Qt::RichText);
    m_counts->setMinimumWidth(100);
    layout->addWidget(m_counts, 0, 0);

    m_view = new QTreeView(central);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setRootIsDecorated(false);
    m_view->setUniformRowHeights(true);
    m_view->header()->setStretchLastSection(false);
    layout->addWidget(m_view, 1, 0);

    m_filter = new QLineEdit(central);
    m_filter->setPlaceholderText(tr("Search"));
    m_filter->setClearButtonEnabled(true);
    layout->addWidget(m_filter, 2, 0);
    setCentralWidget(central);

    m_toolbar = new WideBar(tr("Actions"), this);
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toolbar->setFloatable(false);
    m_toolbar->setMovable(false);
    addToolBar(Qt::RightToolBarArea, m_toolbar);

    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    connect(m_filter, &QLineEdit::textChanged, m_proxy, &QSortFilterProxyModel::setFilterFixedString);
}

void ListPage::setModel(QAbstractItemModel* model, int filterColumn)
{
    m_proxy->setSourceModel(model);
    m_proxy->setFilterKeyColumn(filterColumn);
    m_view->setModel(m_proxy);
}

QAction* ListPage::addAction(const QString& text, const QString& toolTip, const QString& icon)
{
    auto* action = new QAction(text, this);
    action->setToolTip(toolTip);
    if (!icon.isEmpty()) {
        action->setIcon(QIcon::fromTheme(icon));
    }
    m_toolbar->addAction(action);
    return action;
}

void ListPage::addSeparator()
{
    m_toolbar->addSeparator();
}

QList<int> ListPage::selectedRows() const
{
    QList<int> rows;
    for (const auto& index : m_view->selectionModel()->selectedRows()) {
        rows << m_proxy->mapToSource(index).row();
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

void ListPage::setCounts(const QString& text)
{
    m_counts->setText(text);
}

QString ListPage::filterText() const
{
    return m_filter->text();
}
