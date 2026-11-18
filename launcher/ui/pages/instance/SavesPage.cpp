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

#include "SavesPage.h"

#include <QDir>
#include <QHeaderView>
#include <QLocale>
#include <QStandardItemModel>
#include <QTreeView>

#include "DesktopServices.h"
#include "game/GameInstance.h"

SavesPage::SavesPage(GameInstance* instance, QWidget* parent) : ListPage(parent), m_instance(instance), m_model(new QStandardItemModel(this))
{
    m_model->setHorizontalHeaderLabels({ tr("Save"), tr("Last played"), tr("Size") });
    setModel(m_model, 0);
    m_view->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setToolTip(instance->isMain() ? tr("Your saves for the Steam installation. BetterNMM only shows them; it never changes them.")
                                          : tr("This instance's own saves. Other instances and the Steam installation never see them."));

    auto* folder = addAction(tr("View &Folder"), tr("Open the saves folder."), "viewfolder");
    auto* reload = addAction(tr("&Refresh"), tr("Read the saves folder again."), "refresh");
    connect(folder, &QAction::triggered, this, [this] { DesktopServices::openPath(m_instance->savesDir(), !m_instance->isMain()); });
    connect(reload, &QAction::triggered, this, &SavesPage::refresh);
}

bool SavesPage::shouldDisplay() const
{
    return !m_instance->savesDir().isEmpty();
}

void SavesPage::openedImpl()
{
    refresh();
}

void SavesPage::refresh()
{
    m_model->removeRows(0, m_model->rowCount());
    const auto entries = QDir(m_instance->savesDir()).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Time);
    for (const auto& entry : entries) {
        auto* name = new QStandardItem(entry.completeBaseName());
        name->setToolTip(QDir::toNativeSeparators(entry.absoluteFilePath()));
        auto* time = new QStandardItem(QLocale().toString(entry.lastModified(), QLocale::ShortFormat));
        auto* size = new QStandardItem(QLocale().formattedDataSize(entry.size()));
        m_model->appendRow({ name, time, size });
    }
    m_view->resizeColumnToContents(1);
    m_view->resizeColumnToContents(2);
    setCounts(tr("%n save file(s)", nullptr, static_cast<int>(entries.size())));
}
