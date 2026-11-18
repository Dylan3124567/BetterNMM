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

#include "NexusApi.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUrlQuery>

#include <algorithm>

#include "BuildConfig.h"

namespace Nexus {

namespace {

Result<QJsonDocument> parseJson(const QByteArray& json)
{
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError) {
        return std::unexpected(QObject::tr("Nexus Mods sent an unreadable response: %1").arg(error.errorString()));
    }
    return doc;
}

Result<QJsonObject> graphqlData(const QByteArray& json)
{
    TRY_INTO(auto doc, parseJson(json));
    const auto root = doc.object();
    if (const auto errors = root.value("errors").toArray(); !errors.isEmpty()) {
        return std::unexpected(QObject::tr("Nexus Mods search failed: %1").arg(errors.first().toObject().value("message").toString()));
    }
    return root.value("data").toObject();
}

QDateTime fromUnix(const QJsonValue& value)
{
    return value.isDouble() ? QDateTime::fromSecsSinceEpoch(value.toInteger(), QTimeZone::UTC) : QDateTime();
}

QByteArray graphql(const QString& query, const QJsonObject& variables = {})
{
    return QJsonDocument(QJsonObject{ { "query", query }, { "variables", variables } }).toJson(QJsonDocument::Compact);
}

}  // namespace

QUrl apiUrl(const QString& path)
{
    return QUrl(BuildConfig.NEXUS_API_URL + path);
}

QUrl graphqlUrl()
{
    return QUrl(BuildConfig.NEXUS_GRAPHQL_URL);
}

QUrl modPageUrl(const QString& game, int modId)
{
    return QUrl(BuildConfig.NEXUS_SITE_URL + QString("%1/mods/%2").arg(game).arg(modId));
}

QUrl modFilesPageUrl(const QString& game, int modId)
{
    auto url = modPageUrl(game, modId);
    url.setQuery("tab=files");
    return url;
}

QUrl fileDownloadPageUrl(const QString& game, int modId, int fileId)
{
    auto url = modPageUrl(game, modId);
    // the download panel; its "Slow download" button downloads the file (with nmm=1 it would hand over an nxm link)
    url.setQuery(QString("tab=files&file_id=%1").arg(fileId));
    return url;
}

QUrl downloadLinkUrl(const QString& game, int modId, int fileId, const QString& key, qint64 expires)
{
    auto url = apiUrl(QString("games/%1/mods/%2/files/%3/download_link.json").arg(game).arg(modId).arg(fileId));
    if (!key.isEmpty()) {
        QUrlQuery query;
        query.addQueryItem("key", key);
        query.addQueryItem("expires", QString::number(expires));
        url.setQuery(query);
    }
    return url;
}

QString errorMessage(int httpStatus, const QByteArray& body)
{
    const auto message = QJsonDocument::fromJson(body).object().value("message").toString();
    switch (httpStatus) {
        case 401:
            return QObject::tr("Nexus Mods rejected the API key. Add your account again with a current key from your Nexus Mods "
                               "account settings.");
        case 403:
            return message.isEmpty() ? QObject::tr("Nexus Mods only allows Premium members to download without visiting the website. "
                                                   "Use the \"Mod Manager Download\" button on the mod's Files page instead.")
                                     : message;
        case 404:
            return QObject::tr("Nexus Mods does not know this mod or file. It may have been removed or hidden by its author.");
        case 410:
            return QObject::tr("This download link has expired. Click the download button on Nexus Mods again.");
        case 429:
            return QObject::tr("Nexus Mods' request limit for your account is used up. Try again in an hour.");
        default:
            break;
    }
    if (!message.isEmpty()) {
        return message;
    }
    return QObject::tr("Nexus Mods answered with HTTP %1.").arg(httpStatus);
}

