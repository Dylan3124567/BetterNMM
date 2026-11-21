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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <FileSystem.h>
#include <nexus/NexusAccount.h>
#include <nexus/NexusApi.h>
#include <nexus/NxmLink.h>

// moc can not read raw string literals, so the fixtures live where it never looks
#ifndef Q_MOC_RUN
#include "NexusFixtures.h"
#endif

class NexusTest : public QObject {
    Q_OBJECT

   private slots:
    void test_nxm_links()
    {
        auto link = NxmLink::parse(QUrl("nxm://SkyrimSpecialEdition/mods/12604/files/734709?key=abcDEF123&expires=4102444800&user_id=99"));
        QVERIFY2(link.has_value(), qPrintable(link ? QString() : link.error()));
        QCOMPARE(link->game, QString("skyrimspecialedition"));
        QCOMPARE(link->modId, 12604);
        QCOMPARE(link->fileId, 734709);
        QCOMPARE(link->key, QString("abcDEF123"));
        QCOMPARE(link->expires, qint64(4102444800));
        QCOMPARE(link->userId, 99);
        QVERIFY(!link->expired());

        auto premium = NxmLink::parse(QUrl("nxm://fallout4/mods/1/files/2"));
        QVERIFY(premium.has_value());
        QVERIFY(premium->key.isEmpty());
        QVERIFY(!premium->expired());
        QVERIFY(NxmLink::parse(QUrl("nxm://newvegas/mods/1/files/2?key=k&expires=1000"))->expired());

        QVERIFY(!NxmLink::parse(QUrl("https://www.nexusmods.com/skyrimspecialedition/mods/1")).has_value());
        QVERIFY(!NxmLink::parse(QUrl("nxm://skyrim/mods/abc/files/2")).has_value());
        QVERIFY(!NxmLink::parse(QUrl("nxm://skyrim/mods/1")).has_value());
        auto collection = NxmLink::parse(QUrl("nxm://skyrimspecialedition/collections/abcd/revisions/3"));
        QVERIFY(!collection.has_value());
        QVERIFY(collection.error().contains("Collections"));
    }

    void test_rest_responses()
    {
        auto account = Nexus::parseValidate(g_fixture0);
        QVERIFY(account.has_value());
        QCOMPARE(account->userId, 99);
        QCOMPARE(account->name, QString("Dovahkiin"));
        QVERIFY(account->premium);
        QCOMPARE(account->membership(), QString("Premium"));
        QVERIFY(!Nexus::parseValidate(g_fixture1).has_value());
        QVERIFY(!Nexus::parseValidate("<html>").has_value());

        auto mod = Nexus::parseModInfo(g_fixture2);
        QVERIFY(mod.has_value());
        QCOMPARE(mod->version, QString("6.9"));
        QCOMPARE(mod->updated.toSecsSinceEpoch(), qint64(1746484881));

        auto files = Nexus::parseModFiles(g_fixture3);
        QVERIFY(files.has_value());
        QCOMPARE(files->size(), 3);
        QCOMPARE(files->at(1).size, qint64(2048));
        QCOMPARE(Nexus::bestFile(*files).fileId, 2);
        auto primary = *files;
        primary[2].primary = true;
        QCOMPARE(Nexus::bestFile(primary).fileId, 3);
        const QList<Nexus::ModFile> none;
        QCOMPARE(Nexus::bestFile(none).fileId, 0);

        auto links = Nexus::parseDownloadLinks(g_fixture4);
        QVERIFY(links.has_value());
        QCOMPARE(links->first().url.host(), QString("cf-files.nexusmods.com"));
        QVERIFY(!Nexus::parseDownloadLinks("[]").has_value());

        QVERIFY(Nexus::errorMessage(401, {}).contains("API key"));
        QVERIFY(Nexus::errorMessage(403, {}).contains("Premium"));
        const QByteArray teapot = g_fixture5;
        QCOMPARE(Nexus::errorMessage(418, teapot), QString("Short and stout"));
        QCOMPARE(Nexus::downloadLinkUrl("skyrim", 1, 2, "k", 5).toString(),
                 QString("https://api.nexusmods.com/v1/games/skyrim/mods/1/files/2/download_link.json?key=k&expires=5"));
        QCOMPARE(Nexus::modFilesPageUrl("skyrim", 7).toString(), QString("https://www.nexusmods.com/skyrim/mods/7?tab=files"));
    }

