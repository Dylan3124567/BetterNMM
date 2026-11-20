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

class GameInstance;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLineEdit;
class QTableWidget;
class CustomCommands;
class EnvironmentVariables;
class IniEditor;
class IniSettingsView;

/**
 * Launch settings. With an instance it edits that instance (the game definition, its executables and overrides of the
 * global settings); without one it edits the global defaults every instance starts from.
 */
class GameSettingsWidget : public QWidget {
    Q_OBJECT
   public:
    explicit GameSettingsWidget(GameInstance* instance, QWidget* parent = nullptr);

    void loadSettings();
    void saveSettings();
    /// Asks about unsaved INI edits; false when the user cancelled
    bool confirmIniChanges();

   private:
    QWidget* createGameTab();
    QWidget* createConsoleTab();
    void addExecutableRow(const QString& name, const QString& path, const QString& arguments, const QString& workingDir, const QString& outputMod);

    GameInstance* m_instance;

    // game tab (instances only)
    QLineEdit* m_domain = nullptr;
    QLineEdit* m_gameName = nullptr;
    QLineEdit* m_executable = nullptr;
    QLineEdit* m_scriptExtender = nullptr;
    QLineEdit* m_dataPath = nullptr;
    QLineEdit* m_myGames = nullptr;
    QLineEdit* m_appData = nullptr;
    QComboBox* m_pluginFormat = nullptr;
    QComboBox* m_launchExecutable = nullptr;
    QLineEdit* m_arguments = nullptr;
    QTableWidget* m_executables = nullptr;

    // console and play time
    QGroupBox* m_consoleBox = nullptr;
    QCheckBox* m_showConsole = nullptr;
    QCheckBox* m_autoCloseConsole = nullptr;
    QCheckBox* m_showConsoleOnError = nullptr;
    QGroupBox* m_gameTimeBox = nullptr;
    QCheckBox* m_showGameTime = nullptr;
    QCheckBox* m_recordGameTime = nullptr;
    QCheckBox* m_countGameTime = nullptr;
    QCheckBox* m_showGlobalGameTime = nullptr;
    QCheckBox* m_showGameTimeWithoutDays = nullptr;
    QCheckBox* m_closeAfterLaunch = nullptr;
    QCheckBox* m_quitAfterGameStop = nullptr;

    CustomCommands* m_customCommands = nullptr;
    EnvironmentVariables* m_environment = nullptr;
    IniEditor* m_iniEditor = nullptr;
    IniSettingsView* m_iniSettings = nullptr;
    int m_currentTab = 0;
};