Result<NexusAccount> parseValidate(const QByteArray& json)
{
    TRY_INTO(auto doc, parseJson(json));
    const auto obj = doc.object();
    NexusAccount account;
    account.apiKey = obj.value("key").toString();
    account.userId = obj.value("user_id").toInt();
    account.name = obj.value("name").toString();
    account.premium = obj.value("is_premium").toBool();
    account.supporter = obj.value("is_supporter").toBool();
    account.avatarUrl = obj.value("profile_url").toString();
    account.validated = QDateTime::currentDateTimeUtc();
    if (account.userId <= 0 || account.name.isEmpty()) {
        return std::unexpected(QObject::tr("Nexus Mods did not return an account for this key."));
    }
    return account;
}

Result<ModInfo> parseModInfo(const QByteArray& json)
{
    TRY_INTO(auto doc, parseJson(json));
    const auto obj = doc.object();
    ModInfo mod;
    mod.modId = obj.value("mod_id").toInt();
    mod.game = obj.value("domain_name").toString();
    mod.name = obj.value("name").toString();
    mod.summary = obj.value("summary").toString();
    mod.version = obj.value("version").toString();
    mod.author = obj.value("author").toString();
    mod.pictureUrl = obj.value("picture_url").toString();
    mod.endorsements = obj.value("endorsement_count").toInt();
    mod.updated = fromUnix(obj.value("updated_timestamp"));
    mod.available = obj.value("available").toBool(true);
    if (mod.modId <= 0) {
        return std::unexpected(QObject::tr("Nexus Mods did not return mod details."));
    }
    return mod;
}

Result<QList<ModFile>> parseModFiles(const QByteArray& json)
{
    TRY_INTO(auto doc, parseJson(json));
    QList<ModFile> files;
    for (const auto& value : doc.object().value("files").toArray()) {
        const auto obj = value.toObject();
        ModFile file;
        file.fileId = obj.value("file_id").toInt();
        file.name = obj.value("name").toString();
        file.version = obj.value("version").toString();
        file.category = obj.value("category_name").toString();
        file.fileName = obj.value("file_name").toString();
        file.description = obj.value("description").toString();
        file.size = obj.value("size_in_bytes").toInteger(obj.value("size_kb").toInteger() * 1024);
        file.uploaded = fromUnix(obj.value("uploaded_timestamp"));
        file.primary = obj.value("is_primary").toBool();
        if (file.fileId > 0) {
            files << file;
        }
    }
    return files;
}

Result<QList<DownloadLink>> parseDownloadLinks(const QByteArray& json)
{
    TRY_INTO(auto doc, parseJson(json));
    QList<DownloadLink> links;
    for (const auto& value : doc.array()) {
        const auto obj = value.toObject();
        DownloadLink link{ .name = obj.value("name").toString(), .shortName = obj.value("short_name").toString(), .url = QUrl(obj.value("URI").toString()) };
        if (link.url.isValid() && !link.url.isEmpty()) {
            links << link;
        }
    }
    if (links.isEmpty()) {
        return std::unexpected(QObject::tr("Nexus Mods did not return a download location."));
    }
    return links;
}

