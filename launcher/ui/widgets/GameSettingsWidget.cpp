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

#include "GameSettingsWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "Application.h"
#include "Json.h"
#include "game/GameInstance.h"
#include "settings/SettingsObject.h"
#include "ui/widgets/CustomCommands.h"
#include "ui/widgets/EnvironmentVariables.h"
#include "ui/widgets/IniEditor.h"
#include "ui/widgets/IniSettingsView.h"

GameSettingsWidget::GameSettingsWidget(GameInstance* instance, QWidget* parent) : QWidget(parent), m_instance(instance)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* tabs = new QTabWidget(this);
    if (m_instance) {
        tabs->addTab(createGameTab(), tr("Game"));
    }
    tabs->addTab(createConsoleTab(), tr("General"));
    m_customCommands = new CustomCommands(tabs);
    tabs->addTab(m_customCommands, tr("Custom Commands"));
    m_environment = new EnvironmentVariables(tabs);
    tabs->addTab(m_environment, tr("Environment Variables"));
    if (m_instance) {
        const auto domain = m_instance->definition().domain;
        if (const auto& catalog = IniSettings::forGame(domain); !catalog.isEmpty()) {
            m_iniSettings = new IniSettingsView(m_instance, catalog, tabs);
            tabs->addTab(m_iniSettings, tr("INI Settings"));
        }
        m_iniEditor = new IniEditor(m_instance, tabs);
        tabs->addTab(m_iniEditor, tr("INI Files"));
        // both views edit the same files: leaving one settles its changes, and the other shows what is on disk
        connect(tabs, &QTabWidget::currentChanged, this, [this, tabs](int index) {
            auto* previous = tabs->widget(m_currentTab);
            const bool settled = previous == m_iniSettings ? m_iniSettings->maybeSave() : previous == m_iniEditor ? m_iniEditor->maybeSave() : true;
            if (!settled) {
                const QSignalBlocker block(tabs);
                tabs->setCurrentIndex(m_currentTab);
                return;
            }
            m_currentTab = index;
            if (tabs->widget(index) == m_iniSettings) {
                m_iniSettings->reload();
            } else if (tabs->widget(index) == m_iniEditor) {
                m_iniEditor->reloadFiles();
            }
        });
    }
    layout->addWidget(tabs);
    loadSettings();
}

bool GameSettingsWidget::confirmIniChanges()
{
    return (!m_iniSettings || m_iniSettings->maybeSave()) && (!m_iniEditor || m_iniEditor->maybeSave());
}

