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

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <atomic>
#include <map>
#include <optional>
#include <string>

#include "InstanceTask.h"
#include "Result.h"
#include "game/GameDefinition.h"

/// Bringing Vortex profiles over: each becomes an isolated instance with the profile's mods, load order, settings and
/// saves. Vortex itself, its staging folder and the user's real saves are only read.
namespace VortexImport {

struct Mod {
    QString id;      ///< Vortex's mod id
    QString folder;  ///< its folder in Vortex's staging folder
    QString type;    ///< Vortex's mod type; empty is the game's default type
    QString name;    ///< as Vortex shows it
    QString version;
    QString nexusGame;  ///< Nexus Mods domain of its page
    int nexusModId = 0;
    int nexusFileId = 0;
    QString archive;  ///< file name of the archive it was installed from
    QString md5;
    QString logicalFileName;
    qint64 installTime = 0;  ///< ms since epoch
    QJsonArray rules;
};

/// A tool Vortex starts for the game (a script extender, ModEngine's launcher, LOOT...)
struct Tool {
    QString name;
    QString path;  ///< absolute
    QString arguments;
    QString workingDir;
};

struct Game {
    QString vortexId;
    GameDefinition definition;
    QString gameDir;     ///< where Vortex found the game
    QString stagingDir;  ///< Vortex's mods folder for it
    QList<Mod> mods;
    QList<Tool> tools;
    QStringList downloads;    ///< finished download archives of the game, absolute paths
    QString deployedProfile;  ///< the profile whose mods are in the game folder now
};

struct Profile {
    QString id;
    QString name;
    QString gameId;
    QSet<QString> enabledMods;   ///< Vortex mod ids
    bool localSaves = false;     ///< the profile has its own saves (Saves/<profile id> for Bethesda games)
    bool localSettings = false;  ///< the profile has its own INI files in its folder
    qint64 lastActivated = 0;
    QString folder;  ///< Vortex's folder of the profile: plugins.txt, loadorder.txt, its INI files
};

struct State {
    QString userData;  ///< %APPDATA%\Vortex
    QList<Game> games;
    QList<Profile> profiles;  ///< of games BetterNMM knows, most recently used first

    const Game* game(const QString& vortexId) const;
    const Profile* profile(const QString& id) const;
};

/// Reads Vortex's state. @p databaseDir defaults to %APPDATA%\Vortex\state.v2; Vortex's user data folder is its parent.
Result<State> load(const QString& databaseDir = {});
/// The state in Vortex's flattened keys (see VortexState::readDatabase)
State fromEntries(const std::map<std::string, std::string>& entries, const QString& userData);

/// Where the files of each mod type go, relative to the game folder ("" = the game folder): the deployment manifests
/// Vortex wrote (in the game folder and its staging folder), otherwise Vortex's default for the game
QHash<QString, QString> typeTargets(const Game& game);

/// @p path relative to @p gameDir ("" for the folder itself), or nothing when it is outside it
std::optional<QString> insideGamePath(const QString& gameDir, const QString& path);

/// What Vortex deployed into the game folder: lower-case path relative to the game folder -> the mod folder it came from
QHash<QString, QString> deployedFiles(const Game& game);

/**
 * The mods in BetterNMM's priority order, a later mod winning: Vortex's before/after rules first, then, when @p winners
 * is given (what Vortex actually deployed), the mod whose file won each conflict goes after the mods it beat. Ties go by
 * name. @p files maps each mod id to its lower-case paths relative to the game folder. Rules that contradict each other
 * are reported in @p warnings.
 */
QStringList order(const QList<Mod>& mods,
                  const QHash<QString, QSet<QString>>& files,
                  const QHash<QString, QString>& winners,
                  const QSet<QString>& enabled,
                  QStringList* warnings);

/// The folders a game keeps saves in, as {real folder, the instance's folder}; folders the instance shares with the
/// Windows profile are left out. A Vortex profile with its own saves only brings those.
struct SaveFolder {
    QString from;
    QString to;
};
QList<SaveFolder> saveFolders(const GameDefinition& game, const Profile& profile, const QString& realGameDir, const QString& instanceRoot);

/// Points a Bethesda game at a Vortex profile's own saves, Saves\<profile id>, the way Vortex does: SLocalSavePath in the
/// [General] section of @p iniPath, which is made when it does not exist; every other line stays as it is
Result<> pointAtProfileSaves(const QString& iniPath, const QString& profileId);

}  // namespace VortexImport

/// Creates an isolated instance from a Vortex profile, in the instance staging folder
class VortexImportTask : public InstanceTask {
    Q_OBJECT
   public:
    struct Options {
        bool saves = true;
        bool downloads = true;
        QString downloadsRoot;  ///< BetterNMM's downloads folder; the archives go in its folder of the game
    };
    /// @p otherProfiles: the ids of the game's other Vortex profiles, whose own saves are not this one's
    VortexImportTask(VortexImport::Game game, VortexImport::Profile profile, QStringList otherProfiles, Options options);

    QStringList messages() const { return m_messages; }

   protected:
    void executeTask() override;
    bool abort() override;

   private:
    void importProfile();
    /// Vortex's tools for the game become the instance's programs
    void addExecutables();

    VortexImport::Game m_game;
    VortexImport::Profile m_profile;
    QStringList m_otherProfiles;
    Options m_options;
    Task::Ptr m_creation;
    std::atomic_bool m_cancel = false;
    QStringList m_messages;
};