QByteArray searchModsQuery(const QString& game, const SearchOptions& options, int offset, int count)
{
    auto value = [](const QJsonValue& v, const char* op) { return QJsonArray{ QJsonObject{ { "value", v }, { "op", op } } }; };
    QJsonObject filter{ { "gameDomainName", value(game, "EQUALS") } };
    const auto text = options.text.trimmed();
    if (!text.isEmpty()) {
        filter.insert("name", value(text, "WILDCARD"));
    }
    if (!options.author.trimmed().isEmpty()) {
        filter.insert("author", value(options.author.trimmed(), "MATCHES"));
    }
    if (!options.adult) {
        filter.insert("adultContent", value(false, "EQUALS"));
    }
    if (options.updatedSince.isValid()) {
        // the search backend reads dates as Unix seconds; an ISO date with colons breaks its query parser
        filter.insert("updatedAt", value(QString::number(options.updatedSince.toSecsSinceEpoch()), "GTE"));
    }
    if (!options.categories.isEmpty()) {
        // several values in one list must all match, so "any of these categories" is a nested OR filter
        QJsonArray categories;
        for (const auto& category : options.categories) {
            categories << QJsonObject{ { "value", category }, { "op", "EQUALS" } };
        }
        filter.insert("filter", QJsonArray{ QJsonObject{ { "op", "OR" }, { "categoryName", categories } } });
    }
    // relevance needs search text to rank by; without it the most endorsed mods come first, as on the website
    const auto sortField = options.sort == "relevance" && text.isEmpty() ? QString("endorsements") : options.sort;
    QJsonObject sortValue{ { "direction", sortField == "name" ? "ASC" : "DESC" } };
    const QJsonArray sort{ QJsonObject{ { sortField, sortField == "random" ? QJsonObject{} : sortValue } } };
    return graphql(
        "query($filter: ModsFilter, $sort: [ModsSort!], $offset: Int, $count: Int) { mods(filter: $filter, sort: $sort, offset: $offset, "
        "count: $count) { totalCount nodes { modId name summary version author pictureUrl thumbnailUrl endorsements downloads updatedAt "
        "adultContent modCategory { name } game { domainName } } } }",
        QJsonObject{ { "filter", filter }, { "sort", sort }, { "offset", offset }, { "count", count } });
}

QByteArray categoriesQuery(const QString& game)
{
    return graphql("query($filter: ModsFilter) { mods(filter: $filter, count: 0, facets: { categoryName: [] }) { facetsData } }",
                   QJsonObject{ { "filter", QJsonObject{ { "gameDomainName", QJsonArray{ QJsonObject{ { "value", game }, { "op", "EQUALS" } } } } } } });
}

Result<QList<Category>> parseCategories(const QByteArray& json)
{
    TRY_INTO(auto data, graphqlData(json));
    const auto counts = data.value("mods").toObject().value("facetsData").toObject().value("categoryName").toObject();
    QList<Category> categories;
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        // kept exactly as sent: the category filter compares the name character for character
        categories << Category{ .name = it.key(), .count = it.value().toInt() };
    }
    std::sort(categories.begin(), categories.end(), [](const Category& a, const Category& b) { return a.count > b.count; });
    return categories;
}

QByteArray modDescriptionQuery(int gameId, int modId)
{
    return graphql("query($modId: ID!, $gameId: ID!) { mod(modId: $modId, gameId: $gameId) { description } }",
                   QJsonObject{ { "modId", QString::number(modId) }, { "gameId", QString::number(gameId) } });
}

Result<QString> parseModDescription(const QByteArray& json)
{
    TRY_INTO(auto data, graphqlData(json));
    return bbcodeToHtml(data.value("mod").toObject().value("description").toString());
}

