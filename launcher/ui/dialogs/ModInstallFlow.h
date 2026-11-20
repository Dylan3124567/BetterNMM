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

#include <QString>
#include <QUrl>
#include <functional>

#include "game/ModList.h"
#include "nexus/NexusApi.h"

class GameInstance;
class QWidget;
struct DownloadItem;
struct NxmLink;

/// The interactive steps of getting mods into an instance, shared by the main window, the mods page and the Nexus browser
namespace ModInstallFlow {

/// Whether @p archive is an exported instance (instance.cfg at its top) rather than a mod
bool isInstanceArchive(const QString& archive);

/// Asks for mod archives to install into @p instance; empty when cancelled
QStringList pickArchives(GameInstance* instance, QWidget* parent);

/// Picks the instance a mod for @p gameDomain goes into (any game when empty); nullptr when cancelled or none fits
GameInstance* chooseInstance(const QString& gameDomain, QWidget* parent, GameInstance* preferred);

/**
 * Installs a mod archive into the instance: extracts it, runs its FOMOD installer if it has one, stages the files,
 * enables the mod and deploys. @p metadata carries what is known about the mod (name, version, Nexus ids). While the
 * game runs (a download finishing mid-game) the mod is installed and deployed once the game stops.
 *
 * When @p metadata.id is an installed mod, that mod is installed anew in its place without asking, its FOMOD installer
 * starting with the options it has now (@p metadata.choices, or else the options its files show).
 */
bool installArchive(GameInstance* instance, const QString& archive, ModEntry metadata, QWidget* parent);

/**
 * Runs the FOMOD installer of the installed mod @p modId again, with the options it has now picked, and installs it anew
 * with what the person picks: its place in the list, enabled state and Nexus details stay. Its archive comes from the
 * downloads folder; when it is not there the person is asked for it.
 */
bool changeInstallerOptions(GameInstance* instance, const QString& modId, QWidget* parent);

/// Details (name, version, game id, requirements) of mods of @p game from Nexus Mods, with a progress dialog; no API key
/// needed. Mods Nexus Mods no longer has are left out.
Result<QList<Nexus::ModDetails>> fetchModDetails(const QString& game, const QList<int>& modIds, QWidget* parent);

/// The files of mods of one game from Nexus Mods, with a progress dialog; no API key needed
Result<QMap<int, QList<Nexus::ModFile>>> fetchModFiles(int gameId, const QList<int>& modIds, QWidget* parent);

/// The archive path a download of @p fileName goes to: the instance's downloads folder
QString downloadTarget(GameInstance* instance, const QString& fileName);

/// A file on Nexus Mods, with what is known about its mod
struct NexusFile {
    int modId = 0;
    QString modName;
    QString modVersion;
    Nexus::ModFile file;  ///< fileId 0 when the file is not known: the mod's files page opens instead
};

/**
 * Downloads files for an account without Premium. Nexus Mods hands those out only through the "Slow download" button
 * on its website, clicked by the person; BetterNMM's Nexus Mods window shows each file's download page in turn, and
 * everything installs into @p instance once the window closes.
 */
void downloadFromWebsite(GameInstance* instance, const QString& game, const QList<NexusFile>& files, QWidget* parent);

/// Downloads a file from Nexus Mods (from a website download link) in the background, alongside other downloads, and
/// installs it when it is complete; false when the link can not be used (already reported)
bool installNexusFile(GameInstance* instance, const NxmLink& link, QWidget* parent);

/// The same without a website link; Nexus allows this for Premium members only
bool installNexusFile(GameInstance* instance, const QString& game, int modId, int fileId, QWidget* parent);

/// Installs a finished download into the instance with the id @p instanceId, after the installs queued before it: one
/// installer at a time, never inside another dialog
void queueInstall(const QString& instanceId, const QString& archive, const ModEntry& metadata);

/// Runs the queued installs; BetterNMM's Nexus Mods window holds them back while it is open and calls this when it closes
void installQueuedDownloads();

/// Asks Nexus Mods for a new address for a download whose address expired (a DownloadManager::LinkRefresher)
void refreshNexusLink(const DownloadItem& item, std::function<void(const QUrl&, const QString&)> answer);

/**
 * Installs the script extender build made for the instance's game version from Nexus Mods. Premium members get it
 * downloaded and installed; everyone else gets its download page in BetterNMM's Nexus Mods window.
 */
bool installScriptExtender(GameInstance* instance, QWidget* parent);

/// Makes the game folder match the enabled mods, with a progress dialog; false on failure (already reported). While
/// the game runs it is done once the game stops, and false is returned.
bool deploy(GameInstance* instance, QWidget* parent);

/// Removes all deployed mod files and restores the files they replaced
bool purge(GameInstance* instance, QWidget* parent);

/**
 * Sorts the instance: its plugins with LOOT's rules and masterlist (downloaded, or the copy kept from last time), then
 * its mods so that, of two mods with the same files, the one that needs the other (Nexus Mods requirements) wins, then
 * the one whose plugin loads later. Shows what moved, with a way to undo it.
 */
bool autoSort(GameInstance* instance, QWidget* parent);

/**
 * Finds compatibility patches in the FOMOD installers of the enabled mods (read from their archives in the downloads
 * folder) that the installers recommend because of other mods you have, but that are not installed, and adds the ones
 * picked to their mods.
 */
void checkPatches(GameInstance* instance, QWidget* parent);

}  // namespace ModInstallFlow