QWidget* GameSettingsWidget::createGameTab()
{
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);

    auto* launchBox = new QGroupBox(tr("Launching"), tab);
    auto* launchForm = new QFormLayout(launchBox);
    m_launchExecutable = new QComboBox(launchBox);
    m_launchExecutable->setToolTip(tr("What the Launch button starts. A script extender is picked automatically once it is installed."));
    m_arguments = new QLineEdit(launchBox);
    m_arguments->setToolTip(tr("Command line arguments for the game and its script extender. $INST_* variables are expanded."));
    launchForm->addRow(tr("Launch:"), m_launchExecutable);
    launchForm->addRow(tr("Game arguments:"), m_arguments);
    layout->addWidget(launchBox);

    auto* toolsBox = new QGroupBox(tr("Tools"), tab);
    auto* toolsLayout = new QVBoxLayout(toolsBox);
    auto* toolsInfo = new QLabel(tr("Tools started from here (xEdit, LOOT, BodySlide, ...) see this instance's game folder, load order and "
                                    "profile, exactly like the game. Paths may be relative to the game folder. Files a tool writes into the "
                                    "game folder go into its output mod once it exits, so they can be disabled or removed like any mod; "
                                    "with no output mod they stay in the game folder."),
                                 toolsBox);
    toolsInfo->setWordWrap(true);
    toolsLayout->addWidget(toolsInfo);
    m_executables = new QTableWidget(0, 5, toolsBox);
    m_executables->setHorizontalHeaderLabels({ tr("Name"), tr("Executable"), tr("Arguments"), tr("Working folder"), tr("Output mod") });
    m_executables->horizontalHeaderItem(4)->setToolTip(tr("The mod that files the tool writes into the game folder go into (made when "
                                                          "needed). Empty: they stay in the game folder."));
    m_executables->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    // a long path keeps its file name in sight instead of wrapping to "C:..."
    m_executables->setWordWrap(false);
    m_executables->setTextElideMode(Qt::ElideMiddle);
    m_executables->verticalHeader()->hide();
    toolsLayout->addWidget(m_executables);
    auto* toolButtons = new QHBoxLayout();
    auto* add = new QPushButton(tr("&Add Tool..."), toolsBox);
    auto* remove = new QPushButton(tr("&Remove Tool"), toolsBox);
    toolButtons->addWidget(add);
    toolButtons->addWidget(remove);
    toolButtons->addStretch(1);
    toolsLayout->addLayout(toolButtons);
    connect(add, &QPushButton::clicked, this, [this] {
        const auto file = QFileDialog::getOpenFileName(this, tr("Choose a tool"), m_instance->gameRoot(), tr("Programs (*.exe *.bat *.cmd)"));
        if (!file.isEmpty()) {
            // inside the game folder the path is kept relative, so copies and exports of the instance use their own tool
            const auto relative = QDir(m_instance->gameRoot()).relativeFilePath(file);
            const auto path = relative.startsWith("..") || QDir::isAbsolutePath(relative) ? file : relative;
            const auto name = QFileInfo(file).completeBaseName();
            addExecutableRow(name, QDir::toNativeSeparators(path), QString(), QString(), tr("%1 Output").arg(name));
        }
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (const int row = m_executables->currentRow(); row >= 0) {
            m_executables->removeRow(row);
        }
    });
    layout->addWidget(toolsBox, 1);

    auto* gameBox = new QGroupBox(tr("Game"), tab);
    auto* gameForm = new QFormLayout(gameBox);
    m_domain = new QLineEdit(gameBox);
    m_domain->setToolTip(tr("The game's id in Nexus Mods links, e.g. skyrimspecialedition in nexusmods.com/skyrimspecialedition."));
    m_gameName = new QLineEdit(gameBox);
    m_executable = new QLineEdit(gameBox);
    m_scriptExtender = new QLineEdit(gameBox);
    m_dataPath = new QLineEdit(gameBox);
    m_dataPath->setToolTip(tr("Folder loose mod files go in, relative to the game folder. Empty for the game folder itself."));
    m_myGames = new QLineEdit(gameBox);
    m_myGames->setToolTip(tr("Folder in Documents\\My Games with the game's INI files and saves."));
    m_appData = new QLineEdit(gameBox);
    m_appData->setToolTip(tr("Folder in AppData\\Local with the game's plugins.txt."));
    m_pluginFormat = new QComboBox(gameBox);
    m_pluginFormat->addItem(tr("No plugins"), "none");
    m_pluginFormat->addItem(tr("plugins.txt with * markers (Skyrim SE, Fallout 4, Starfield)"), "asterisk");
    m_pluginFormat->addItem(tr("plugins.txt and loadorder.txt (Skyrim)"), "textfile");
    m_pluginFormat->addItem(tr("plugins.txt and file dates (Oblivion, Fallout 3, New Vegas)"), "timestamp");
    gameForm->addRow(tr("Nexus Mods game id:"), m_domain);
    gameForm->addRow(tr("Name:"), m_gameName);
    gameForm->addRow(tr("Executable:"), m_executable);
    gameForm->addRow(tr("Script extender:"), m_scriptExtender);
    gameForm->addRow(tr("Data folder:"), m_dataPath);
    gameForm->addRow(tr("My Games folder:"), m_myGames);
    gameForm->addRow(tr("AppData folder:"), m_appData);
    gameForm->addRow(tr("Plugins:"), m_pluginFormat);
    layout->addWidget(gameBox);
    return tab;
}