QString bbcodeToHtml(const QString& bbcode)
{
    auto html = bbcode;
    if (!html.contains("<br", Qt::CaseInsensitive)) {
        html.replace('\n', "<br>");
    }
    auto replace = [&html](const QString& pattern, const QString& with) {
        html.replace(QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption), with);
    };
    // line breaks next to block tags would add empty lines around lists, headings and centered blocks
    const QString block = R"((?:\*|/\*|/?list(?:=[^\]]*)?|/?center|/?left|/?right|/?quote(?:=[^\]]*)?|/?spoiler(?:=[^\]]*)?|/?heading|line|hr|/?code))";
    replace(R"(<br\s*/?>\s*(?=\[)" + block + R"(\]))", "");
    replace(R"((\[)" + block + R"(\])\s*<br\s*/?>)", "\\1");

    replace(R"(\[(b|i|u|s)\])", "<\\1>");
    replace(R"(\[/(b|i|u|s)\])", "</\\1>");
    replace(R"(\[(center|left|right)\])", R"(<div align="\1">)");
    replace(R"(\[/(center|left|right)\])", "</div>");
    replace(R"(\[color=["']?([^\]"']+)["']?\])", R"(<span style="color:\1">)");
    replace(R"(\[font=["']?([^\]"']+)["']?\])", R"(<span style="font-family:'\1'">)");
    replace(R"(\[/(color|font|size)\])", "</span>");
    // [size=1] to [size=7] are the HTML font size steps; anything larger is taken as pixels
    static const QStringList sizes{ "", "10", "13", "16", "18", "24", "32", "48" };
    QRegularExpression size(R"(\[size=["']?(\d+)(?:px|pt)?["']?\])", QRegularExpression::CaseInsensitiveOption);
    for (auto match = size.match(html); match.hasMatch(); match = size.match(html, match.capturedStart())) {
        const int n = match.captured(1).toInt();
        const auto pixels = n >= 1 && n <= 7 ? sizes[n] : QString::number(n);
        html.replace(match.capturedStart(), match.capturedLength(), QString(R"(<span style="font-size:%1px">)").arg(pixels));
    }
    replace(R"(\[heading\](.*?)\[/heading\])", "<h3>\\1</h3>");
    replace(R"(\[(?:line|hr)\])", "<hr>");
    replace(R"(\[quote(?:=["']?([^\]"']*)["']?)?\])", "<blockquote>");
    replace(R"(\[/quote\])", "</blockquote>");
    replace(R"(\[spoiler(?:=[^\]]*)?\])", "<blockquote>");
    replace(R"(\[/spoiler\])", "</blockquote>");
    replace(R"(\[code\])", "<pre>");
    replace(R"(\[/code\])", "</pre>");
    replace(R"(\[img(?:[^\]]*)\]\s*(\S+?)\s*\[/img\])", R"(<img src="\1">)");
    replace(R"(\[url=["']?([^\]"']+)["']?\](.*?)\[/url\])", R"(<a href="\1">\2</a>)");
    replace(R"(\[url\]\s*(\S+?)\s*\[/url\])", R"(<a href="\1">\1</a>)");
    replace(R"(\[email\]\s*(\S+?)\s*\[/email\])", R"(<a href="mailto:\1">\1</a>)");
    replace(R"(\[youtube\]\s*(\S+?)\s*\[/youtube\])", R"(<a href="https://www.youtube.com/watch?v=\1">YouTube video</a>)");
    replace(R"(\[\*\])", "<li>");
    replace(R"(\[/\*\])", "</li>");

    // lists nest, so each [/list] closes the innermost open one: [list] is bulleted, [list=1] / [list=a] numbered
    QRegularExpression listTag(R"(\[(/?)list(=[^\]]*)?\])", QRegularExpression::CaseInsensitiveOption);
    QStringList open;
    QString out;
    qsizetype last = 0;
    for (auto it = listTag.globalMatch(html); it.hasNext();) {
        const auto match = it.next();
        out += html.mid(last, match.capturedStart() - last);
        last = match.capturedEnd();
        if (match.captured(1).isEmpty()) {
            open << (match.captured(2).isEmpty() ? "ul" : "ol");
            out += '<' + open.last() + '>';
        } else if (!open.isEmpty()) {
            out += "</" + open.takeLast() + '>';
        }
    }
    out += html.mid(last);
    while (!open.isEmpty()) {
        out += "</" + open.takeLast() + '>';
    }
    return out;
}

Result<SearchResult> parseSearchMods(const QByteArray& json)
{
    TRY_INTO(auto data, graphqlData(json));
    const auto mods = data.value("mods").toObject();
    SearchResult result;
    result.total = mods.value("totalCount").toInt();
    for (const auto& value : mods.value("nodes").toArray()) {
        const auto obj = value.toObject();
        ModInfo mod;
        mod.modId = obj.value("modId").toInt();
        mod.game = obj.value("game").toObject().value("domainName").toString();
        mod.name = obj.value("name").toString();
        mod.summary = obj.value("summary").toString();
        mod.version = obj.value("version").toString();
        mod.author = obj.value("author").toString();
        mod.pictureUrl = obj.value("pictureUrl").toString();
        mod.thumbnailUrl = obj.value("thumbnailUrl").toString();
        mod.endorsements = obj.value("endorsements").toInt();
        mod.downloads = obj.value("downloads").toInteger();
        mod.updated = QDateTime::fromString(obj.value("updatedAt").toString(), Qt::ISODate);
        mod.category = obj.value("modCategory").toObject().value("name").toString();
        mod.adult = obj.value("adultContent").toBool();
        if (mod.modId > 0) {
            result.mods << mod;
        }
    }
    return result;
}