    void test_graphql()
    {
        Nexus::SearchOptions options;
        options.text = " sky ui ";
        const auto query = QJsonDocument::fromJson(Nexus::searchModsQuery("skyrimspecialedition", options, 20, 10)).object();
        const auto variables = query.value("variables").toObject();
        QCOMPARE(variables.value("offset").toInt(), 20);
        const auto filter = variables.value("filter").toObject();
        QCOMPARE(filter.value("name").toArray().first().toObject().value("value").toString(), QString("sky ui"));
        QCOMPARE(filter.value("adultContent").toArray().first().toObject().value("value").toBool(true), false);
        QVERIFY(!filter.contains("filter"));
        QVERIFY(variables.value("sort").toArray().first().toObject().contains("relevance"));
        QVERIFY(query.value("query").toString().contains("mods(filter: $filter"));

        // filters as the dialog sends them: categories are "any of" (a nested OR), dates are Unix seconds
        options = {};
        options.sort = "relevance";
        options.categories = { "Armour", "User Interface" };
        options.author = "SkyUI Team";
        options.updatedSince = QDateTime(QDate(2026, 10, 1), QTime(0, 0), QTimeZone::UTC);
        options.adult = true;
        const auto filtered = QJsonDocument::fromJson(Nexus::searchModsQuery("skyrimspecialedition", options, 0, 25)).object().value("variables").toObject();
        const auto f = filtered.value("filter").toObject();
        QVERIFY(!f.contains("adultContent"));
        QCOMPARE(f.value("updatedAt").toArray().first().toObject().value("value").toString(), QString("1790812800"));
        QCOMPARE(f.value("author").toArray().first().toObject().value("op").toString(), QString("MATCHES"));
        const auto anyOf = f.value("filter").toArray().first().toObject();
        QCOMPARE(anyOf.value("op").toString(), QString("OR"));
        QCOMPARE(anyOf.value("categoryName").toArray().size(), 2);
        // relevance without search text falls back to the most endorsed
        QVERIFY(filtered.value("sort").toArray().first().toObject().contains("endorsements"));
        options.sort = "name";
        const auto byName = QJsonDocument::fromJson(Nexus::searchModsQuery("x", options, 0, 25)).object().value("variables").toObject();
        QCOMPARE(byName.value("sort").toArray().first().toObject().value("name").toObject().value("direction").toString(), QString("ASC"));

        const QByteArray facets = "{\"data\":{\"mods\":{\"facetsData\":{\"categoryName\":{\"User Interface\":6385,\"Armour\":11560,"
                                  "\"Locations -  New\":1180}}}}}";
        const auto categories = Nexus::parseCategories(facets);
        QVERIFY(categories.has_value());
        QCOMPARE(categories->size(), 3);
        QCOMPARE(categories->first().name, QString("Armour"));
        QCOMPARE(categories->first().count, 11560);
        QCOMPARE(categories->last().name, QString("Locations -  New"));
    }