QWidget* GameSettingsWidget::createConsoleTab()
{
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);

    m_consoleBox = new QGroupBox(tr("Console"), tab);
    m_consoleBox->setCheckable(m_instance != nullptr);
    auto* consoleLayout = new QVBoxLayout(m_consoleBox);
    m_showConsole = new QCheckBox(tr("Show console while the game is running"), m_consoleBox);
    m_autoCloseConsole = new QCheckBox(tr("Automatically close console when the game quits"), m_consoleBox);
    m_showConsoleOnError = new QCheckBox(tr("Show console when the game crashes"), m_consoleBox);
    consoleLayout->addWidget(m_showConsole);
    consoleLayout->addWidget(m_autoCloseConsole);
    consoleLayout->addWidget(m_showConsoleOnError);
    layout->addWidget(m_consoleBox);

    m_gameTimeBox = new QGroupBox(tr("Play Time"), tab);
    m_gameTimeBox->setCheckable(m_instance != nullptr);
    auto* timeLayout = new QVBoxLayout(m_gameTimeBox);
    m_showGameTime = new QCheckBox(m_instance ? tr("Show time spent playing this instance") : tr("Show time spent playing instances"), m_gameTimeBox);
    m_recordGameTime = new QCheckBox(m_instance ? tr("Record time spent playing this instance") : tr("Record time spent playing instances"),
                                     m_gameTimeBox);
    m_countGameTime = new QCheckBox(tr("Count this instance in the total play time"), m_gameTimeBox);
    m_showGlobalGameTime = new QCheckBox(tr("Show the total play time"), m_gameTimeBox);
    m_showGameTimeWithoutDays = new QCheckBox(tr("Show play time in hours only"), m_gameTimeBox);
    timeLayout->addWidget(m_showGameTime);
    timeLayout->addWidget(m_recordGameTime);
    timeLayout->addWidget(m_countGameTime);
    timeLayout->addWidget(m_showGlobalGameTime);
    timeLayout->addWidget(m_showGameTimeWithoutDays);
    m_countGameTime->setVisible(m_instance != nullptr);
    m_showGlobalGameTime->setVisible(m_instance == nullptr);
    m_showGameTimeWithoutDays->setVisible(m_instance == nullptr);
    layout->addWidget(m_gameTimeBox);

    if (!m_instance) {
        auto* launcherBox = new QGroupBox(tr("Launcher"), tab);
        auto* launcherLayout = new QVBoxLayout(launcherBox);
        m_closeAfterLaunch = new QCheckBox(tr("Hide the launcher while a game is running"), launcherBox);
        m_quitAfterGameStop = new QCheckBox(tr("Quit the launcher after the game stops"), launcherBox);
        launcherLayout->addWidget(m_closeAfterLaunch);
        launcherLayout->addWidget(m_quitAfterGameStop);
        layout->addWidget(launcherBox);
    }
    layout->addStretch(1);
    return tab;
}

void GameSettingsWidget::addExecutableRow(const QString& name, const QString& path, const QString& arguments, const QString& workingDir, const QString& outputMod)
{
    const int row = m_executables->rowCount();
    m_executables->insertRow(row);
    m_executables->setItem(row, 0, new QTableWidgetItem(name));
    auto* pathItem = new QTableWidgetItem(path);
    pathItem->setToolTip(path);
    m_executables->setItem(row, 1, pathItem);
    m_executables->setItem(row, 2, new QTableWidgetItem(arguments));
    m_executables->setItem(row, 3, new QTableWidgetItem(workingDir));
    m_executables->setItem(row, 4, new QTableWidgetItem(outputMod));
}

void GameSettingsWidget::loadSettings()
{
    SettingsObject* settings = m_instance ? m_instance->settings() : APPLICATION->settings();

    if (m_iniEditor) {
        // mods installed since the page was last open may have brought INI files
        m_iniEditor->reloadFiles();
    }
    if (m_instance) {
        m_domain->setText(settings->get("GameDomain").toString());
        m_gameName->setText(settings->get("GameName").toString());
        m_executable->setText(settings->get("GameExecutable").toString());
        m_scriptExtender->setText(settings->get("ScriptExtender").toString());
        // left empty, the game's own loader is used
        m_scriptExtender->setPlaceholderText(GameRegistry::byDomain(settings->get("GameDomain").toString()).scriptExtender);
        m_dataPath->setText(settings->get("DataPath").toString());
        m_myGames->setText(settings->get("MyGamesFolder").toString());
        m_appData->setText(settings->get("AppDataFolder").toString());
        m_pluginFormat->setCurrentIndex(std::max(0, m_pluginFormat->findData(settings->get("PluginFormat").toString())));
        m_arguments->setText(settings->get("GameArguments").toString());
        m_launchExecutable->clear();
        for (const auto& exe : m_instance->executables()) {
            m_launchExecutable->addItem(exe.name);
        }
        m_launchExecutable->setCurrentText(m_instance->defaultExecutableName());
        m_executables->setRowCount(0);
        for (const auto& exe : m_instance->customExecutables()) {
            addExecutableRow(exe.name, QDir::toNativeSeparators(exe.path), exe.arguments, QDir::toNativeSeparators(exe.workingDir), exe.outputMod);
        }
        m_consoleBox->setChecked(settings->get("OverrideConsole").toBool());
        m_gameTimeBox->setChecked(settings->get("OverrideGameTime").toBool());
        m_countGameTime->setChecked(settings->get("CountGameTime").toBool());
    } else {
        m_showGlobalGameTime->setChecked(settings->get("ShowGlobalGameTime").toBool());
        m_showGameTimeWithoutDays->setChecked(settings->get("ShowGameTimeWithoutDays").toBool());
        m_closeAfterLaunch->setChecked(settings->get("CloseAfterLaunch").toBool());
        m_quitAfterGameStop->setChecked(settings->get("QuitAfterGameStop").toBool());
    }
    m_showConsole->setChecked(settings->get("ShowConsole").toBool());
    m_autoCloseConsole->setChecked(settings->get("AutoCloseConsole").toBool());
    m_showConsoleOnError->setChecked(settings->get("ShowConsoleOnError").toBool());
    m_showGameTime->setChecked(settings->get("ShowGameTime").toBool());
    m_recordGameTime->setChecked(settings->get("RecordGameTime").toBool());

    m_customCommands->initialize(m_instance != nullptr, m_instance == nullptr || settings->get("OverrideCommands").toBool(),
                                 settings->get("PreLoadCommand").toString(), settings->get("PreLaunchCommand").toString(),
                                 settings->get("PostExitCommand").toString());
    m_environment->initialize(m_instance != nullptr, m_instance == nullptr || settings->get("OverrideEnv").toBool(),
                              Json::toMap(settings->get("Env").toString()));
}

