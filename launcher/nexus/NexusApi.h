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

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QUrl>

#include <functional>

#include "Result.h"
#include "nexus/NexusAccount.h"

/// Request builders and response parsers for the Nexus Mods v1 REST API and v2 GraphQL API
namespace Nexus {

struct GameInfo {
    int id = 0;
    QString name;
    QString domain;
};

struct ModInfo {
    int modId = 0;
    QString game;
    QString name;
    QString summary;
    QString version;
    QString author;
    QString pictureUrl;
    QString thumbnailUrl;
    QString category;
    int endorsements = 0;
    qint64 downloads = 0;
    QDateTime updated;
    bool available = true;
    bool adult = false;
};

/// What the Browse Nexus Mods dialog asks for; empty fields do not filter
struct SearchOptions {
    QString text;
    QString sort = "relevance";  ///< a ModsSort field: relevance, endorsements, downloads, updatedAt, createdAt, name, ...
    QStringList categories;      ///< any of these
    QString author;
    QDateTime updatedSince;
    bool adult = false;  ///< include adult content
};

/// A mod category of a game and how many mods it has
struct Category {
    QString name;
    int count = 0;
};

struct ModFile {
    int fileId = 0;
    QString name;
    QString version;
    QString category;  ///< MAIN, UPDATE, OPTIONAL, OLD_VERSION, MISCELLANEOUS, ARCHIVED
    QString fileName;
    QString description;
    qint64 size = 0;
    QDateTime uploaded;
    bool primary = false;
};

struct DownloadLink {
    QString name;
    QString shortName;
    QUrl url;
};

struct SearchResult {
    QList<ModInfo> mods;
    int total = 0;
};

/// Something a mod lists under "Requirements" on Nexus Mods
struct Requirement {
    int modId = 0;   ///< 0 when it is not a Nexus Mods mod (a tool or a mod hosted elsewhere)
    int gameId = 0;
    QString name;
    QString notes;
    QString url;     ///< where a requirement outside Nexus Mods is
};

/// What one lookup tells about a mod: enough to update it and to follow its requirements
struct ModDetails {
    int modId = 0;
    QString game;  ///< Nexus Mods game domain
    int gameId = 0;
    QString name;
    QString version;
    QList<Requirement> requirements;
};

/// A mod that has to come along because a chosen mod needs it
struct Dependency {
    ModDetails mod;
    QStringList requiredBy;  ///< names of the mods that need it
};

/// A requirement BetterNMM can not download: hosted elsewhere, from another game, or no longer on Nexus Mods
struct OtherRequirement {
    QString name;
    QString url;
    QString notes;
    QStringList requiredBy;
};

struct Requirements {
    QList<Dependency> mods;  ///< what they need first: a mod comes after everything it needs
    QList<OtherRequirement> other;
};

QUrl apiUrl(const QString& path);
QUrl graphqlUrl();
QUrl modPageUrl(const QString& game, int modId);
QUrl modFilesPageUrl(const QString& game, int modId);
/// The files tab with one file's download panel open
QUrl fileDownloadPageUrl(const QString& game, int modId, int fileId);
QUrl downloadLinkUrl(const QString& game, int modId, int fileId, const QString& key = {}, qint64 expires = 0);

/// Turns an error response into a message the user can act on
QString errorMessage(int httpStatus, const QByteArray& body);

Result<NexusAccount> parseValidate(const QByteArray& json);
Result<ModInfo> parseModInfo(const QByteArray& json);
Result<QList<ModFile>> parseModFiles(const QByteArray& json);
Result<QList<DownloadLink>> parseDownloadLinks(const QByteArray& json);

QByteArray searchModsQuery(const QString& game, const SearchOptions& options, int offset, int count);
Result<SearchResult> parseSearchMods(const QByteArray& json);
/// The mod categories of @p game, largest first
QByteArray categoriesQuery(const QString& game);
Result<QList<Category>> parseCategories(const QByteArray& json);
QByteArray modDescriptionQuery(int gameId, int modId);
/// The description, as HTML a QTextBrowser shows
Result<QString> parseModDescription(const QByteArray& json);
/// Turns the BBCode Nexus Mods descriptions are written in into HTML
QString bbcodeToHtml(const QString& bbcode);
QByteArray modFilesQuery(int gameId, int modId);
Result<QList<ModFile>> parseGraphqlModFiles(const QByteArray& json);
/// Details (name, version, game, requirements) of several mods of @p game in one request
QByteArray modDetailsQuery(const QString& game, const QList<int>& modIds);
/// The mods Nexus Mods knows; ids it does not know (deleted, hidden) are left out
Result<QList<ModDetails>> parseModDetails(const QByteArray& json);
/// The files of several mods of one game in one request
QByteArray modFilesBatchQuery(int gameId, const QList<int>& modIds);
Result<QMap<int, QList<ModFile>>> parseModFilesBatch(const QByteArray& json);
/**
 * Follows the requirements of the mods @p selected, and what those need in turn, skipping the mods in @p have (installed
 * or already chosen). @p lookup fetches the details of mods by id; it is called once per level of requirements.
 */
Result<Requirements> resolveRequirements(const QList<int>& selected, const QSet<int>& have,
                                         const std::function<Result<QList<ModDetails>>(const QList<int>&)>& lookup);
QByteArray gameQuery(const QString& domain);
QByteArray searchGamesQuery(const QString& text, int count);
Result<QList<GameInfo>> parseGames(const QByteArray& json);

/// Ranks files for "install the newest main file": primary first, then MAIN, then newest
ModFile bestFile(const QList<ModFile>& files);

/**
 * Picks the script extender build for @p gameVersion (the executable's "1.7.104.0") from the extender mod's files: the best
 * file whose name or description names that version, for the Steam game unless @p gog, never a VR build. Without such a
 * file it is the newest main file and @p matched is set to false.
 */
ModFile scriptExtenderFile(const QList<ModFile>& files, const QString& gameVersion, bool gog, bool* matched = nullptr);

}  // namespace Nexus