    void test_mod_details()
    {
        const auto query = QJsonDocument::fromJson(Nexus::modDetailsQuery("skyrimspecialedition", { 12604, 32444 })).object();
        const auto ids = query.value("variables").toObject().value("ids").toArray();
        QCOMPARE(ids.size(), 2);
        QCOMPARE(ids.at(0).toObject().value("gameDomain").toString(), QString("skyrimspecialedition"));
        QCOMPARE(ids.at(1).toObject().value("modId").toInt(), 32444);
        QVERIFY(query.value("query").toString().contains("legacyModsByDomain(ids: $ids)"));

        // as Nexus Mods answered for SkyUI (ids as strings in requirements), plus an off-site requirement
        const QByteArray json =
            "{\"data\":{\"legacyModsByDomain\":{\"nodes\":[{\"modId\":12604,\"name\":\"SkyUI\",\"version\":\"5.2SE\","
            "\"game\":{\"domainName\":\"skyrimspecialedition\",\"id\":1704},\"modRequirements\":{\"nexusRequirements\":{\"nodes\":["
            "{\"modId\":\"30379\",\"gameId\":\"1704\",\"modName\":\"Skyrim Script Extender (SKSE64)\",\"externalRequirement\":false,"
            "\"notes\":\"\",\"url\":\"\"},{\"modId\":\"\",\"gameId\":\"1704\",\"modName\":\"Some Tool\",\"externalRequirement\":true,"
            "\"notes\":\"for patching\",\"url\":\"https://example.org/tool\"}]}}}]}}}";
        const auto mods = Nexus::parseModDetails(json);
        QVERIFY(mods.has_value());
        QCOMPARE(mods->size(), 1);
        const auto& skyui = mods->first();
        QCOMPARE(skyui.modId, 12604);
        QCOMPARE(skyui.gameId, 1704);
        QCOMPARE(skyui.game, QString("skyrimspecialedition"));
        QCOMPARE(skyui.version, QString("5.2SE"));
        QCOMPARE(skyui.requirements.size(), 2);
        QCOMPARE(skyui.requirements[0].modId, 30379);
        QCOMPARE(skyui.requirements[0].gameId, 1704);
        QCOMPARE(skyui.requirements[1].modId, 0);  // not a Nexus Mods mod
        QCOMPARE(skyui.requirements[1].url, QString("https://example.org/tool"));

        QVERIFY(!Nexus::parseModDetails("{\"errors\":[{\"message\":\"bad\"}]}").has_value());
    }

    void test_files_batch()
    {
        const auto query = QJsonDocument::fromJson(Nexus::modFilesBatchQuery(1704, { 30379, 32444 })).object().value("query").toString();
        QVERIFY(query.contains("m30379: modFiles(modId: 30379, gameId: 1704)"));
        QVERIFY(query.contains("m32444: modFiles(modId: 32444, gameId: 1704)"));

        // a newer file's uri is a storage path, not its name; an older file's is its archive's name
        const QByteArray json = "{\"data\":{\"m30379\":[{\"fileId\":795992,\"name\":\"SKSE64 Steam\",\"category\":\"MAIN\","
                                "\"uri\":\"7d/ee/b7/7deeb793-fe96-4937-8fe9-97a8cc9e0b2c\"}],\"m32444\":[{\"fileId\":1,\"name\":\"Old\","
                                "\"category\":\"MAIN\",\"uri\":\"Address Library-32444-1.zip\"}]}}";
        const auto files = Nexus::parseModFilesBatch(json);
        QVERIFY(files.has_value());
        QCOMPARE(files->size(), 2);
        QCOMPARE(files->value(30379).first().fileId, 795992);
        QVERIFY(files->value(30379).first().fileName.isEmpty());
        QCOMPARE(files->value(32444).first().fileName, QString("Address Library-32444-1.zip"));
    }

