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
#include <QMap>
#include <QSet>

#include "net/NetJob.h"
#include "nexus/NexusApi.h"

class GameInstance;
class ProjectDescriptionPage;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTimer;
class QToolButton;

/// Searches Nexus Mods for the instance's game the way Prism's mod downloader searches Modrinth: filters, sorting,
/// endless scrolling, a full description, and mods picked for download that are reviewed and installed together
class NexusBrowseDialog : public QDialog {
    Q_OBJECT
   public:
    explicit NexusBrowseDialog(GameInstance* instance, QWidget* parent = nullptr);
    ~NexusBrowseDialog() override;

   private:
    struct Selection {
        Nexus::ModInfo mod;
        Nexus::ModFile file;
    };

    Nexus::SearchOptions currentOptions() const;
    void search(bool more);
    void searchFinished(bool more);
    void loadGameAndCategories();
    void loadThumbnail(int row, const QString& url);
    void showMod();
    void loadDetails(int modId);
    void showFiles(int modId);
    void showDescription(int modId);
    void toggleSelection(int row);
    void updateSelectionUi();
    void updateFilterButton();
    void reviewAndConfirm();
    int currentRow() const;

    GameInstance* m_instance;
    QString m_game;
    int m_gameId = 0;
    QList<Nexus::ModInfo> m_mods;
    int m_total = 0;
    int m_generation = 0;           ///< bumped by every new search, so late thumbnails of old results are dropped
    bool m_searchAgain = false;     ///< the filters changed while a search was running
    int m_selectWhenLoaded = 0;     ///< mod to pick for download once its files are known
    QHash<int, QList<Nexus::ModFile>> m_files;
    QHash<int, QString> m_descriptions;
    QMap<int, Selection> m_selected;
    QSet<int> m_installed;

    QToolButton* m_filterButton = nullptr;
    QLineEdit* m_query = nullptr;
    QFrame* m_filters = nullptr;
    QListWidget* m_categories = nullptr;
    QComboBox* m_updated = nullptr;
    QLineEdit* m_author = nullptr;
    QCheckBox* m_adult = nullptr;
    QListWidget* m_list = nullptr;
    QComboBox* m_sort = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_stats = nullptr;
    ProjectDescriptionPage* m_description = nullptr;
    QComboBox* m_fileBox = nullptr;
    QPushButton* m_select = nullptr;
    QPushButton* m_review = nullptr;
    QLabel* m_status = nullptr;
    QTimer* m_searchTimer = nullptr;

    NetJob::Ptr m_searchJob;
    NetJob::Ptr m_gameJob;
    NetJob::Ptr m_detailsJob;
    QByteArray* m_searchData = nullptr;  // owned by the request in m_searchJob
    QByteArray* m_gameData = nullptr;    // owned by the requests in m_gameJob
    QByteArray* m_categoryData = nullptr;
    QByteArray* m_filesData = nullptr;   // owned by the requests in m_detailsJob
    QByteArray* m_descriptionData = nullptr;
    int m_detailsModId = 0;
};
