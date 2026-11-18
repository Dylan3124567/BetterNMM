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

#include <QDirListing>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <FileSystem.h>
#include <game/GameFiles.h>
#include <game/GameInstance.h>
#include <game/Ini.h>
#include <game/ModList.h>
#include <settings/INISettingsObject.h>
#include <vortex/VortexImport.h>
#include <vortex/VortexState.h>

namespace {

// no raw string literals in this file: moc skips the slots that follow one

void writeFile(const QString& path, const QByteArray& content = "x")
{
    QVERIFY(FS::ensureFilePathExists(path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

////// a small LevelDB writer: the format as LevelDB (and so Vortex) writes it //////

QByteArray varint(quint64 value)
{
    QByteArray out;
    while (value >= 0x80) {
        out += static_cast<char>((value & 0x7f) | 0x80);
        value >>= 7;
    }
    out += static_cast<char>(value);
    return out;
}

QByteArray prefixed(const QByteArray& bytes)
{
    return varint(static_cast<quint64>(bytes.size())) + bytes;
}

QByteArray le32(quint32 value)
{
    QByteArray out;
    for (int i = 0; i < 4; i++) {
        out += static_cast<char>((value >> (8 * i)) & 0xff);
    }
    return out;
}

QByteArray le64(quint64 value)
{
    return le32(static_cast<quint32>(value)) + le32(static_cast<quint32>(value >> 32));
}

quint32 maskedCrc(const QByteArray& bytes)
{
    const auto crc = VortexState::crc32c(bytes.constData(), static_cast<size_t>(bytes.size()));
    return ((crc >> 15) | (crc << 17)) + 0xa282ead8u;
}

/// Log file framing: records split into FIRST/MIDDLE/LAST fragments across 32 KiB blocks
QByteArray logFile(const QList<QByteArray>& records)
{
    QByteArray out;
    for (const auto& record : records) {
        qsizetype pos = 0;
        bool first = true;
        while (true) {
            const qsizetype left = 32768 - out.size() % 32768;
            if (left < 7) {
                out += QByteArray(left, '\0');
                continue;
            }
            const qsizetype n = std::min(record.size() - pos, left - 7);
            const bool last = pos + n == record.size();
            const char type = first && last ? 1 : first ? 2 : last ? 4 : 3;
            const auto data = record.mid(pos, n);
            out += le32(maskedCrc(QByteArray(1, type) + data)) + static_cast<char>(n & 0xff) + static_cast<char>(n >> 8) + type + data;
            pos += n;
            first = false;
            if (last) {
                break;
            }
        }
    }
    return out;
}

struct Op {
    QByteArray key;
    QByteArray value;
    bool deletion = false;
};

QByteArray batch(quint64 sequence, const QList<Op>& ops)
{
    QByteArray out = le64(sequence) + le32(static_cast<quint32>(ops.size()));
    for (const auto& op : ops) {
        out += op.deletion ? QByteArray(1, 0) + prefixed(op.key) : QByteArray(1, 1) + prefixed(op.key) + prefixed(op.value);
    }
    return out;
}

QByteArray internalKey(const QByteArray& key, quint64 sequence, bool deletion)
{
    return key + le64((sequence << 8) | (deletion ? 0 : 1));
}

/// Entries sorted by key, prefix-compressed with a restart point every @p interval entries
QByteArray block(const QList<std::pair<QByteArray, QByteArray>>& entries, int interval = 2)
{
    QByteArray out;
    QList<quint32> restarts;
    QByteArray previous;
    for (qsizetype i = 0; i < entries.size(); i++) {
        const auto& [key, value] = entries[i];
        qsizetype shared = 0;
        if (i % interval == 0) {
            restarts << static_cast<quint32>(out.size());
        } else {
            while (shared < key.size() && shared < previous.size() && key[shared] == previous[shared]) {
                shared++;
            }
        }
        out += varint(static_cast<quint64>(shared)) + varint(static_cast<quint64>(key.size() - shared)) +
               varint(static_cast<quint64>(value.size())) + key.mid(shared) + value;
        previous = key;
    }
    if (restarts.isEmpty()) {
        restarts << 0;
    }
    for (const auto offset : restarts) {
        out += le32(offset);
    }
    return out + le32(static_cast<quint32>(restarts.size()));
}

/// Snappy made only of literals: valid input for any Snappy reader (copies are covered by test_snappy)
QByteArray snappyLiterals(const QByteArray& raw)
{
    QByteArray out = varint(static_cast<quint64>(raw.size()));
    for (qsizetype pos = 0; pos < raw.size(); pos += 256) {
        const auto chunk = raw.mid(pos, 256);
        out += static_cast<char>(60 << 2);
        out += static_cast<char>(chunk.size() - 1);
        out += chunk;
    }
    return out;
}

/// A table with one data block (Snappy or plain), an empty meta index, its index and footer
QByteArray table(const QList<std::pair<QByteArray, QByteArray>>& entries, bool snappy)
{
    QByteArray file;
    auto addBlock = [&](const QByteArray& raw, bool compress) {
        const auto offset = file.size();
        const auto contents = compress ? snappyLiterals(raw) : raw;
        const char type = compress ? 1 : 0;
        file += contents + type + le32(maskedCrc(contents + type));
        return varint(static_cast<quint64>(offset)) + varint(static_cast<quint64>(contents.size()));
    };
    const auto data = addBlock(block(entries), snappy);
    const auto meta = addBlock(block({}), false);
    const auto index = addBlock(block({ { entries.last().first, data } }, 1), false);
    auto footer = meta + index;
    footer += QByteArray(40 - footer.size(), '\0');
    return file + footer + le64(0xdb4775248b80fb57ull);
}

QByteArray newFile(quint64 number, quint64 size, const QByteArray& smallest, const QByteArray& largest)
{
    return varint(7) + varint(0) + varint(number) + varint(size) + prefixed(smallest) + prefixed(largest);
}

using Entries = std::map<std::string, std::string>;

void put(Entries& entries, const QString& key, const QJsonValue& value)
{
    entries[key.toStdString()] = QJsonDocument(QJsonArray{ value }).toJson(QJsonDocument::Compact).mid(1).chopped(1).toStdString();
}

QByteArray msgpackString(const QByteArray& text)
{
    return (text.size() < 32 ? QByteArray(1, static_cast<char>(0xa0 | text.size()))
                             : QByteArray(1, static_cast<char>(0xd9)) + static_cast<char>(text.size())) +
           text;
}

/// Every file below @p dir with its size and time, to show nothing there changed
QMap<QString, QString> snapshot(const QString& dir)
{
    QMap<QString, QString> out;
    for (const auto& entry : QDirListing(dir, QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly)) {
        out.insert(entry.absoluteFilePath(),
                   QString("%1 %2").arg(entry.size()).arg(entry.lastModified(QTimeZone::UTC).toMSecsSinceEpoch()));
    }
    return out;
}

}  // namespace

class VortexImportTest : public QObject {
    Q_OBJECT

   private slots:
    void test_crc32c()
    {
        QCOMPARE(VortexState::crc32c("123456789", 9), 0xe3069283u);
        const QByteArray zeros(32, '\0');
        QCOMPARE(VortexState::crc32c(zeros.constData(), 32), 0x8a9136aau);
    }

    void test_snappy()
    {
        std::string out;
        // a literal, then a copy with a one-byte offset that overlaps what it writes
        const QByteArray copy1 = QByteArray::fromHex(
            "0c0861626315"
            "03");
        QVERIFY(VortexState::snappyUncompress(copy1.constData(), static_cast<size_t>(copy1.size()), out));
        QCOMPARE(QByteArray::fromStdString(out), QByteArray("abcabcabcabc"));
        // two-byte offset
        const QByteArray copy2 = QByteArray::fromHex("0a0461621e0200");
        QVERIFY(VortexState::snappyUncompress(copy2.constData(), static_cast<size_t>(copy2.size()), out));
        QCOMPARE(QByteArray::fromStdString(out), QByteArray("ababababab"));
        // four-byte offset
        const QByteArray copy4 = QByteArray::fromHex("06087879") + QByteArray("z") + QByteArray::fromHex("0b03000000");
        QVERIFY(VortexState::snappyUncompress(copy4.constData(), static_cast<size_t>(copy4.size()), out));
        QCOMPARE(QByteArray::fromStdString(out), QByteArray("xyzxyz"));
        // a literal longer than 60 bytes carries its length in an extra byte
        const QByteArray text(100, 'q');
        const QByteArray longLiteral = varint(100) + static_cast<char>(60 << 2) + static_cast<char>(99) + text;
        QVERIFY(VortexState::snappyUncompress(longLiteral.constData(), static_cast<size_t>(longLiteral.size()), out));
        QCOMPARE(QByteArray::fromStdString(out), text);

        // damaged input is refused, never read past
        for (const auto& bad : { QByteArray::fromHex("0c08616263150000"),    // trailing garbage
                                 QByteArray::fromHex("040105"),              // copy from before the start
                                 QByteArray::fromHex("05046162050a"),        // offset beyond the output
                                 QByteArray::fromHex("0a0861"),              // literal cut short
                                 QByteArray::fromHex("03046162046364") }) {  // more output than announced
            QVERIFY2(!VortexState::snappyUncompress(bad.constData(), static_cast<size_t>(bad.size()), out), bad.toHex().constData());
        }
    }

    void test_read_database()
    {
        QTemporaryDir dir;
        const auto db = dir.path();
        // the live table: a and b, c, and a deletion of d; Snappy compressed
        const auto live = table({ { internalKey("a", 1, false), "1" },
                                  { internalKey("b", 2, false), "2" },
                                  { internalKey("c", 3, false), "3" },
                                  { internalKey("d", 4, true), "" },
                                  { internalKey("persistent###profiles###p1###name", 5, false), "\"Main\"" } },
                                true);
        writeFile(FS::PathCombine(db, "000005.ldb"), live);
        // a table compaction replaced: still on disk, no longer in the database
        const auto stale = table({ { internalKey("d", 0, false), "old" }, { internalKey("e", 0, false), "stale" } }, false);
        writeFile(FS::PathCombine(db, "000003.ldb"), stale);
        // the write-ahead log: b changes, c goes, a value that spans log blocks, then a record torn by a crash
        const QByteArray big(40000, 'v');
        auto log = logFile({ batch(10, { { "b", "20" }, { "c", "", true } }), batch(12, { { "f", big } }) });
        log += le32(0x12345678) + QByteArray::fromHex("6400") + static_cast<char>(1) + "cut";
        writeFile(FS::PathCombine(db, "000006.log"), log);
        // a log older than the tables is obsolete
        writeFile(FS::PathCombine(db, "000004.log"), logFile({ batch(100, { { "a", "obsolete" } }) }));
        const auto edit1 = varint(1) + prefixed("leveldb.BytewiseComparator") + varint(2) + varint(4) + varint(3) + varint(5) + varint(4) +
                           varint(4) +
                           newFile(3, static_cast<quint64>(stale.size()), internalKey("d", 0, false), internalKey("e", 0, false));
        const auto edit2 = varint(2) + varint(6) + varint(3) + varint(7) + varint(4) + varint(5) + varint(6) + varint(0) + varint(3) +
                           newFile(5, static_cast<quint64>(live.size()), internalKey("a", 1, false),
                                   internalKey("persistent###profiles###p1###name", 5, false));
        writeFile(FS::PathCombine(db, "MANIFEST-000002"), logFile({ edit1, edit2 }));
        writeFile(FS::PathCombine(db, "CURRENT"), "MANIFEST-000002\n");

        std::map<std::string, std::string> entries;
        std::string error;
        QVERIFY2(VortexState::readDatabase(QDir::toNativeSeparators(db).toStdWString(), entries, error), error.c_str());
        QCOMPARE(entries.size(), size_t(4));
        QCOMPARE(entries.at("a"), std::string("1"));
        QCOMPARE(entries.at("b"), std::string("20"));
        QCOMPARE(entries.at("f"), big.toStdString());
        QCOMPARE(entries.at("persistent###profiles###p1###name"), std::string("\"Main\""));
        QVERIFY(!entries.count("c") && !entries.count("d") && !entries.count("e"));

        // a damaged table is reported, not half read
        auto damaged = live;
        damaged[3] = static_cast<char>(damaged[3] ^ 0x40);
        writeFile(FS::PathCombine(db, "000005.ldb"), damaged);
        QVERIFY(!VortexState::readDatabase(QDir::toNativeSeparators(db).toStdWString(), entries, error));
        QVERIFY2(QString::fromStdString(error).contains("damaged"), error.c_str());
        QFile::remove(FS::PathCombine(db, "CURRENT"));
        QVERIFY(!VortexState::readDatabase(QDir::toNativeSeparators(db).toStdWString(), entries, error));

        auto missing = VortexImport::load(FS::PathCombine(db, "nothing-here"));
        QVERIFY(!missing.has_value());
        QVERIFY2(missing.error().contains("not set up"), qPrintable(missing.error()));
    }

    void test_json_string()
    {
        QCOMPARE(VortexState::jsonString("\"a\\\"b\\\\c\\u00e9\\ud83d\\ude00\\n\"").value_or("-"),
                 std::string("a\"b\\c\xc3\xa9\xf0\x9f\x98\x80\n"));
        QVERIFY(!VortexState::jsonString("42").has_value());
        QVERIFY(!VortexState::jsonString("\"bad \\x\"").has_value());
        QCOMPARE(VortexState::splitKey("persistent###mods###skyrimse"), (std::vector<std::string>{ "persistent", "mods", "skyrimse" }));
    }

    void test_known_games_match_the_registry()
    {
        for (const auto& game : VortexState::knownGames()) {
            const auto definition = GameRegistry::byDomain(QString::fromLatin1(game.domain));
            QVERIFY2(definition.isValid(), game.domain);
            QCOMPARE(definition.name, QString::fromUtf8(game.name));
        }
        QCOMPARE(QString::fromLatin1(VortexState::gameByVortexId("skyrimse")->domain), QString("skyrimspecialedition"));
        QCOMPARE(QString::fromLatin1(VortexState::gameByVortexId("falloutnv")->domain), QString("newvegas"));
        QVERIFY(!VortexState::gameByVortexId("kerbalspaceprogram"));
    }

    void test_state_from_entries()
    {
        Entries entries;
        const QString mod = "persistent###mods###skyrimse###";
        put(entries, mod + "ModA###installationPath", "ModA-100-1-0");
        put(entries, mod + "ModA###state", "installed");
        put(entries, mod + "ModA###type", "");
        put(entries, mod + "ModA###attributes###customFileName", "My A");
        put(entries, mod + "ModA###attributes###logicalFileName", "A logical");
        put(entries, mod + "ModA###attributes###name", "A name");
        put(entries, mod + "ModA###attributes###modId", 100);
        put(entries, mod + "ModA###attributes###fileId", "200");
        put(entries, mod + "ModA###attributes###version", "1.0");
        put(entries, mod + "ModA###attributes###downloadGame", "skyrimse");
        put(entries, mod + "ModA###attributes###fileName", "ModA-100-1-0.7z");
        put(entries, mod + "ModA###attributes###installTime", "2026-03-19T19:15:21.367Z");
        put(entries, mod + "ModA###rules",
            QJsonArray{ QJsonObject{ { "type", "after" }, { "reference", QJsonObject{ { "id", "ModB" } } } } });
        put(entries, mod + "ModB###installationPath", "ModB");
        put(entries, mod + "ModB###state", "installed");
        put(entries, mod + "ModB###type", "dinput");
        put(entries, mod + "ModB###attributes###logicalFileName", "B logical");
        put(entries, mod + "ModB###attributes###downloadGame", "falloutnv");
        put(entries, mod + "ModC###installationPath", "ModC");
        put(entries, mod + "ModC###state", "installing");
        put(entries, mod + "ModD###state", "installed");
        put(entries, "persistent###mods###kerbalspaceprogram###K###installationPath", "K");
        put(entries, "persistent###mods###kerbalspaceprogram###K###state", "installed");

        put(entries, "persistent###profiles###P1###gameId", "skyrimse");
        put(entries, "persistent###profiles###P1###name", "Main");
        put(entries, "persistent###profiles###P1###lastActivated", 200);
        put(entries, "persistent###profiles###P1###modState###ModA###enabled", true);
        put(entries, "persistent###profiles###P1###modState###ModB###enabled", false);
        put(entries, "persistent###profiles###P1###features###local_saves", true);
        put(entries, "persistent###profiles###P2###gameId", "skyrimse");
        put(entries, "persistent###profiles###P2###name", "Second");
        put(entries, "persistent###profiles###P2###lastActivated", 300);
        put(entries, "persistent###profiles###P2###features###local_game_settings", true);
        put(entries, "persistent###profiles###P3###gameId", "kerbalspaceprogram");

        const QString discovered = "settings###gameMode###discovered###skyrimse###";
        put(entries, discovered + "path", "C:\\Games\\Skyrim Special Edition");
        put(entries, discovered + "tools###skse###name", "SKSE");
        put(entries, discovered + "tools###skse###path", "C:\\Games\\Skyrim Special Edition\\skse64_loader.exe");
        put(entries, discovered + "tools###skse###parameters", QJsonArray{ "-a", "two words" });
        put(entries, discovered + "tools###hidden###path", "C:\\Tools\\x.exe");
        put(entries, discovered + "tools###hidden###hidden", true);
        put(entries, discovered + "tools###missing###name", "Never found");
        put(entries, "settings###mods###installPath###skyrimse", "{USERDATA}\\{GAME}\\staging");
        put(entries, "settings###downloads###path", "D:\\VortexDownloads");
        put(entries, "settings###profiles###lastActiveProfile###skyrimse", "P1");
        put(entries, "persistent###downloads###files###dl1###game", QJsonArray{ "skyrimse" });
        put(entries, "persistent###downloads###files###dl1###state", "finished");
        put(entries, "persistent###downloads###files###dl1###localPath", "ModA-100-1-0.7z");
        put(entries, "persistent###downloads###files###dl2###game", QJsonArray{ "fallout4", "skyrimse" });
        put(entries, "persistent###downloads###files###dl2###state", "finished");
        put(entries, "persistent###downloads###files###dl2###localPath", "Shared.zip");
        put(entries, "persistent###downloads###files###dl3###game", QJsonArray{ "skyrimse" });
        put(entries, "persistent###downloads###files###dl3###state", "paused");
        put(entries, "persistent###downloads###files###dl3###localPath", "Partial.zip");

        const auto state = VortexImport::fromEntries(entries, "C:/Users/me/AppData/Roaming/Vortex");
        QCOMPARE(state.games.size(), 1);
        const auto& game = state.games.first();
        QCOMPARE(game.vortexId, QString("skyrimse"));
        QCOMPARE(game.definition.domain, QString("skyrimspecialedition"));
        QCOMPARE(game.gameDir, QString("C:/Games/Skyrim Special Edition"));
        QCOMPARE(game.stagingDir, QString("C:/Users/me/AppData/Roaming/Vortex/skyrimse/staging"));
        QCOMPARE(game.deployedProfile, QString("P1"));
        QCOMPARE(game.downloads, (QStringList{ "D:/VortexDownloads/fallout4/Shared.zip", "D:/VortexDownloads/skyrimse/ModA-100-1-0.7z" }));

        QCOMPARE(game.mods.size(), 2);
        const auto& a = game.mods[0];
        QCOMPARE(a.id, QString("ModA"));
        QCOMPARE(a.folder, QString("ModA-100-1-0"));
        QCOMPARE(a.name, QString("My A"));
        QCOMPARE(a.nexusGame, QString("skyrimspecialedition"));
        QCOMPARE(a.nexusModId, 100);
        QCOMPARE(a.nexusFileId, 200);
        QCOMPARE(a.version, QString("1.0"));
        QCOMPARE(a.archive, QString("ModA-100-1-0.7z"));
        QCOMPARE(a.installTime, QDateTime(QDate(2026, 3, 19), QTime(19, 15, 21, 367), QTimeZone::UTC).toMSecsSinceEpoch());
        QCOMPARE(a.rules.size(), 1);
        const auto& b = game.mods[1];
        QCOMPARE(b.name, QString("B logical"));
        QCOMPARE(b.type, QString("dinput"));
        QCOMPARE(b.nexusGame, QString("newvegas"));

        QCOMPARE(game.tools.size(), 1);
        QCOMPARE(game.tools[0].name, QString("SKSE"));
        QCOMPARE(game.tools[0].path, QString("C:/Games/Skyrim Special Edition/skse64_loader.exe"));
        QCOMPARE(game.tools[0].arguments, QString("-a \"two words\""));

        // most recently used first; the unknown game's profile is left out
        QCOMPARE(state.profiles.size(), 2);
        QCOMPARE(state.profiles[0].id, QString("P2"));
        QVERIFY(state.profiles[0].localSettings && !state.profiles[0].localSaves);
        const auto* main = state.profile("P1");
        QVERIFY(main);
        QCOMPARE(main->name, QString("Main"));
        QCOMPARE(main->enabledMods, QSet<QString>{ "ModA" });
        QVERIFY(main->localSaves);
        QCOMPARE(main->folder, QString("C:/Users/me/AppData/Roaming/Vortex/skyrimse/profiles/P1"));

        // setup sees the same profiles through the plain reader
        const auto plain = VortexState::profiles(entries);
        QCOMPARE(plain.size(), size_t(2));
        QCOMPARE(plain[0].id, std::string("P2"));
        QCOMPARE(plain[1].enabledMods, 1);
        QCOMPARE(plain[1].name, std::string("Main"));
    }

    void test_type_targets()
    {
        QTemporaryDir dir;
        const auto gameDir = FS::PathCombine(dir.path(), "game");
        const auto staging = FS::PathCombine(dir.path(), "staging");
        // the default type's manifest sits in Data, whatever path it recorded
        writeFile(FS::PathCombine(gameDir, "Data/vortex.deployment.json"),
                  QJsonDocument(QJsonObject{ { "targetPath", "Z:\\moved\\Data" }, { "files", QJsonArray{} } }).toJson());
        writeFile(FS::PathCombine(gameDir, "vortex.deployment.dinput.json"), "{}");
        // three levels down is further than Vortex deploys
        writeFile(FS::PathCombine(gameDir, "a/b/c/vortex.deployment.deep.json"), "{}");
        // the staging folder's MessagePack copy, its file list before the target path
        QByteArray pack(1, static_cast<char>(0x83));
        pack += msgpackString("version") + QByteArray(1, 1);
        pack += msgpackString("files") + QByteArray(1, static_cast<char>(0x92)) + QByteArray(1, static_cast<char>(0x82)) +
                msgpackString("relPath") + msgpackString("enbseries\\a.fx") + msgpackString("time") +
                QByteArray(1, static_cast<char>(0xcf)) + QByteArray(8, 1) + QByteArray(1, static_cast<char>(0x80));
        pack += msgpackString("targetPath") + msgpackString(QDir::toNativeSeparators(FS::PathCombine(gameDir, "enbseries")).toUtf8());
        writeFile(FS::PathCombine(staging, "vortex.deployment.enb.msgpack"), pack);
        writeFile(FS::PathCombine(staging, "vortex.deployment.json"),
                  QJsonDocument(QJsonObject{ { "targetPath", QDir::toNativeSeparators(FS::PathCombine(gameDir, "Elsewhere")) } }).toJson());

        VortexImport::Game game;
        game.vortexId = "skyrimse";
        game.definition = GameRegistry::byDomain("skyrimspecialedition");
        game.gameDir = gameDir;
        game.stagingDir = staging;
        const auto targets = VortexImport::typeTargets(game);
        QCOMPARE(targets.value(""), QString("Data"));
        QVERIFY(targets.contains("dinput"));
        QCOMPARE(targets.value("dinput"), QString(""));
        QCOMPARE(targets.value("enb"), QString("enbseries"));
        QVERIFY(!targets.contains("deep"));

        // without manifests: Vortex's defaults where they are fixed, nothing where its game extension decides
        VortexImport::Game witcher;
        witcher.vortexId = "witcher3";
        witcher.definition = GameRegistry::byDomain("witcher3");
        QCOMPARE(VortexImport::typeTargets(witcher).value("", "-"), QString("Mods"));
        VortexImport::Game elden;
        elden.vortexId = "eldenring";
        elden.definition = GameRegistry::byDomain("eldenring");
        QVERIFY(!VortexImport::typeTargets(elden).contains(""));
    }

    void test_deployed_files_below_the_game_folder()
    {
        QTemporaryDir dir;
        const auto gameDir = dir.path();
        const QJsonObject manifest{ { "files",
                                      QJsonArray{ QJsonObject{ { "relPath", "modengine2\\a.dll" }, { "source", "ModEngine" } },
                                                  QJsonObject{ { "relPath", "launchmod_eldenring.bat" }, { "source", "ModEngine" } } } } };
        writeFile(FS::PathCombine(gameDir, "Game/vortex.deployment.eldenring-eldenmodloaders-modtype.json"),
                  QJsonDocument(manifest).toJson());
        VortexImport::Game game;
        game.vortexId = "eldenring";
        game.definition = GameRegistry::byDomain("eldenring");
        game.gameDir = gameDir;
        const auto deployed = VortexImport::deployedFiles(game);
        QCOMPARE(deployed.value("game/modengine2/a.dll"), QString("ModEngine"));
        QCOMPARE(deployed.size(), 2);
        // the files an isolated instance must not take for game files
        const auto known = GameFiles::vortexDeployedFiles(gameDir, QString());
        QVERIFY(known.contains("game/modengine2/a.dll"));
        QVERIFY(known.contains("game/launchmod_eldenring.bat"));
        QVERIFY(known.contains("game/vortex.deployment.eldenring-eldenmodloaders-modtype.json"));
    }

    void test_order()
    {
        auto mod = [](const QString& id, const QString& name) {
            VortexImport::Mod m;
            m.id = id;
            m.folder = id + "-folder";
            m.name = name;
            m.logicalFileName = name + " file";
            return m;
        };
        auto a = mod("A", "Alpha");
        auto b = mod("B", "Bravo");
        auto c = mod("C", "Charlie");
        auto d = mod("D", "Delta");
        a.rules << QJsonObject{ { "type", "after" }, { "reference", QJsonObject{ { "id", "B" } } } };
        c.rules << QJsonObject{ { "type", "before" }, { "reference", QJsonObject{ { "logicalFileName", "alpha FILE" } } } };
        // contradicts A's rule
        b.rules << QJsonObject{ { "type", "after" }, { "reference", QJsonObject{ { "id", "A" } } } };
        b.rules << QJsonObject{ { "type", "requires" }, { "reference", QJsonObject{ { "id", "D" } } } };
        // a file name pattern, matched against the archive's name
        c.archive = "Charlie-123-1-0.7z";
        d.rules << QJsonObject{ { "type", "before" }, { "reference", QJsonObject{ { "fileExpression", "CHARLIE-*" } } } };
        const QList<VortexImport::Mod> mods{ a, b, c, d };
        QHash<QString, QSet<QString>> files{ { "B", { "data/x.esp" } }, { "D", { "data/x.esp" } } };
        QStringList warnings;
        // nothing deployed: rules and names decide
        QCOMPARE(VortexImport::order(mods, files, {}, { "A", "B", "C", "D" }, &warnings), (QStringList{ "B", "D", "C", "A" }));
        QCOMPARE(warnings.size(), 1);
        QVERIFY2(warnings.first().contains("Bravo") && warnings.first().contains("Alpha"), qPrintable(warnings.first()));
        // Vortex deployed B's copy of x.esp: B wins over D
        warnings.clear();
        const QHash<QString, QString> winners{ { "data/x.esp", "B-FOLDER" } };
        QCOMPARE(VortexImport::order(mods, files, winners, { "A", "B", "C", "D" }, &warnings), (QStringList{ "D", "B", "C", "A" }));
        // a disabled mod's copy did not take part in deployment
        QCOMPARE(VortexImport::order(mods, files, winners, { "A", "B", "C" }, &warnings), (QStringList{ "B", "D", "C", "A" }));
    }

    void test_save_folders()
    {
        QTemporaryDir dir;
        const auto appData = FS::PathCombine(dir.path(), "appdata");
        const auto oldAppData = qgetenv("APPDATA");
        qputenv("APPDATA", QDir::toNativeSeparators(appData).toLocal8Bit());
        const auto restore = qScopeGuard([&] { qputenv("APPDATA", oldAppData); });
        const auto instance = FS::PathCombine(dir.path(), "instance");
        VortexImport::Profile profile;
        profile.id = "P1";

        const auto elden = VortexImport::saveFolders(GameRegistry::byDomain("eldenring"), profile, "C:/Games/ER", instance);
        QCOMPARE(elden.size(), 1);
        QCOMPARE(elden[0].from, FS::PathCombine(appData, "EldenRing"));
        QCOMPARE(elden[0].to, FS::PathCombine(instance, "profile", GameInstance::roamingAppDataSubfolder(), "EldenRing"));

        const auto morrowind = VortexImport::saveFolders(GameRegistry::byDomain("morrowind"), profile, "C:/Games/Morrowind", instance);
        QCOMPARE(morrowind.size(), 1);
        QCOMPARE(morrowind[0].from, QString("C:/Games/Morrowind/Saves"));
        QCOMPARE(morrowind[0].to, FS::PathCombine(instance, "game", "Saves"));

        const auto documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        auto skyrim = VortexImport::saveFolders(GameRegistry::byDomain("skyrimspecialedition"), profile, "C:/Games/SSE", instance);
        QCOMPARE(skyrim.size(), 1);
        QCOMPARE(skyrim[0].from, FS::PathCombine(documents, "My Games/Skyrim Special Edition/Saves"));
        QCOMPARE(skyrim[0].to,
                 FS::PathCombine(instance, "profile", GameInstance::documentsSubfolder(), "My Games/Skyrim Special Edition/Saves"));
        profile.localSaves = true;
        skyrim = VortexImport::saveFolders(GameRegistry::byDomain("skyrimspecialedition"), profile, "C:/Games/SSE", instance);
        QCOMPARE(skyrim[0].from, FS::PathCombine(documents, "My Games/Skyrim Special Edition/Saves/P1"));
        QVERIFY(skyrim[0].to.endsWith("Saves/P1"));

        // Saved Games is only written when the instance has its own
        const auto cyberpunk = VortexImport::saveFolders(GameRegistry::byDomain("cyberpunk2077"), profile, "C:/Games/CP", instance);
        QCOMPARE(cyberpunk.size(), GameInstance::sharedProfileFolders().contains("Saved Games") ? 0 : 1);
    }

    void test_point_at_profile_saves()
    {
        QTemporaryDir dir;
        // an existing INI keeps every other line
        const auto ini = FS::PathCombine(dir.path(), "Skyrim.ini");
        writeFile(ini, "[General]\r\nsLanguage=ENGLISH\r\nSLocalSavePath=Saves\\\r\n\r\n[Display]\r\nfShadowLODMaxStartFade=1000.0\r\n");
        QVERIFY(VortexImport::pointAtProfileSaves(ini, "P1").has_value());
        QCOMPARE(readFile(ini),
                 QByteArray(
                     "[General]\r\nsLanguage=ENGLISH\r\nSLocalSavePath=Saves\\P1\\\r\n\r\n[Display]\r\nfShadowLODMaxStartFade=1000.0\r\n"));
        // a profile without an INI of its own gets one with only that
        const auto fresh = FS::PathCombine(dir.path(), "Fallout.ini");
        QVERIFY(VortexImport::pointAtProfileSaves(fresh, "abc").has_value());
        const auto document = Ini::Document::fromBytes(readFile(fresh));
        QCOMPARE(document.value("General", "SLocalSavePath").value_or("-"), QString("Saves\\abc\\"));
    }

    void test_import_profile()
    {
        QTemporaryDir dir;
        const auto root = dir.path();
        const auto userData = FS::PathCombine(root, "vortex");
        const auto gameDir = FS::PathCombine(root, "ELDEN RING");
        const auto staging = FS::PathCombine(userData, "eldenring/mods");
        const auto appData = FS::PathCombine(root, "appdata");
        const auto oldAppData = qgetenv("APPDATA");
        qputenv("APPDATA", QDir::toNativeSeparators(appData).toLocal8Bit());
        const auto restore = qScopeGuard([&] { qputenv("APPDATA", oldAppData); });

        // the game as Vortex left it: ModEngine 2 deployed into Game/
        writeFile(FS::PathCombine(gameDir, "Game/eldenring.exe"), "MZ game");
        writeFile(FS::PathCombine(gameDir, "Game/regulation.bin"), "regulation");
        writeFile(FS::PathCombine(gameDir, "Game/launchmod_eldenring.bat"), "deployed");
        const QJsonObject manifest{
            { "files", QJsonArray{ QJsonObject{ { "relPath", "launchmod_eldenring.bat" }, { "source", "ModEngine-2" } } } }
        };
        writeFile(FS::PathCombine(gameDir, "Game/vortex.deployment.eldenring-eldenmodloaders-modtype.json"),
                  QJsonDocument(manifest).toJson());
        // Vortex's staging folder
        writeFile(FS::PathCombine(staging, "ModEngine-2/launchmod_eldenring.bat"), "launch");
        writeFile(FS::PathCombine(staging, "ModEngine-2/config_eldenring.toml"), "config");
        writeFile(FS::PathCombine(staging, "ModEngine-2/modengine2/README.md"), "readme");
        writeFile(FS::PathCombine(staging, "Seamless/SeamlessCoop/ersc.dll"), "coop");
        writeFile(FS::PathCombine(staging, "Unused/unused.txt"), "unused");
        writeFile(FS::PathCombine(userData, "downloads/eldenring/ModEngine-2.zip"), "archive");
        writeFile(FS::PathCombine(userData, "eldenring/profiles/P1/readme.txt"), "profile");
        // the real saves, which must stay as they are
        writeFile(FS::PathCombine(appData, "EldenRing/76561198000000000/ER0000.sl2"), "save");
        const auto vortexBefore = snapshot(userData);
        const auto savesBefore = snapshot(appData);

        Entries entries;
        const QString mods = "persistent###mods###eldenring###";
        for (const auto& [id, type, name, enabled] :
             { std::tuple{ "ModEngine-2", "eldenring-eldenmodloaders-modtype", "ModEngine2", true },
               std::tuple{ "Seamless", "", "Seamless Co-op", true }, std::tuple{ "Unused", "", "Unused", false } }) {
            put(entries, mods + id + "###installationPath", id);
            put(entries, mods + id + "###state", "installed");
            put(entries, mods + id + "###type", type);
            put(entries, mods + id + "###attributes###name", name);
            put(entries, "persistent###profiles###P1###modState###" + QString(id) + "###enabled", enabled);
        }
        put(entries, mods + "Seamless###rules",
            QJsonArray{ QJsonObject{ { "type", "after" }, { "reference", QJsonObject{ { "id", "ModEngine-2" } } } } });
        put(entries, mods + "Seamless###attributes###modId", 510);
        put(entries, mods + "Seamless###attributes###fileId", 7000);
        put(entries, mods + "Seamless###attributes###version", "1.9.6");
        put(entries, mods + "Seamless###attributes###downloadGame", "eldenring");
        put(entries, "persistent###profiles###P1###gameId", "eldenring");
        put(entries, "persistent###profiles###P1###name", "Multiplayer");
        put(entries, "settings###gameMode###discovered###eldenring###path", QDir::toNativeSeparators(gameDir));
        put(entries, "settings###gameMode###discovered###eldenring###tools###modengine2###name", "ModEngine 2");
        put(entries, "settings###gameMode###discovered###eldenring###tools###modengine2###path",
            QDir::toNativeSeparators(FS::PathCombine(gameDir, "Game/launchmod_eldenring.bat")));
        put(entries, "persistent###downloads###files###d1###game", QJsonArray{ "eldenring" });
        put(entries, "persistent###downloads###files###d1###state", "finished");
        put(entries, "persistent###downloads###files###d1###localPath", "ModEngine-2.zip");
        put(entries, "settings###profiles###lastActiveProfile###eldenring", "P1");
        const auto state = VortexImport::fromEntries(entries, userData);
        QCOMPARE(state.games.size(), 1);
        QCOMPARE(state.profiles.size(), 1);

        auto global = std::make_unique<INISettingsObject>(FS::PathCombine(root, "global.cfg"));
        // the global settings an instance reads, as Isolation_test registers them
        for (const auto& [key, value] : { std::pair<const char*, QVariant>{ "ShowGameTime", true },
                                          { "RecordGameTime", false },
                                          { "PreLoadCommand", "" },
                                          { "PreLaunchCommand", "" },
                                          { "PostExitCommand", "" },
                                          { "ShowConsole", false },
                                          { "AutoCloseConsole", false },
                                          { "ShowConsoleOnError", false },
                                          { "LogPrePostOutput", true },
                                          { "ConsoleMaxLines", 1000 },
                                          { "ConsoleOverflowStop", true },
                                          { "Env", "{}" },
                                          { "QuitAfterGameStop", false },
                                          { "CloseAfterLaunch", false } }) {
            global->registerSetting(key, value);
        }
        const auto instanceRoot = FS::PathCombine(root, "instances/imported");
        const auto downloads = FS::PathCombine(root, "bnmm-downloads");
        VortexImportTask task(state.games.first(), state.profiles.first(), {},
                              { .saves = true, .downloads = true, .downloadsRoot = downloads });
        task.setStagingPath(instanceRoot);
        task.setParentSettings(global.get());
        task.setName("Elden Ring (Multiplayer)");
        QSignalSpy finished(&task, &Task::finished);
        task.start();
        QVERIFY(finished.wait(60000));
        QVERIFY2(task.wasSuccessful(), qPrintable(task.failReason()));

        // the game folder holds the game, not what Vortex deployed into it
        QVERIFY(QFileInfo::exists(FS::PathCombine(instanceRoot, "game/Game/eldenring.exe")));
        QVERIFY(QFileInfo::exists(FS::PathCombine(instanceRoot, "game/Game/regulation.bin")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(instanceRoot, "game/Game/launchmod_eldenring.bat")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(instanceRoot, "game/Game/vortex.deployment.eldenring-eldenmodloaders-modtype.json")));

        ModList list(FS::PathCombine(instanceRoot, "mods"), FS::PathCombine(instanceRoot, "mods.json"));
        QVERIFY(list.load().has_value());
        QStringList names;
        for (const auto& entry : list.mods()) {
            names << entry.name + (entry.enabled ? "+" : "-");
        }
        QCOMPARE(names, (QStringList{ "ModEngine2+", "Seamless Co-op+", "Unused-" }));
        const auto& coop = list.mods()[1];
        QCOMPARE(coop.nexusGame, QString("eldenring"));
        QCOMPARE(coop.nexusModId, 510);
        QCOMPARE(coop.nexusFileId, 7000);
        QCOMPARE(coop.version, QString("1.9.6"));
        // ModEngine's files go where Vortex deployed its kind of mod; the default kind was never deployed
        const auto engine = list.modDir(list.mods()[0].id);
        QCOMPARE(readFile(FS::PathCombine(engine, "Game/launchmod_eldenring.bat")), QByteArray("launch"));
        QVERIFY(QFileInfo::exists(FS::PathCombine(list.modDir(coop.id), "SeamlessCoop/ersc.dll")));
        // plain files are links to Vortex's copies, configs are copies of their own
        QVERIFY(GameFiles::isSameFile(FS::PathCombine(staging, "ModEngine-2/modengine2/README.md"),
                                      FS::PathCombine(engine, "Game/modengine2/README.md")));
        QVERIFY(!GameFiles::isSameFile(FS::PathCombine(staging, "ModEngine-2/config_eldenring.toml"),
                                       FS::PathCombine(engine, "Game/config_eldenring.toml")));
        QVERIFY(
            std::any_of(task.warnings().cbegin(), task.warnings().cend(), [](const QString& w) { return w.contains("Seamless Co-op"); }));

        // the saves are copies in the instance's own AppData
        const auto save =
            FS::PathCombine(instanceRoot, "profile", GameInstance::roamingAppDataSubfolder(), "EldenRing/76561198000000000/ER0000.sl2");
        QCOMPARE(readFile(save), QByteArray("save"));
        QVERIFY(!GameFiles::isSameFile(save, FS::PathCombine(appData, "EldenRing/76561198000000000/ER0000.sl2")));
        QCOMPARE(readFile(FS::PathCombine(downloads, "eldenring/ModEngine-2.zip")), QByteArray("archive"));

        // Vortex's tool runs from the instance's own game folder
        auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(instanceRoot, "instance.cfg"));
        settings->registerSetting("InstanceType", "");
        GameInstance instance(global.get(), std::move(settings), instanceRoot);
        const auto custom = instance.customExecutables();
        QCOMPARE(custom.size(), 1);
        QCOMPARE(custom[0].name, QString("ModEngine 2"));
        QCOMPARE(custom[0].path, QString("Game/launchmod_eldenring.bat"));
        QVERIFY(std::any_of(task.messages().cbegin(), task.messages().cend(),
                            [](const QString& m) { return m.contains("Copied 1 save file"); }));

        // nothing of Vortex's and none of the saves changed
        QCOMPARE(snapshot(userData), vortexBefore);
        QCOMPARE(snapshot(appData), savesBefore);
    }

    /// Plans bringing over every profile of this PC's Vortex, reading only; set BETTERNMM_TEST_REAL_VORTEX=1 to run it
    void test_real_vortex_read_only()
    {
        if (qEnvironmentVariable("BETTERNMM_TEST_REAL_VORTEX") != "1") {
            QSKIP("set BETTERNMM_TEST_REAL_VORTEX=1 to plan an import from this PC's Vortex");
        }
        const auto database = QDir::fromNativeSeparators(QString::fromStdWString(VortexState::defaultDatabase()));
        const auto userData = QFileInfo(database).absolutePath();
        const auto before = snapshot(userData);
        auto state = VortexImport::load();
        QVERIFY2(state.has_value(), qPrintable(state ? QString() : state.error()));
        for (const auto& profile : state->profiles) {
            const auto* game = state->game(profile.gameId);
            QVERIFY(game);
            qInfo().noquote() << QString("profile %1 \"%2\" of %3, game folder %4, staging %5, %6 enabled")
                                     .arg(profile.id, profile.name, game->definition.name, game->gameDir, game->stagingDir)
                                     .arg(profile.enabledMods.size());
            const auto targets = VortexImport::typeTargets(*game);
            for (auto it = targets.cbegin(); it != targets.cend(); ++it) {
                qInfo().noquote() << QString("  type \"%1\" -> \"%2\"").arg(it.key(), it.value());
            }
            QHash<QString, QSet<QString>> files;
            for (const auto& mod : game->mods) {
                const auto source = FS::PathCombine(game->stagingDir, mod.folder);
                QVERIFY2(QFileInfo(source).isDir(), qPrintable(source));
                QVERIFY2(targets.contains(mod.type), qPrintable(mod.type));
                const auto prefix = targets.value(mod.type);
                QSet<QString> paths;
                const QDir root(source);
                for (const auto& entry : QDirListing(source, QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly)) {
                    const auto rel = root.relativeFilePath(entry.absoluteFilePath());
                    paths.insert((prefix.isEmpty() ? rel : prefix + '/' + rel).toLower());
                }
                files.insert(mod.id, paths);
                qInfo().noquote() << QString("  mod \"%1\" (%2) %3, %4 files under \"%5\", Nexus %6 %7/%8")
                                         .arg(mod.name, mod.id, profile.enabledMods.contains(mod.id) ? "enabled" : "disabled")
                                         .arg(paths.size())
                                         .arg(prefix, mod.nexusGame)
                                         .arg(mod.nexusModId)
                                         .arg(mod.nexusFileId);
            }
            // everything Vortex deployed for the active profile comes from a mod the plan places at that same path
            const auto deployed = game->deployedProfile == profile.id ? VortexImport::deployedFiles(*game) : QHash<QString, QString>();
            for (auto it = deployed.cbegin(); it != deployed.cend(); ++it) {
                const auto mod = std::find_if(game->mods.cbegin(), game->mods.cend(),
                                              [&](const VortexImport::Mod& m) { return m.folder == it.value(); });
                QVERIFY2(mod != game->mods.cend(), qPrintable(it.value()));
                QVERIFY2(files.value(mod->id).contains(it.key()), qPrintable(it.key()));
            }
            QStringList warnings;
            qInfo().noquote() << "  order:" << VortexImport::order(game->mods, files, deployed, profile.enabledMods, &warnings).join(", ");
            QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
            for (const auto& folder : VortexImport::saveFolders(game->definition, profile, game->gameDir, "C:/instance")) {
                qInfo().noquote()
                    << QString("  saves %1 -> %2 (%3)").arg(folder.from, folder.to, QFileInfo(folder.from).isDir() ? "present" : "none");
            }
            for (const auto& tool : game->tools) {
                qInfo().noquote() << QString("  tool \"%1\" %2 -> %3")
                                         .arg(tool.name, tool.path,
                                              VortexImport::insideGamePath(game->gameDir, tool.path).value_or("outside the game"));
            }
            qInfo().noquote() << "  downloads:" << game->downloads.join(", ");
        }
        QCOMPARE(snapshot(userData), before);
    }
};

QTEST_GUILESS_MAIN(VortexImportTest)

#include "VortexImport_test.moc"
