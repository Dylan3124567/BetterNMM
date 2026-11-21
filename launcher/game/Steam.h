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

#include <QList>
#include <QPair>
#include <QString>
#include <QVariantMap>
#include <optional>

#include "Result.h"

/// Read-only access to the local Steam client: libraries, installed apps and the file lists of installed depots.
namespace Steam {

struct AppInstall {
    int appId = 0;
    QString name;
    QString installDir;   ///< absolute path of the game folder
    QString libraryPath;  ///< the Steam library holding it
    QList<QPair<QString, QString>> depots;  ///< installed depot id -> manifest id
};

struct DepotFile {
    QString path;  ///< relative to the game folder, '/' separated
    quint64 size = 0;
    bool isDirectory = false;
};

/// Parses Valve's text KeyValues format into nested QVariantMaps (keys keep their case)
QVariantMap parseVdf(const QByteArray& text);

/// The Steam client folder from the registry, empty when Steam is not installed
QString steamRoot();

/// What the Steam client says about itself in the registry
struct ClientState {
    bool running = false;   ///< its process is alive
    bool loggedIn = false;  ///< someone is logged in, so games can start
    QString exe;            ///< steam.exe, to start it
};
/// The key clientState reads by default ("Software\Valve\Steam"); setClientKey points it elsewhere, for tests
QString clientKey();
void setClientKey(const QString& key);
/// The client's state from HKEY_CURRENT_USER\@p key
ClientState clientState(const QString& key = clientKey());

/// Every library folder from libraryfolders.vdf, the client folder first
QStringList libraryFolders(const QString& root);

/// Every installed app found through appmanifest_*.acf files
QList<AppInstall> installedApps(const QString& root);

std::optional<AppInstall> findApp(const QString& root, int appId);

/// The app whose installation folder is @p dir, if Steam installed it
std::optional<AppInstall> findAppByInstallDir(const QString& root, const QString& dir);

/// Parses a depot manifest from Steam's depotcache folder
Result<QList<DepotFile>> parseDepotManifest(const QByteArray& data);

/**
 * The files Steam installed for @p app, from the cached manifests of its installed depots.
 * Fails when a manifest is not cached or unreadable, so callers never mistake a partial list for the vanilla game.
 */
Result<QList<DepotFile>> vanillaFiles(const QString& root, const AppInstall& app);

/// Minutes Steam counted for @p appId in one account's localconfig.vdf; empty when it has none
std::optional<qint64> playtimeMinutes(const QByteArray& localConfig, int appId);

/// Minutes Steam counted for @p appId for the account signed in (or, with Steam closed, signed in last); empty when unknown
std::optional<qint64> playtimeMinutes(const QString& root, int appId);

}  // namespace Steam