QByteArray modFilesQuery(int gameId, int modId)
{
    return graphql(
        "query($modId: ID!, $gameId: ID!) { modFiles(modId: $modId, gameId: $gameId) { fileId name version category size sizeInBytes date "
        "primary uri description } }",
        QJsonObject{ { "modId", QString::number(modId) }, { "gameId", QString::number(gameId) } });
}

namespace {

const char* const k_modFileFields = "fileId name version category size sizeInBytes date primary uri description";

QList<ModFile> readModFiles(const QJsonArray& array)
{
    QList<ModFile> files;
    for (const auto& value : array) {
        const auto obj = value.toObject();
        ModFile file;
        file.fileId = obj.value("fileId").toInt();
        file.name = obj.value("name").toString();
        file.version = obj.value("version").toString();
        file.category = obj.value("category").toString();
        // older files' uri is their archive's name; newer ones' is a storage path ("7d/ee/b7/<uuid>"), which is not a
        // name: then the download's own name is used
        if (const auto uri = obj.value("uri").toString(); !uri.contains('/')) {
            file.fileName = uri;
        }
        file.description = obj.value("description").toString();
        // older files only have their size in KiB
        file.size = obj.value("sizeInBytes").isString() ? obj.value("sizeInBytes").toString().toLongLong() : obj.value("size").toInteger() * 1024;
        file.uploaded = fromUnix(obj.value("date"));
        file.primary = obj.value("primary").toInt() != 0;
        if (file.fileId > 0) {
            files << file;
        }
    }
    return files;
}

/// GraphQL sends ids as strings in some places and as numbers in others
int toId(const QJsonValue& value)
{
    return value.isString() ? value.toString().toInt() : value.toInt();
}

}  // namespace

Result<QList<ModFile>> parseGraphqlModFiles(const QByteArray& json)
{
    TRY_INTO(auto data, graphqlData(json));
    return readModFiles(data.value("modFiles").toArray());
}

QByteArray modDetailsQuery(const QString& game, const QList<int>& modIds)
{
    QJsonArray ids;
    for (const int id : modIds) {
        ids.append(QJsonObject{ { "gameDomain", game }, { "modId", id } });
    }
    return graphql("query($ids: [CompositeDomainWithIdInput!]!) { legacyModsByDomain(ids: $ids) { nodes { modId name version "
                   "game { id domainName } modRequirements { nexusRequirements { nodes { modId gameId modName externalRequirement "
                   "notes url } } } } } }",
                   QJsonObject{ { "ids", ids } });
}

Result<QList<ModDetails>> parseModDetails(const QByteArray& json)
{
    TRY_INTO(auto data, graphqlData(json));
    QList<ModDetails> mods;
    for (const auto& value : data.value("legacyModsByDomain").toObject().value("nodes").toArray()) {
        const auto obj = value.toObject();
        const auto game = obj.value("game").toObject();
        ModDetails mod{ .modId = toId(obj.value("modId")),
                        .game = game.value("domainName").toString(),
                        .gameId = toId(game.value("id")),
                        .name = obj.value("name").toString(),
                        .version = obj.value("version").toString() };
        const auto nodes = obj.value("modRequirements").toObject().value("nexusRequirements").toObject().value("nodes").toArray();
        for (const auto& node : nodes) {
            const auto req = node.toObject();
            Requirement requirement{ .modId = req.value("externalRequirement").toBool() ? 0 : toId(req.value("modId")),
                                     .gameId = toId(req.value("gameId")),
                                     .name = req.value("modName").toString(),
                                     .notes = req.value("notes").toString(),
                                     .url = req.value("url").toString() };
            if (requirement.modId > 0 || !requirement.name.isEmpty()) {
                mod.requirements << requirement;
            }
        }
        if (mod.modId > 0) {
            mods << mod;
        }
    }
    return mods;
}