    void test_requirements()
    {
        // A needs B and C; B needs D and A (a cycle); C needs D and an off-site tool; E needs Installed and Gone (deleted
        // from Nexus Mods) and a mod of another game
        auto mod = [](int id, const QString& name, QList<Nexus::Requirement> requirements) {
            return Nexus::ModDetails{ .modId = id, .game = "skyrimspecialedition", .gameId = 1704, .name = name, .version = "1", .requirements = requirements };
        };
        auto needs = [](int id, const QString& name, int gameId = 1704) { return Nexus::Requirement{ .modId = id, .gameId = gameId, .name = name }; };
        const QMap<int, Nexus::ModDetails> nexus{
            { 1, mod(1, "A", { needs(2, "B"), needs(3, "C") }) },
            { 2, mod(2, "B", { needs(4, "D"), needs(1, "A") }) },
            { 3, mod(3, "C", { needs(4, "D"), Nexus::Requirement{ .name = "Tool", .notes = "for patching", .url = "https://example.org/tool" } }) },
            { 4, mod(4, "D", {}) },
            { 5, mod(5, "E", { needs(6, "Installed"), needs(7, "Gone"), needs(8, "Oldrim mod", 110) }) },
            { 6, mod(6, "Installed", { needs(9, "Never looked at") }) },
        };
        QList<QList<int>> asked;
        auto lookup = [&](const QList<int>& ids) -> Result<QList<Nexus::ModDetails>> {
            asked << ids;
            QList<Nexus::ModDetails> found;
            for (const int id : ids) {
                if (nexus.contains(id)) {
                    found << nexus.value(id);
                }
            }
            return found;
        };

        const auto result = Nexus::resolveRequirements({ 1, 5 }, { 6 }, lookup);
        QVERIFY(result.has_value());
        QStringList names;
        for (const auto& dependency : result->mods) {
            names << dependency.mod.name;
        }
        // every mod after what it needs; nothing twice, the chosen and the installed ones not at all
        QCOMPARE(names, QStringList({ "D", "B", "C" }));
        QCOMPARE(result->mods[0].requiredBy, QStringList({ "B", "C" }));
        QCOMPARE(result->mods[0].mod.version, QString("1"));  // the details came from the lookup
        QCOMPARE(asked, QList<QList<int>>({ { 1, 5 }, { 2, 3, 7 }, { 4 } }));  // one lookup per level, nothing installed looked up

        QStringList other;
        for (const auto& requirement : result->other) {
            other << requirement.name + " <- " + requirement.requiredBy.join('+');
        }
        // in the order they turn up: the other game's mod and the deleted one on E's level, the tool on C's
        QCOMPARE(other, QStringList({ "Oldrim mod <- E", "Gone <- E", "Tool <- C" }));
        QVERIFY(result->other[1].url.contains("skyrimspecialedition/mods/7"));
        QCOMPARE(result->other[2].url, QString("https://example.org/tool"));

        // a failed lookup is reported, not half a result
        const auto failed = Nexus::resolveRequirements({ 1 }, {}, [](const QList<int>&) -> Result<QList<Nexus::ModDetails>> {
            return std::unexpected(QString("offline"));
        });
        QVERIFY(!failed.has_value());
        QCOMPARE(failed.error(), QString("offline"));
        // nothing needed: no dependencies, one lookup
        asked.clear();
        const auto alone = Nexus::resolveRequirements({ 4 }, {}, lookup);
        QVERIFY(alone.has_value() && alone->mods.isEmpty() && alone->other.isEmpty());
        QCOMPARE(asked.size(), 1);
    }

    void test_file_sizes()
    {
        // older files come without sizeInBytes; their size in KiB stands in (Wet and Cold SE's main file)
        const QByteArray json = "{\"data\":{\"modFiles\":[{\"fileId\":1,\"name\":\"Wet and Cold SE v2.4.0\",\"version\":\"2.4.0\","
                                "\"category\":\"MAIN\",\"size\":39781,\"sizeInBytes\":null},{\"fileId\":2,\"name\":\"Patch\",\"version\":\"1\","
                                "\"category\":\"OPTIONAL\",\"size\":46194,\"sizeInBytes\":\"47303051\"}]}}";
        const auto files = Nexus::parseGraphqlModFiles(json);
        QVERIFY(files.has_value());
        QCOMPARE(files->at(0).size, qint64(39781) * 1024);
        QCOMPARE(files->at(1).size, qint64(47303051));
    }

