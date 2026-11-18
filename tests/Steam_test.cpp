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

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <FileSystem.h>
#include <QTest>

#include <game/GameDefinition.h>
#include <game/Steam.h>

// moc can not read raw string literals, so the fixtures live where it never looks
#ifndef Q_MOC_RUN
#include "SteamFixtures.h"
#endif

class SteamTest : public QObject {
    Q_OBJECT

   private slots:
    void test_vdf()
    {
        const QByteArray text = g_libraryFolders;
        const auto root = Steam::parseVdf(text);
        const auto folders = root.value("libraryfolders").toMap();
        QCOMPARE(folders.size(), 2);
        QCOMPARE(folders.value("0").toMap().value("path").toString(), QString(R"(A:\SteamClient)"));
        QCOMPARE(folders.value("0").toMap().value("apps").toMap().value("489830").toString(), QString("16092533099"));
        QCOMPARE(folders.value("1").toMap().value("path").toString(), QString(R"(D:\Games\Steam "Library")"));
        // garbage never loops forever
        Steam::parseVdf("{{{ } \"unterminated");
    }

    void test_depot_manifest()
    {
        QByteArray payload = fileMapping("SkyrimSE.exe", 37910440, 32) + fileMapping(QByteArray("Data\\Skyrim.esm\0", 16), 249752131, 0) +
                             fileMapping("Data", 0, 64);
        QByteArray metadata = field(1, 0) + varint(489833) + field(4, 0) + varint(0);
        QByteArray manifest = section(0x71F617D0, payload) + section(0x1F4812BE, metadata) + section(0x1B81B817, QByteArray(32, 's'));
        manifest += QByteArray("\xab\x15\xc4\x32", 4);

        auto files = Steam::parseDepotManifest(manifest);
        QVERIFY2(files.has_value(), qPrintable(files ? QString() : files.error()));
        QCOMPARE(files->size(), 3);
        QCOMPARE(files->at(0).path, QString("SkyrimSE.exe"));
        QCOMPARE(files->at(0).size, quint64(37910440));
        QVERIFY(!files->at(0).isDirectory);
        QCOMPARE(files->at(1).path, QString("Data/Skyrim.esm"));
        QVERIFY(files->at(2).isDirectory);

        QByteArray encrypted = section(0x71F617D0, payload) + section(0x1F4812BE, field(4, 0) + varint(1));
        QVERIFY(!Steam::parseDepotManifest(encrypted).has_value());
        QVERIFY(!Steam::parseDepotManifest(manifest.left(20)).has_value());
        QVERIFY(!Steam::parseDepotManifest("not a manifest at all").has_value());
    }

    void test_game_registry()
    {
        Q_INIT_RESOURCE(games);
        QFile file(":/games/games.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QString error;
        const auto games = GameRegistry::parse(file.readAll(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(games.size() >= 10);
        const auto sse = GameRegistry::bySteamAppId(489830);
        QCOMPARE(sse.domain, QString("skyrimspecialedition"));
        QCOMPARE(sse.scriptExtender, QString("skse64_loader.exe"));
        QCOMPARE(sse.pluginFormat, PluginFormat::Asterisk);
        QCOMPARE(GameRegistry::byDomain("newvegas").pluginFormat, PluginFormat::Timestamp);
        QCOMPARE(GameRegistry::byDomain("skyrim").pluginFormat, PluginFormat::Textfile);
        QVERIFY(!GameRegistry::byDomain("not-a-game").isValid());
        // every domain is unique and every entry has an executable
        QSet<QString> domains;
        for (const auto& game : games) {
            QVERIFY(!domains.contains(game.domain));
            domains.insert(game.domain);
            QVERIFY(!game.executable.isEmpty());
        }
        QVERIFY(GameRegistry::parse("{\"games\":[{\"domain\":\"x\"}]}", &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void test_playtime()
    {
        // Steam writes the path with mixed case ("valve"); minutes as a string
        const QByteArray config =
            "\"UserLocalConfigStore\"\n{\n\t\"Software\"\n\t{\n\t\t\"valve\"\n\t\t{\n\t\t\t\"Steam\"\n\t\t\t{\n\t\t\t\t\"apps\"\n\t\t\t\t{\n"
            "\t\t\t\t\t\"489830\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"LastPlayed\"\t\t\"1794846935\"\n\t\t\t\t\t\t\"Playtime\"\t\t\"18468\"\n"
            "\t\t\t\t\t}\n\t\t\t\t\t\"22380\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"LastPlayed\"\t\t\"1\"\n\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n";
        QCOMPARE(Steam::playtimeMinutes(config, 489830).value_or(-1), qint64(18468));
        QVERIFY(!Steam::playtimeMinutes(config, 22380).has_value());  // played, but no playtime recorded
        QVERIFY(!Steam::playtimeMinutes(config, 1).has_value());
        QVERIFY(!Steam::playtimeMinutes(QByteArray("garbage {"), 489830).has_value());
        QVERIFY(!Steam::playtimeMinutes(QString(), 489830).has_value());
    }

    void test_real_steam_install_when_present()
    {
        // read-only look at the real Steam client, when this machine has one
        const auto root = Steam::steamRoot();
        if (root.isEmpty()) {
            QSKIP("Steam is not installed");
        }
        const auto libraries = Steam::libraryFolders(root);
        QVERIFY(!libraries.isEmpty());
        QCOMPARE(libraries.first(), root);
        int parsed = 0;
        for (const auto& app : Steam::installedApps(root)) {
            QVERIFY(app.appId > 0);
            QVERIFY(QFileInfo(app.installDir).isDir());
            const auto files = Steam::vanillaFiles(root, app);
            if (!files) {
                continue;  // Steam did not cache this app's file list
            }
            parsed++;
            int present = 0;
            int regular = 0;
            for (const auto& file : *files) {
                QVERIFY(!file.path.isEmpty());
                QVERIFY(!file.path.contains('\\'));
                QVERIFY(!QDir::isAbsolutePath(file.path));
                if (!file.isDirectory) {
                    regular++;
                    present += QFileInfo::exists(FS::PathCombine(app.installDir, file.path)) ? 1 : 0;
                }
            }
            qInfo() << app.name << ":" << regular << "files in Steam's list," << present << "on disk";
            QVERIFY(regular > 0);
            QVERIFY(present > 0);
            if (app.appId == 489830) {
                // read-only look at the signed-in account's Steam playtime
                if (const auto minutes = Steam::playtimeMinutes(root, app.appId)) {
                    qInfo() << "Steam playtime of Skyrim Special Edition:" << *minutes << "minutes";
                    QVERIFY(*minutes >= 0);
                }
                // several apps can share a folder (a game and its DLC app); Skyrim has its own
                QCOMPARE(Steam::findAppByInstallDir(root, app.installDir)->appId, app.appId);
                const auto paths = [&] {
                    QStringList out;
                    for (const auto& file : *files) {
                        out << file.path;
                    }
                    return out;
                }();
                QVERIFY(paths.contains("SkyrimSE.exe"));
                QVERIFY(paths.contains("Data/Skyrim.esm"));
                // mods put into the Steam folder are not part of the game
                for (const auto& path : paths) {
                    QVERIFY(!path.startsWith("Data/SKSE", Qt::CaseInsensitive));
                    QVERIFY(!path.endsWith(".dmp", Qt::CaseInsensitive));
                }
            }
        }
        qInfo() << parsed << "apps had a cached file list";
    }
};

QTEST_GUILESS_MAIN(SteamTest)

#include "Steam_test.moc"