QByteArray modFilesBatchQuery(int gameId, const QList<int>& modIds)
{
    // one aliased modFiles field per mod: m<id>
    QStringList fields;
    for (const int id : modIds) {
        fields << QString("m%1: modFiles(modId: %1, gameId: %2) { %3 }").arg(id).arg(gameId).arg(k_modFileFields);
    }
    return graphql("query { " + fields.join(' ') + " }");
}

Result<QMap<int, QList<ModFile>>> parseModFilesBatch(const QByteArray& json)
{
    TRY_INTO(auto data, graphqlData(json));
    QMap<int, QList<ModFile>> files;
    for (auto it = data.begin(); it != data.end(); ++it) {
        if (it.key().startsWith('m')) {
            files.insert(it.key().mid(1).toInt(), readModFiles(it.value().toArray()));
        }
    }
    return files;
}

Result<Requirements> resolveRequirements(const QList<int>& selected, const QSet<int>& have,
                                         const std::function<Result<QList<ModDetails>>(const QList<int>&)>& lookup)
{
    Requirements result;
    QMap<int, Dependency> found;
    QMap<int, QList<int>> needs;  // mod id -> the Nexus Mods mods it requires
    auto seen = have;
    for (const int id : selected) {
        seen.insert(id);
    }
    auto addOther = [&result](const OtherRequirement& other, const QString& by) {
        for (auto& known : result.other) {
            if (known.name == other.name && known.url == other.url) {
                if (!known.requiredBy.contains(by)) {
                    known.requiredBy << by;
                }
                return;
            }
        }
        auto added = other;
        added.requiredBy << by;
        result.other << added;
    };

    TRY_INTO(auto level, lookup(selected));
    // requirement chains end; the limit only guards against a broken answer that never stops
    for (int depth = 0; depth < 20 && !level.isEmpty(); depth++) {
        QList<int> next;
        for (const auto& mod : level) {
            for (const auto& requirement : mod.requirements) {
                if (requirement.modId > 0) {
                    needs[mod.modId] << requirement.modId;
                }
                if (requirement.modId <= 0) {
                    addOther({ .name = requirement.name, .url = requirement.url, .notes = requirement.notes }, mod.name);
                } else if (requirement.gameId > 0 && mod.gameId > 0 && requirement.gameId != mod.gameId) {
                    addOther({ .name = requirement.name,
                               .notes = QObject::tr("a mod for another game on Nexus Mods") + (requirement.notes.isEmpty() ? "" : "; " + requirement.notes) },
                             mod.name);
                } else if (seen.contains(requirement.modId)) {
                    if (auto it = found.find(requirement.modId); it != found.end() && !it->requiredBy.contains(mod.name)) {
                        it->requiredBy << mod.name;
                    }
                } else {
                    seen.insert(requirement.modId);
                    next << requirement.modId;
                    found.insert(requirement.modId,
                                 Dependency{ .mod = ModDetails{ .modId = requirement.modId, .game = mod.game, .gameId = mod.gameId, .name = requirement.name },
                                             .requiredBy = { mod.name } });
                }
            }
        }
        if (next.isEmpty()) {
            break;
        }
        TRY_INTO(level, lookup(next));
        for (const auto& details : level) {
            if (auto it = found.find(details.modId); it != found.end()) {
                it->mod = details;
            }
        }
        for (const int id : next) {
            if (std::none_of(level.begin(), level.end(), [id](const ModDetails& details) { return details.modId == id; })) {
                // listed as a requirement but gone from Nexus Mods (deleted or hidden): nothing to download
                const auto gone = found.take(id);
                for (const auto& by : gone.requiredBy) {
                    addOther({ .name = gone.mod.name,
                               .url = modPageUrl(gone.mod.game, id).toString(),
                               .notes = QObject::tr("not available on Nexus Mods any more") },
                             by);
                }
            }
        }
    }
    // every mod after what it needs (depth first; a cycle is cut where it closes)
    QSet<int> visited;
    std::function<void(int)> visit = [&](int id) {
        if (visited.contains(id)) {
            return;
        }
        visited.insert(id);
        for (const int required : needs.value(id)) {
            visit(required);
        }
        if (found.contains(id)) {
            result.mods << found.value(id);
        }
    };
    for (const int id : selected) {
        visit(id);
    }
    return result;
}

