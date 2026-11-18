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

#include <QPlainTextEdit>
#include <QStringConverter>
#include <QWidget>

#include "Result.h"
#include "game/Ini.h"

class GameInstance;
class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

/// Plain text editing with line numbers in the margin
class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
   public:
    explicit CodeEditor(QWidget* parent = nullptr);
    int lineNumberWidth() const;
    void paintLineNumbers(QPaintEvent* event);

   protected:
    void resizeEvent(QResizeEvent* event) override;

   private:
    QWidget* m_lineNumbers;
};

/**
 * Edits the INI files of an instance: the game's settings in its My Games folder and every INI in its game folder.
 * Saving replaces the file instead of writing into it, so a file that is a hard link (to the Steam installation or a
 * mod's staging folder) never changes anywhere else; INI files mods deployed are saved into the mod and linked again.
 */
class IniEditor : public QWidget {
    Q_OBJECT
   public:
    explicit IniEditor(GameInstance* instance, QWidget* parent = nullptr);

    /// Asks what to do with unsaved changes; false when the user cancelled
    bool maybeSave();
    void reloadFiles();

   private:
    struct File {
        QString path;  ///< where the game reads it
        QString label;
        QString modId;  ///< the mod that deployed it, empty otherwise
        bool settings = false;  ///< one of the game's settings files in My Games
    };

    void openFile(int index);
    Result<> save();
    void revert();
    void find(bool backwards);
    void showSettingsView(bool settings);
    void rebuildSettings();
    void settingEdited(QTreeWidgetItem* item, int column);
    void updateState();
    void fileChangedOnDisk();

    GameInstance* m_instance;
    QList<File> m_files;
    int m_current = -1;
    Ini::Text m_loaded;
    bool m_exists = false;
    bool m_saving = false;
    bool m_updatingSettings = false;

    QTreeWidget* m_fileList = nullptr;
    QLabel* m_path = nullptr;
    QToolButton* m_textButton = nullptr;
    QToolButton* m_settingsButton = nullptr;
    QLineEdit* m_find = nullptr;
    QToolButton* m_save = nullptr;
    QToolButton* m_revert = nullptr;
    QStackedWidget* m_views = nullptr;
    CodeEditor* m_editor = nullptr;
    QTreeWidget* m_settings = nullptr;
    QLabel* m_status = nullptr;
    QFileSystemWatcher* m_watcher = nullptr;
};