    void test_archive_names_lead_to_their_files()
    {
        // what Nexus Mods lists for two mods (from its GraphQL modFiles, 2026-11-20): older files are named by their uri,
        // newer ones only have a storage path there
        const QByteArray beyondReach =
            "{\"data\":{\"modFiles\":["
            "{\"fileId\":741776,\"name\":\"Grass - Beyond Reach\",\"version\":\"1\",\"category\":\"ARCHIVED\",\"date\":1776169628,"
            "\"uri\":\"Grass - Beyond Reach-177338-1-1776169628.rar\"},"
            "{\"fileId\":741777,\"name\":\"BOS fix - Beyond Reach\",\"version\":\"1\",\"category\":\"MAIN\",\"date\":1776169650,"
            "\"uri\":\"Beyond Reach - Home Clipping BOS Fix-177338-1-1776169650.rar\"}]}}";
        const QByteArray tng =
            "{\"data\":{\"modFiles\":["
            "{\"fileId\":830948,\"name\":\"TNG Main Thread Fix\",\"version\":\"v4.2.7-mitfix2PL\",\"category\":\"ARCHIVED\","
            "\"date\":1793613371,\"uri\":\"6a/0e/d9/6a0ed9cd-07eb-4b0c-9609-1cc7624decb8\"},"
            "{\"fileId\":831494,\"name\":\"TNG Main Thread Fix\",\"version\":\"4\",\"category\":\"MISCELLANEOUS\","
            "\"date\":1793703548,\"uri\":\"03/23/32/0323326d-f36f-4334-bd8f-17226021cac3\"}]}}";
        const auto beyondFiles = Nexus::parseGraphqlModFiles(beyondReach);
        const auto tngFiles = Nexus::parseGraphqlModFiles(tng);
        QVERIFY(beyondFiles.has_value() && tngFiles.has_value());

        // an older file: every number between dashes may be the mod; its own name tells the file
        const QString older = "Beyond Reach - Home Clipping BOS Fix-177338-1-1776169650.rar";
        QCOMPARE(Nexus::modIdsInArchiveName(older), (QList<int>{ 177338, 1 }));
        QCOMPARE(Nexus::fileForArchive(older, *beyondFiles).value_or(Nexus::ModFile{}).fileId, 741777);
        QCOMPARE(Nexus::modIdsInArchiveName("HDT-SMP for SSE 1.5.97 v2.11-30872-2-11-1578745910.7z"), (QList<int>{ 30872, 2, 11 }));
        QCOMPARE(Nexus::modIdsInArchiveName("Seamless Males SE-19164-1-0.7z"), (QList<int>{ 19164, 1 }));

        // a newer file: the number before the version is the mod, the upload time (UTC, to the minute) the file
        const QString newer = "TNG Main Thread Fix 197140 3PL 2026-11-03T10-59Z pQ0AZWKTW.7z";
        QCOMPARE(Nexus::modIdsInArchiveName(newer), QList<int>{ 197140 });
        QCOMPARE(Nexus::fileForArchive(newer, *tngFiles).value_or(Nexus::ModFile{}).fileId, 831494);
        QCOMPARE(Nexus::modIdsInArchiveName("Address Library All in One (1.7.104.0) v13 32444 13 2026-08-27T15-29Z Ae46W7Fw2.zip"),
                 QList<int>{ 32444 });

        // nothing that fits: no file
        QVERIFY(!Nexus::fileForArchive("TNG Main Thread Fix 197140 3PL 2026-11-03T11-00Z pQ0AZWKTW.7z", *tngFiles));
        QVERIFY(!Nexus::fileForArchive(older, *tngFiles));
        QVERIFY(Nexus::modIdsInArchiveName("My own patch.7z").isEmpty());
    }