void GameSettingsWidget::saveSettings()
{
    SettingsObject* settings = m_instance ? m_instance->settings() : APPLICATION->settings();
    SettingsObject::Lock lock(settings);

    if (m_instance) {
        settings->set("GameDomain", m_domain->text().trimmed().toLower());
        settings->set("GameName", m_gameName->text().trimmed());
        settings->set("GameExecutable", QDir::fromNativeSeparators(m_executable->text().trimmed()));
        settings->set("ScriptExtender", QDir::fromNativeSeparators(m_scriptExtender->text().trimmed()));
        settings->set("DataPath", QDir::fromNativeSeparators(m_dataPath->text().trimmed()));
        settings->set("MyGamesFolder", m_myGames->text().trimmed());
        settings->set("AppDataFolder", m_appData->text().trimmed());
        settings->set("PluginFormat", m_pluginFormat->currentData().toString());
        settings->set("GameArguments", m_arguments->text());
        QList<GameExecutable> tools;
        for (int row = 0; row < m_executables->rowCount(); row++) {
            auto text = [this, row](int column) { return m_executables->item(row, column) ? m_executables->item(row, column)->text().trimmed() : QString(); };
            if (!text(0).isEmpty() && !text(1).isEmpty()) {
                tools << GameExecutable{ .name = text(0),
                                         .path = QDir::fromNativeSeparators(text(1)),
                                         .arguments = text(2),
                                         .workingDir = QDir::fromNativeSeparators(text(3)),
                                         .outputMod = text(4) };
            }
        }
        m_instance->setCustomExecutables(tools);
        settings->set("LaunchExecutable", m_launchExecutable->currentText());
        settings->set("OverrideConsole", m_consoleBox->isChecked());
        settings->set("OverrideGameTime", m_gameTimeBox->isChecked());
        settings->set("CountGameTime", m_countGameTime->isChecked());
    } else {
        settings->set("ShowGlobalGameTime", m_showGlobalGameTime->isChecked());
        settings->set("ShowGameTimeWithoutDays", m_showGameTimeWithoutDays->isChecked());
        settings->set("CloseAfterLaunch", m_closeAfterLaunch->isChecked());
        settings->set("QuitAfterGameStop", m_quitAfterGameStop->isChecked());
    }

    if (!m_instance || m_consoleBox->isChecked()) {
        settings->set("ShowConsole", m_showConsole->isChecked());
        settings->set("AutoCloseConsole", m_autoCloseConsole->isChecked());
        settings->set("ShowConsoleOnError", m_showConsoleOnError->isChecked());
    } else {
        settings->reset("ShowConsole");
        settings->reset("AutoCloseConsole");
        settings->reset("ShowConsoleOnError");
    }
    if (!m_instance || m_gameTimeBox->isChecked()) {
        settings->set("ShowGameTime", m_showGameTime->isChecked());
        settings->set("RecordGameTime", m_recordGameTime->isChecked());
    } else {
        settings->reset("ShowGameTime");
        settings->reset("RecordGameTime");
    }

    const bool commands = !m_instance || m_customCommands->checked();
    if (m_instance) {
        settings->set("OverrideCommands", commands);
    }
    if (commands) {
        settings->set("PreLoadCommand", m_customCommands->preLoadCommand());
        settings->set("PreLaunchCommand", m_customCommands->prelaunchCommand());
        settings->set("PostExitCommand", m_customCommands->postexitCommand());
    } else {
        settings->reset("PreLoadCommand");
        settings->reset("PreLaunchCommand");
        settings->reset("PostExitCommand");
    }

    const bool env = !m_instance || m_environment->override();
    if (m_instance) {
        settings->set("OverrideEnv", env);
    }
    if (env) {
        settings->set("Env", Json::fromMap(m_environment->value()));
    } else {
        settings->reset("Env");
    }
}
