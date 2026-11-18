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

#include <QProcessEnvironment>

#include "BaseInstance.h"
#include "Result.h"
#include "game/GameDefinition.h"

class ModList;
class PluginList;
class LaunchTask;

namespace GameFiles {
class BaseSyncTask;
}
class DeployTask;

/// Something an instance can run: the game, its script extender, or a tool the user added
struct GameExecutable {
    QString name;
    QString path;  ///< relative to the game folder or absolute
    QString arguments;
    QString workingDir;  ///< empty = the executable's folder
    bool builtIn = false;
};

/**
 * One game installation.
 *
 * The Steam instance ("Main") is the game as Steam installed it, played with the user's real Documents and AppData.
 * Every other instance ("Isolated") has its own game folder built from hard links to the Steam installation, and its own
 * profile folder that the game sees as the user profile, so INIs, saves and plugins.txt never mix with other instances.
 */
class GameInstance : public BaseInstance {
    Q_OBJECT
   public:
    enum class Kind { Isolated, Main };

    GameInstance(SettingsObject* globalSettings, std::unique_ptr<SettingsObject> settings, const QString& rootDir);
    ~GameInstance() override;

    /// Writes the game definition into a fresh instance's settings
    static void applyDefinition(SettingsObject* settings, const GameDefinition& game);
    /// File patterns copied instead of linked by default for @p game
    static QString defaultLocalPatterns(const GameDefinition& game);
    /// The files kept as private copies instead of links to the Steam installation: the setting, or the game's defaults
    /// when it is empty (instances made before it existed, or a cleared field)
    QStringList localPatterns() const;
    static QString defaultExcludePatterns();

    void saveNow() override;
    void loadSpecificSettings() override;

    bool canEdit() const override { return true; }
    bool canExport() const override { return true; }
    bool canLaunch() const override;
    void populateLaunchMenu(QMenu* menu) override;

    Kind kind() const;
    bool isMain() const { return kind() == Kind::Main; }
    GameDefinition definition() const;
    QString sourceGameDir() const;
    int steamAppId() const;

    ////// Folders //////
    QString gameRoot() const override;
    QString modsRoot() const override;
    QString instanceConfigFolder() const override;
    QString profileRoot() const;      ///< USERPROFILE seen by the game; empty for the Steam instance
    QString documentsDir() const;
    QString myGamesDir() const;
    QString savesDir() const;
    QString localAppDataDir() const;
    QString pluginListDir() const;
    QString dataDir() const;
    QString overwrittenDir() const;
    QString downloadsDir() const;
    QString deploymentManifestPath() const;
    QString baseManifestPath() const;

    ////// Lists //////
    ModList* modList();
    PluginList* pluginList();

    ////// Tasks //////
    std::unique_ptr<DeployTask> createDeployTask();
    /// Removes every deployed mod file and puts the files they replaced back
    std::unique_ptr<DeployTask> createPurgeTask();
    std::unique_ptr<GameFiles::BaseSyncTask> createBaseSyncTask();
    /// Creates the profile folders the game expects; returns an error when the profile can not be redirected
    Result<> prepareProfile() const;

    ////// Launching //////
    QList<GameExecutable> executables() const;
    void setCustomExecutables(const QList<GameExecutable>& list);
    QList<GameExecutable> customExecutables() const;
    GameExecutable executable(const QString& name) const;
    QString defaultExecutableName() const;
    QString absoluteExecutablePath(const GameExecutable& exe) const;

    LaunchTask* createLaunchTask(const QString& executableName) override;
    QStringList verboseDescription(const QString& executableName) override;
    QMap<QString, QString> getVariables() override;
    QProcessEnvironment createEnvironment() override;
    QProcessEnvironment createLaunchEnvironment() override;
    QStringList getLogFileSearchPaths() override;
    QString getStatusbarDescription() override;

    /// The Steam instance that manages the game installed in @p gameDir (same folder or Steam app), if there is one;
    /// each installation has at most one, since two would deploy over each other's files
    static GameInstance* mainInstanceFor(const QString& gameDir, int steamAppId);

    /// Whether Windows resolves Documents and AppData through %USERPROFILE%, which is what profile isolation relies on
    static Result<> profileRedirectionSupported();
    /// Where a user folder sits inside a profile, given its User Shell Folders entry: the part after %USERPROFILE%
    /// ("OneDrive/Documents" when OneDrive backs up Documents), otherwise @p defaultPath
    static QString profileSubfolder(const QString& shellFolderEntry, const QString& defaultPath);
    /// Documents and AppData\Local inside an isolated profile, relative to it, where the game will look for them
    static QString documentsSubfolder();
    static QString localAppDataSubfolder();
    static QString roamingAppDataSubfolder();
    static QString savedGamesSubfolder();
    /// Profile folders Windows will not give an isolated instance its own copy of (games saving there share saves).
    /// @p shellFoldersKey is the User Shell Folders key below HKEY_CURRENT_USER; empty means the real one
    static QStringList sharedProfileFolders(const QString& shellFoldersKey = {});
    /**
     * Gives isolated instances their own Saved Games and AppData\LocalLow, or stops doing so. Windows only resolves
     * these through %USERPROFILE% once they have a User Shell Folders entry, so this records their default places as
     * "%USERPROFILE%\Saved Games" (the form Windows uses for Documents) or removes exactly that entry again. A folder is only
     * recorded when it is at its default place, so other programs keep seeing the same folders either way.
     */
    static Result<> setProfileFoldersIsolated(bool isolated, const QString& shellFoldersKey = {});

   private:
    std::unique_ptr<ModList> m_modList;
    std::unique_ptr<PluginList> m_pluginList;
};