    void test_script_extender_file()
    {
        // the SKSE64 files on Nexus Mods as they were listed (ids, names and descriptions), newest last
        auto file = [](int id, const char* category, const char* name, const char* version, const char* description, int day) {
            Nexus::ModFile f;
            f.fileId = id;
            f.category = category;
            f.name = name;
            f.version = version;
            f.description = description;
            f.uploaded = QDateTime(QDate(2026, 1, 1).addDays(day), QTime(0, 0), QTimeZone::UTC);
            return f;
        };
        const QList<Nexus::ModFile> files{
            file(154651, "ARCHIVED", "Skyrim Script Extender (SKSE64)", "2.0.19", "", 1),
            file(320440, "ARCHIVED", "Skyrim Script Extender (SKSE64) GOG", "2.2.2", "Compatible with Skyrim Special Edition 1.6.659 from GOG.com", 2),
            file(449035, "ARCHIVED", "Skyrim Script Extender (SKSE64)", "2.2.4", "Compatible with Skyrim Special Edition 1.6.1130 from Steam", 3),
            file(470991, "MAIN", "Skyrim Script Extender (SKSE64) GOG", "2.2.6", "Compatible with Skyrim Special Edition 1.6.1179 from GOG.com", 4),
            file(792225, "ARCHIVED", "Skyrim Script Extender (SKSE64) Steam", "2.2.7", "Compatible with Skyrim Special Edition 1.6.1170 from Steam", 5),
            file(795963, "ARCHIVED", "Skyrim Script Extender (SKSE64) Steam", "2.3.1", "Compatible with Skyrim Special Edition 1.7.104 from Steam", 6),
            file(795992, "MAIN", "Skyrim Script Extender (SKSE64) Steam", "2.3.1", "Compatible with Skyrim Special Edition 1.7.104 from Steam", 7),
            file(800000, "OPTIONAL", "Skyrim Script Extender VR", "2.0.12", "For SkyrimVR 1.7.104 users", 8),
            file(113739, "REMOVED", "Placeholder", "1", "", 0),
        };
        bool matched = false;
        // this PC's SkyrimSE.exe states 1.7.104.0: the main Steam build for it, not the archived copy, VR or GOG
        QCOMPARE(Nexus::scriptExtenderFile(files, "1.7.104.0", false, &matched).fileId, 795992);
        QVERIFY(matched);
        QCOMPARE(Nexus::scriptExtenderFile(files, "1.6.1170.0", false, &matched).fileId, 792225);
        QVERIFY(matched);
        QCOMPARE(Nexus::scriptExtenderFile(files, "1.6.1179.0", true, &matched).fileId, 470991);
        QVERIFY(matched);
        // "1.6.117" must not match "1.6.1170"
        QCOMPARE(Nexus::scriptExtenderFile(files, "1.6.117.0", false, &matched).fileId, 795992);
        QVERIFY(!matched);
        // an unknown version falls back to the newest Steam main file and says so
        QCOMPARE(Nexus::scriptExtenderFile(files, "", false, &matched).fileId, 795992);
        QVERIFY(!matched);
    }

