// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2022 Tayou <git@tayou.org>
 *  Copyright (C) 2023 TheKodeToad <TheKodeToad@proton.me>
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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#pragma once

#include <memory>

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFlag>
#include <QIcon>
#include <QMutex>
#include <QPointer>
#include <QUrl>

#include "QObjectPtr.h"

class LaunchController;
class LocalPeer;
class InstanceWindow;
class MainWindow;
class ViewLogWindow;
class GenericPageProvider;
class QFile;
class HttpMetaCache;
class SettingsObject;
class InstanceList;
class AccountList;
class NexusDownloadQueue;
class DownloadManager;
class DownloadsDialog;
class IconList;
class QNetworkAccessManager;
class ITheme;
class ThemeManager;
class IconTheme;
class BaseInstance;
class GameInstance;

class LogModel;

#if defined(APPLICATION)
#undef APPLICATION
#endif
#define APPLICATION (static_cast<Application*>(QCoreApplication::instance()))

// Used for checking if is a test
#if defined(APPLICATION_DYN)
#undef APPLICATION_DYN
#endif
#define APPLICATION_DYN (dynamic_cast<Application*>(QCoreApplication::instance()))

class Application : public QApplication {
    Q_OBJECT
   public:
    enum Status : std::uint8_t { StartingUp, Failed, Succeeded, Initialized };

   public:
    Application(int& argc, char** argv);
    ~Application() override;

    bool event(QEvent* event) override;

    SettingsObject* settings() const { return m_settings.get(); }
    SettingsObject* playtimeSettings() const { return m_playtimeSettings.get(); }

    static QIcon logo();

    ThemeManager* themeManager() { return m_themeManager.get(); }

    InstanceList* instances() const { return m_instances.get(); }

    IconList* icons() const { return m_icons.get(); }

    AccountList* accounts() const { return m_accounts.get(); }

    /// Downloads for accounts without Premium, through BetterNMM's Nexus Mods window
    NexusDownloadQueue* nexusDownloads();

    /// Every download, several at once, resumed after failures, pauses and restarts; finished ones install in turn
    DownloadManager* downloads();
    /// Shows the downloads window
    void showDownloads();
    MainWindow* mainWindow() const { return m_mainWindow; }

    Status status() const { return m_status; }

    void updateProxySettings(const QString& proxyTypeStr, const QString& addr, int port, const QString& user, const QString& password);

    QNetworkAccessManager* network();

    HttpMetaCache* metacache();

    QString getUserAgent();

    /// this is the root of the 'installation'
    const QString& root() { return m_rootPath; }

    /// the data path the application is using
    const QString& dataRoot() { return m_dataPath; }

    bool isPortable() const { return m_portable; }

    InstanceWindow* showInstanceWindow(GameInstance* instance, const QString& page = QString());
    MainWindow* showMainWindow(bool minimized = false);
    ViewLogWindow* showLogWindow();

    void ShowGlobalSettings(class QWidget* parent, QString openPage = QString());

    static QUrl normalizeImportUrl(const QString& url);

   signals:
    void globalSettingsAboutToOpen();
    void globalSettingsApplied();
    int currentCatChanged(int index);

#ifdef Q_OS_MACOS
    void clickedOnDock();
#endif

   public slots:
    bool launch(GameInstance* instance, const QString& executable = QString());
    bool kill(BaseInstance* instance);
    static void closeCurrentWindow();

   private slots:
    void on_windowClose();
    void messageReceived(const QByteArray& message);
    void controllerFinished();

   private:
    bool createSetupWizard();
    void performMainStartupAction();

    // sets the fatal error message and m_status to Failed.
    void showFatalErrorMessage(const QString& title, const QString& content);

   private:
    void addRunningInstance();
    void subRunningInstance();
    bool shouldExitNow() const;

   private:
    QHash<QString, int> m_qsaveResources;
    mutable QMutex m_qsaveResourcesMutex;

   private:
    std::unique_ptr<QNetworkAccessManager> m_network;

    std::unique_ptr<AccountList> m_accounts;
    NexusDownloadQueue* m_nexusDownloads = nullptr;
    DownloadManager* m_downloads = nullptr;
    QPointer<DownloadsDialog> m_downloadsDialog;

    std::unique_ptr<HttpMetaCache> m_metacache;

    std::unique_ptr<SettingsObject> m_settings;
    std::unique_ptr<SettingsObject> m_playtimeSettings;
    std::unique_ptr<InstanceList> m_instances;
    std::unique_ptr<IconList> m_icons;
    std::unique_ptr<GenericPageProvider> m_globalSettingsProvider;
    std::unique_ptr<ThemeManager> m_themeManager;

    QString m_rootPath;
    QString m_dataPath;
    Status m_status = Application::StartingUp;
    bool m_portable = false;

    // main state variables
    size_t m_openWindows = 0;
    size_t m_runningInstances = 0;

    // main window, if any
    MainWindow* m_mainWindow = nullptr;

    // log window, if any
    ViewLogWindow* m_viewLogWindow = nullptr;

    // peer launcher instance connector - used to implement single instance launcher and signalling
    LocalPeer* m_peerInstance = nullptr;

   public:
    QString m_instanceIdToLaunch;
    QString m_executableToLaunch;
    bool m_liveCheck = false;
    QList<QUrl> m_urlsToImport;
    QString m_instanceIdToShowWindowOf;
    bool m_showMainWindow = false;
    std::unique_ptr<QFile> logFile;
    std::unique_ptr<LogModel> logModel;

   public:
    void addQSavePath(const QString&);
    void removeQSavePath(const QString&);
    bool checkQSavePath(const QString&);
};
