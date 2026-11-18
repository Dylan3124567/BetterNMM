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

#include <QMap>
#include <QSet>

#include <functional>

#include "Result.h"
#include "game/ModList.h"
#include "tasks/Task.h"

struct DeployedFile {
    QString path;   ///< relative to the game folder, original case
    QString mod;
    qint64 stamp = 0;
    bool displacedOriginal = false;  ///< a file that was there before now waits in the overwritten folder
    /// GameFiles::fileIndex of the placed file, so a file put there later by something else is told apart; 0 in
    /// manifests from before it was recorded
    quint64 fileId = 0;
};

/// deployment.json: which mod owns each deployed file
struct DeploymentManifest {
    QMap<QString, DeployedFile> files;  ///< lower-case relative path -> entry
    QStringList createdDirs;            ///< folders deployment created, removed again when empty

    static Result<DeploymentManifest> load(const QString& path);
    Result<> save(const QString& path) const;
    QSet<QString> keys() const;
};

/// Deployed files another program replaced in the game folder since deployment placed them (xEdit saving a cleaned
/// plugin, a tool's output): what the game reads is not the mod's own file any more
namespace DeployedChanges {
Result<QList<DeployedFile>> find(const QString& manifestPath, const QString& gameDir);
/// Makes the changed file at @p path (relative to the game folder) its mod's own, so it stays through disabling and
/// enabling the mod; the mod's previous file goes
Result<> saveIntoMod(const QString& manifestPath, const QString& gameDir, const QString& modsDir, const QString& path);
/// Hands the changed file to @p discard (the Recycle Bin, for the person) and links the mod's own file in its place
Result<> restore(const QString& manifestPath,
                 const QString& gameDir,
                 const QString& modsDir,
                 const QString& path,
                 const std::function<bool(const QString&)>& discard);
}  // namespace DeployedChanges

/**
 * Makes the game folder match the enabled mods: every file of an enabled mod is hard linked (or copied across volumes)
 * from its staging folder into the game folder, the highest priority mod winning. A file that was already there is moved
 * to the overwritten folder first and moved back when no mod covers it any more, so purging restores the game exactly.
 */
class DeployTask : public Task {
    Q_OBJECT
   public:
    struct Params {
        QString gameDir;
        QString modsDir;
        QString overwrittenDir;
        QString manifestPath;
        QList<ModEntry> enabledMods;  ///< in priority order, last wins
    };

    explicit DeployTask(Params params);

    int added() const { return m_added; }
    int removed() const { return m_removed; }
    /// Files that were changed in the game folder after they were deployed and were left there (see warnings())
    int kept() const { return m_kept; }

   protected:
    void executeTask() override;

   private:
    Result<> perform();

    Params m_params;
    int m_added = 0;
    int m_removed = 0;
    int m_kept = 0;
};
