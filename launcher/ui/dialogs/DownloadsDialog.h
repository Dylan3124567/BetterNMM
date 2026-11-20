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

#include <QDialog>
#include <QHash>

class DownloadManager;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

/// Every download with its progress and speed; pause, resume and cancel. It does not block anything: downloads go on
/// whether it is open or not.
class DownloadsDialog : public QDialog {
    Q_OBJECT
   public:
    explicit DownloadsDialog(DownloadManager* manager, QWidget* parent = nullptr);

   private:
    void addRow(const QString& id);
    void updateRow(const QString& id);
    void removeRow(const QString& id);
    void updateSummary();
    void updateButtons();
    QStringList selectedIds() const;

    DownloadManager* m_manager;
    QTreeWidget* m_list;
    QLabel* m_summary;
    QPushButton* m_pause;
    QPushButton* m_resume;
    QPushButton* m_cancel;
    QPushButton* m_clear;
    QHash<QString, QTreeWidgetItem*> m_rows;
};
