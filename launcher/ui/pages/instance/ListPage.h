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
#include <QSortFilterProxyModel>
#include <functional>

#include "ui/pages/BasePage.h"

class QLabel;
class QLineEdit;
class QTreeView;
class WideBar;

/// The search filter of a list page, which can also leave out rows while nothing is searched for
class ListFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
   public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    /// @p hidden tells which source rows to leave out while the search is empty
    void setRowHider(std::function<bool(int)> hidden);
    /// Asks the row hider again
    void refilter() { invalidateRowsFilter(); }

   protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

   private:
    std::function<bool(int)> m_hidden;
};

/// The layout of the instance's list pages: a searchable list with an action bar on its right
class ListPage : public QMainWindow, public BasePage {
    Q_OBJECT
   public:
    explicit ListPage(QWidget* parent = nullptr);
    ~ListPage() override = default;

   protected:
    /// Shows @p model through a filter on @p filterColumn
    void setModel(QAbstractItemModel* model, int filterColumn);
    QAction* addAction(const QString& text, const QString& toolTip, const QString& icon = {});
    void addSeparator();
    /// Selected rows of the source model, sorted
    QList<int> selectedRows() const;
    void setCounts(const QString& text);
    QString filterText() const;

    QTreeView* m_view = nullptr;
    ListFilterProxy* m_proxy = nullptr;
    WideBar* m_toolbar = nullptr;

   private:
    QLabel* m_counts = nullptr;
    QLineEdit* m_filter = nullptr;
};
