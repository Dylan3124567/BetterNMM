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
#include <QString>
#include <QStringList>

#include <atomic>

#include "Result.h"
#include "tasks/Task.h"

/**
 * File plumbing for game instances.
 *
 * An isolated instance owns a game folder whose unchanged files are hard links to the Steam installation, so they cost no
 * disk space. Files matching the instance's "local" patterns (executables, DLLs, configs) are real copies, so patching
 * them never reaches the Steam installation. Every write BetterNMM makes first removes the link, never writing through it.
 */
namespace GameFiles {

enum class PlaceMode { Link, Copy };

/// Places @p src at @p dst, which must not exist. Hard link when @p wanted is Link and the volume allows it, otherwise a
/// copy (a block clone on file systems that support it). Returns the mode actually used. Setting @p cancel stops a copy
/// in progress and removes the partial file.
Result<PlaceMode> placeFile(const QString& src, const QString& dst, PlaceMode wanted, const std::atomic_bool* cancel = nullptr);

/// The version an executable states about itself ("1.7.104.0"), empty when it states none
QString fileVersion(const QString& path);

/// Deletes a single file without following links. Read-only files are deleted without clearing the attribute, because the
/// attribute is shared with every other hard link to the file.
Result<> removeFile(const QString& path);

/// Deletes a folder tree. Links and junctions are removed themselves, never followed, so nothing outside the tree can be
/// touched; read-only files are handled like removeFile does.
Result<> removeTree(const QString& path);

/// Deletes a folder all or nothing: it is first moved into a ".deleting" folder beside it, which Windows refuses while a
/// program has a file in it open, so a delete never stops halfway because something was in use. Files still open
/// through another hard link (the game running from its own folder) stay there and are deleted by a later call.
Result<> removeTreeWhole(const QString& path);

/// Moves a file within or across volumes, creating parent folders
Result<> moveFile(const QString& src, const QString& dst);

/// True when both paths name the same file on disk (hard links of each other)
bool isSameFile(const QString& a, const QString& b);

/// The file's identity on its volume (hard links share it, a file saved over it gets a new one); 0 when it cannot be read
quint64 fileIndex(const QString& path);

/// True when both paths live on the same volume, so hard links between them work
bool isSameVolume(const QString& a, const QString& b);

/// False when the drive holding @p path has no hard links (FAT32, exFAT, some network drives): files are copied there
bool supportsHardLinks(const QString& path);

/// Splits a ';' separated pattern list
QStringList splitPatterns(const QString& patterns);

/// Wildcard match: patterns with a '/' match the whole relative path, others only the file name. Case-insensitive.
bool matchesAny(const QString& relPath, const QStringList& patterns);

/// Removes @p dir and its parents up to (excluding) @p stopAt while they are empty
void pruneEmptyDirs(const QString& dir, const QString& stopAt);

struct BaseEntry {
    quint64 size = 0;
    qint64 mtime = 0;  ///< ms since epoch, of the copy in the instance when it was placed
    bool linked = false;
};

/// What an isolated instance received from the Steam installation (base.json)
struct BaseManifest {
    QString source;
    QMap<QString, BaseEntry> files;  ///< relative path ('/' separated) -> entry
    QStringList dirs;

    static Result<BaseManifest> load(const QString& path);
    Result<> save(const QString& path) const;
};

/**
 * Makes the instance's files that match @p localPatterns and are still hard links to the Steam installation (an
 * instance made before the patterns were set, or a pattern added since) private copies, so what the game or a patcher
 * rewrites in place never reaches the Steam installation. @p deployedFiles (lower-case) wait in @p overwrittenDir.
 * Returns how many files were copied.
 */
Result<int> unshareLocalFiles(const QString& manifestPath,
                              const QString& gameDir,
                              const QString& overwrittenDir,
                              const QSet<QString>& deployedFiles,
                              const QStringList& localPatterns);

/// The files of a game installation an instance should receive
struct BaseSelection {
    QStringList files;
    QStringList dirs;
    QString note;  ///< explains how the selection was made, shown in the creation log
    bool vanillaKnown = false;
    QSet<QString> missing;  ///< files Steam lists that are not in the folder
};

/**
 * Chooses the base files of @p sourceDir: Steam's own file list for the installed depots when Steam has it cached,
 * otherwise every file. Files matching @p excludePatterns and files listed in @p knownModFiles (lower-case relative
 * paths deployed by another mod manager) are left out either way. @p steamRoot is the Steam folder whose file lists are
 * used, the installed Steam when empty.
 */
BaseSelection selectBaseFiles(const QString& sourceDir,
                              const QStringList& excludePatterns,
                              const QSet<QString>& knownModFiles,
                              const QString& steamRoot = QString());

/// The vortex.deployment*.json manifests Vortex keeps in each folder it deploys a mod type into: the game folder, its data
/// folder, and the folders up to two levels below the game folder (Elden Ring's Game, The Witcher 3's Mods...)
QStringList vortexManifests(const QString& gameDir, const QString& dataPath);
/// Lower-case relative paths of files Vortex deployed into @p gameDir (from its vortex.deployment*.json manifests)
QSet<QString> vortexDeployedFiles(const QString& gameDir, const QString& dataPath);

/**
 * Creates or refreshes an isolated instance's game folder from the source installation.
 * New and changed source files are linked or copied in, files that vanished from the source are removed, and files the
 * user or a mod changed inside the instance are kept.
 */
class BaseSyncTask : public Task {
    Q_OBJECT
   public:
    struct Params {
        QString sourceDir;
        QString gameDir;
        QString manifestPath;
        QString overwrittenDir;        ///< where base files displaced by deployed mods live
        QString alternateSourceDir;    ///< preferred source per file (the Steam instance's displaced originals); may be empty
        QSet<QString> deployedFiles;   ///< lower-case relative paths currently occupied by mod files
        QStringList localPatterns;
        QStringList excludePatterns;
        QSet<QString> knownModFiles;
        QString steamRoot;  ///< the installed Steam when empty
        /// Free space copying must leave on the instance's drive; a sync that would go below it fails before copying
        qint64 reserveBytes = 512LL * 1024 * 1024;
    };

    explicit BaseSyncTask(Params params);

    int linkedCount() const { return m_linked; }
    int copiedCount() const { return m_copied; }
    quint64 copiedBytes() const { return m_copiedBytes; }
    QStringList messages() const { return m_messages; }
    /// The messages the user should act on or know about: no Steam file list, or files missing from the Steam folder
    QStringList warnings() const { return m_warnings; }

    /// Stops at once, also inside a large copy; files placed so far are recorded, so the next sync carries on from there
    bool abort() override;

   protected:
    void executeTask() override;

   private:
    Result<> perform();

    Params m_params;
    int m_linked = 0;
    int m_copied = 0;
    quint64 m_copiedBytes = 0;
    QStringList m_messages;
    QStringList m_warnings;
    std::atomic_bool m_cancel = false;
};

}  // namespace GameFiles