    void test_bbcode()
    {
        // no raw string literals in this file: moc skips the slots that follow one
        QCOMPARE(Nexus::bbcodeToHtml("[b]bold[/b] [i]it[/i] [u]u[/u] [s]s[/s]"), QString("<b>bold</b> <i>it</i> <u>u</u> <s>s</s>"));
        QCOMPARE(Nexus::bbcodeToHtml("[center]x[/center]"), QString("<div align=\"center\">x</div>"));
        QCOMPARE(Nexus::bbcodeToHtml("[color=#444444]c[/color]"), QString("<span style=\"color:#444444\">c</span>"));
        QCOMPARE(Nexus::bbcodeToHtml("[size=5]big[/size]"), QString("<span style=\"font-size:24px\">big</span>"));
        QCOMPARE(Nexus::bbcodeToHtml("[url=https://a.b/c]link[/url] [url]https://d.e[/url]"),
                 QString("<a href=\"https://a.b/c\">link</a> <a href=\"https://d.e\">https://d.e</a>"));
        QCOMPARE(Nexus::bbcodeToHtml("[img]https://x.y/p.png[/img]"), QString("<img src=\"https://x.y/p.png\">"));
        QCOMPARE(Nexus::bbcodeToHtml("[heading]Features[/heading]"), QString("<h3>Features</h3>"));
        // nested lists close in order, and line breaks around list markup do not add empty lines
        QCOMPARE(Nexus::bbcodeToHtml("[list]<br />[*]a[/*]<br />[list=1][*]b[/list]<br />[/list]"),
                 QString("<ul><li>a</li><ol><li>b</ol></ul>"));
        // an unclosed list is closed at the end; unknown tags and plain brackets stay text
        QCOMPARE(Nexus::bbcodeToHtml("[list][*]a"), QString("<ul><li>a</ul>"));
        QCOMPARE(Nexus::bbcodeToHtml("Requires [SKSE] 2.2"), QString("Requires [SKSE] 2.2"));
        // descriptions without HTML line breaks keep their lines
        QCOMPARE(Nexus::bbcodeToHtml("one\ntwo"), QString("one<br>two"));

        auto search = Nexus::parseSearchMods(g_fixture6);
        QVERIFY(search.has_value());
        QCOMPARE(search->total, 220);
        QCOMPARE(search->mods.first().downloads, qint64(26576843));
        QCOMPARE(search->mods.first().game, QString("skyrimspecialedition"));
        QVERIFY(!Nexus::parseSearchMods(g_fixture7).has_value());

        auto files = Nexus::parseGraphqlModFiles(g_fixture8);
        QVERIFY(files.has_value());
        QCOMPARE(files->first().size, qint64(2778523));
        QCOMPARE(files->first().fileName, QString("SkyUI.7z"));

        auto games = Nexus::parseGames(g_fixture9);
        QVERIFY(games.has_value());
        QCOMPARE(games->first().id, 130);
        games = Nexus::parseGames(g_fixture10);
        QVERIFY(games.has_value());
        QCOMPARE(games->size(), 2);
    }

    void test_accounts_file()
    {
        QTemporaryDir dir;
        const auto path = FS::PathCombine(dir.path(), "accounts.json");
        {
            AccountList list;
            list.setListFilePath(path);
            QVERIFY(list.load().has_value());
            QVERIFY(list.apiKey().isEmpty());
            list.upsert(NexusAccount{ .apiKey = "k1", .userId = 1, .name = "one" });
            list.upsert(NexusAccount{ .apiKey = "k2", .userId = 2, .name = "two", .premium = true });
            QCOMPARE(list.apiKey(), QString("k1"));
            list.setDefault(1);
            QVERIFY(list.isPremium());
            list.upsert(NexusAccount{ .apiKey = "k2-new", .userId = 2, .name = "two", .premium = true });
            QCOMPARE(list.accounts().size(), 2);
        }
        AccountList reloaded;
        reloaded.setListFilePath(path);
        QVERIFY(reloaded.load().has_value());
        QCOMPARE(reloaded.apiKey(), QString("k2-new"));
        reloaded.remove(1);
        QCOMPARE(reloaded.apiKey(), QString("k1"));
        // a file that is not ours (an old launcher's) is left alone
        QFile foreign(FS::PathCombine(dir.path(), "other.json"));
        QVERIFY(foreign.open(QIODevice::WriteOnly));
        foreign.write(g_fixture11);
        foreign.close();
        AccountList other;
        other.setListFilePath(foreign.fileName());
        QVERIFY(!other.load().has_value());
        QVERIFY(other.accounts().isEmpty());
    }
};

QTEST_GUILESS_MAIN(NexusTest)

#include "Nexus_test.moc"