QByteArray gameQuery(const QString& domain)
{
    return graphql("query($domain: String) { game(domainName: $domain) { id name domainName } }", QJsonObject{ { "domain", domain } });
}

QByteArray searchGamesQuery(const QString& text, int count)
{
    QJsonObject filter;
    if (!text.trimmed().isEmpty()) {
        filter.insert("name", QJsonArray{ QJsonObject{ { "value", text.trimmed() }, { "op", "WILDCARD" } } });
    }
    return graphql("query($filter: GamesSearchFilter, $count: Int) { games(filter: $filter, count: $count) { nodes { id name domainName } } }",
                   QJsonObject{ { "filter", filter }, { "count", count } });
}

Result<QList<GameInfo>> parseGames(const QByteArray& json)
{
    TRY_INTO(auto data, graphqlData(json));
    QList<GameInfo> games;
    auto read = [&games](const QJsonObject& obj) {
        GameInfo game{ .id = obj.value("id").toVariant().toInt(), .name = obj.value("name").toString(), .domain = obj.value("domainName").toString() };
        if (game.id > 0 && !game.domain.isEmpty()) {
            games << game;
        }
    };
    if (data.contains("game")) {
        read(data.value("game").toObject());
    }
    for (const auto& value : data.value("games").toObject().value("nodes").toArray()) {
        read(value.toObject());
    }
    return games;
}

ModFile bestFile(const QList<ModFile>& files)
{
    auto rank = [](const ModFile& f) {
        if (f.primary) {
            return 0;
        }
        if (f.category.compare("MAIN", Qt::CaseInsensitive) == 0) {
            return 1;
        }
        if (f.category.compare("UPDATE", Qt::CaseInsensitive) == 0 || f.category.compare("OPTIONAL", Qt::CaseInsensitive) == 0) {
            return 2;
        }
        return 3;
    };
    auto sorted = files;
    std::stable_sort(sorted.begin(), sorted.end(), [&rank](const ModFile& a, const ModFile& b) {
        if (rank(a) != rank(b)) {
            return rank(a) < rank(b);
        }
        return a.uploaded > b.uploaded;
    });
    return sorted.isEmpty() ? ModFile{} : sorted.first();
}

ModFile scriptExtenderFile(const QList<ModFile>& files, const QString& gameVersion, bool gog, bool* matched)
{
    // descriptions give the version as major.minor.build ("Compatible with Skyrim Special Edition 1.7.104 from Steam")
    const auto version = gameVersion.split('.').mid(0, 3).join('.');
    // "1.7.104" or "1.7.104.0", but not "1.7.1040" or "1.7.104.5"
    const QRegularExpression names(QString(R"((?<![\d.])%1(?!\d|\.[1-9]))").arg(QRegularExpression::escape(version)));
    const QRegularExpression vr(R"(\bVR\b)");
    QList<ModFile> candidates;
    QList<ModFile> exact;
    for (const auto& file : files) {
        const auto text = file.name + ' ' + file.description;
        if (file.category.compare("REMOVED", Qt::CaseInsensitive) == 0 || vr.match(text).hasMatch() ||
            text.contains("GOG", Qt::CaseInsensitive) != gog) {
            continue;
        }
        candidates << file;
        if (!version.isEmpty() && names.match(text).hasMatch()) {
            exact << file;
        }
    }
    if (matched) {
        *matched = !exact.isEmpty();
    }
    return bestFile(exact.isEmpty() ? candidates : exact);
}

}  // namespace Nexus
