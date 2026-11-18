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

#include <functional>

#include "Result.h"
#include "game/IniSettings.h"

class GameInstance;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;

/**
 * The game's INI settings as named controls with explanations, grouped like BethINI: display, quality, view distance,
 * memory and so on, plus the game's own quality presets. Values are read the way the game reads them and saved into the
 * file the game takes them from; every other line of the files stays as it was.
 */
class IniSettingsView : public QWidget {
    Q_OBJECT
   public:
    IniSettingsView(GameInstance* instance, const IniSettings::Catalog& catalog, QWidget* parent = nullptr);

    /// Asks what to do with unsaved changes; false when the user cancelled
    bool maybeSave();
    /// Reads the files again, unless there are unsaved changes
    void reload();

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    QWidget* createPage(const IniSettings::Category& category);
    QWidget* createControl(const IniSettings::Setting& setting);
    void setValues(const IniSettings::Setting& setting, const QStringList& values);
    void setResolution(const IniSettings::Setting& setting, const QString& text);
    void refresh();
    void showFiles();
    Result<> save();
    void revert();
    void updateState();

    GameInstance* m_instance;
    IniSettings::Catalog m_catalog;
    IniSettings::Files m_files;
    QList<std::function<void()>> m_refreshers;  ///< show the current values in the controls

    QWidget* m_presetBar = nullptr;
    QList<QPushButton*> m_presets;
    QListWidget* m_categories = nullptr;
    QStackedWidget* m_pages = nullptr;
    QPushButton* m_save = nullptr;
    QPushButton* m_revert = nullptr;
    QLabel* m_status = nullptr;
};
