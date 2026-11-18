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

#include <QSignalSpy>
#include <QFileInfo>
#include <cstring>
#include <QTemporaryDir>
#include <QTest>

#include <FileSystem.h>
#include <game/CrashReport.h>
#include <game/Deployer.h>
#include <game/GameInstance.h>
#include <game/ModList.h>
#include <game/PluginList.h>
#include <game/VersionCheck.h>
#include <settings/INISettingsObject.h>
#include <ui/dialogs/CrashReportDialog.h>

// no raw string literals in this file: moc skips the slots that follow one
namespace {

void writeFile(const QString& path, const QByteArray& content)
{
    QVERIFY(FS::ensureFilePathExists(path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
}

QByteArray field(const char* type, const QByteArray& data)
{
    QByteArray out(type, 4);
    out.append(static_cast<char>(data.size() & 0xFF));
    out.append(static_cast<char>((data.size() >> 8) & 0xFF));
    return out + data;
}

/// A plugin with just its TES4 record; headerSize 24 for Skyrim and Fallout, 20 for Oblivion
QByteArray plugin(const QStringList& masters, int headerSize = 24)
{
    QByteArray body = field("HEDR", QByteArray(12, '\0')) + field("CNAM", QByteArray("BetterNMM\0", 10));
    for (const auto& master : masters) {
        body += field("MAST", master.toLatin1() + '\0') + field("DATA", QByteArray(8, '\0'));
    }
    QByteArray header("TES4");
    for (int i = 0; i < 4; i++) {
        header.append(static_cast<char>((body.size() >> (8 * i)) & 0xFF));
    }
    header += QByteArray(headerSize - 8, '\0');
    return header + body;
}

void put32(QByteArray& data, qsizetype at, quint32 value)
{
    for (int i = 0; i < 4; i++) {
        data[at + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
    }
}

/// A DLL as the script extenders look at it: its machine, link time and named exports pointing at data
QByteArray fakeDll(const QList<std::pair<QByteArray, QByteArray>>& exports, quint16 machine = 0x8664, quint32 buildTime = 1760000000)
{
    const quint32 va = 0x1000;  // the one section, at file offset 0x200
    const quint32 count = static_cast<quint32>(exports.size());
    const quint32 functionsAt = 40;
    const quint32 namesAt = functionsAt + 4 * count;
    const quint32 ordinalsAt = namesAt + 4 * count;
    const quint32 stringsAt = ordinalsAt + 2 * count;
    QByteArray strings;
    QList<quint32> nameRvas;
    for (const auto& [name, data] : exports) {
        nameRvas << va + stringsAt + static_cast<quint32>(strings.size());
        strings += name + '\0';
    }
    const quint32 blobsAt = (stringsAt + static_cast<quint32>(strings.size()) + 7) & ~7u;
    QByteArray blobs;
    QList<quint32> dataRvas;
    for (const auto& [name, data] : exports) {
        dataRvas << va + blobsAt + static_cast<quint32>(blobs.size());
        blobs += data;
    }
    QByteArray section(blobsAt + blobs.size(), '\0');
    put32(section, 16, 1);  // ordinal base
    put32(section, 20, count);
    put32(section, 24, count);
    put32(section, 28, va + functionsAt);
    put32(section, 32, va + namesAt);
    put32(section, 36, va + ordinalsAt);
    for (quint32 i = 0; i < count; i++) {
        put32(section, functionsAt + 4 * i, dataRvas[i]);
        put32(section, namesAt + 4 * i, nameRvas[i]);
        section[ordinalsAt + 2 * i] = static_cast<char>(i);
    }
    section.replace(stringsAt, strings.size(), strings);
    section.replace(blobsAt, blobs.size(), blobs);

    QByteArray file(0x200, '\0');
    file[0] = 'M';
    file[1] = 'Z';
    put32(file, 0x3C, 0x40);
    file.replace(0x40, 4, QByteArray("PE\0\0", 4));
    file[0x44] = static_cast<char>(machine & 0xFF);
    file[0x45] = static_cast<char>(machine >> 8);
    file[0x46] = 1;  // one section
    put32(file, 0x48, buildTime);
    file[0x54] = static_cast<char>(240);  // PE32+ optional header with 16 directories
    file[0x58] = 0x0b;
    file[0x59] = 0x02;
    put32(file, 0x58 + 108, 16);
    put32(file, 0x58 + 112, va);
    put32(file, 0x58 + 116, static_cast<quint32>(section.size()));
    file.replace(0x148, 6, QByteArray(".edata"));
    put32(file, 0x148 + 8, static_cast<quint32>(section.size()));
    put32(file, 0x148 + 12, va);
    put32(file, 0x148 + 16, static_cast<quint32>(section.size()));
    put32(file, 0x148 + 20, 0x200);
    return file + section;
}

/// SKSEPluginVersionData
QByteArray skseData(quint32 independence, const QList<quint32>& versions, quint32 independenceEx = 0, quint32 seRequired = 0,
                    quint32 dataVersion = 1, const QByteArray& name = "Test Plugin")
{
    QByteArray data(848, '\0');
    put32(data, 0, dataVersion);
    put32(data, 4, 1);
    data.replace(8, name.size(), name);
    put32(data, 772, independenceEx);
    put32(data, 776, independence);
    for (int i = 0; i < versions.size(); i++) {
        put32(data, 780 + 4 * i, versions[i]);
    }
    put32(data, 844, seRequired);
    return data;
}

/// F4SEPluginVersionData and SFSEPluginVersionData
QByteArray xseData(quint32 address, quint32 structure, const QList<quint32>& versions, const QByteArray& name = "Test Plugin",
                   quint32 pluginVersion = 1, quint32 reservedBreaking = 0)
{
    QByteArray data(604, '\0');
    put32(data, 0, 1);
    put32(data, 4, pluginVersion);
    data.replace(8, name.size(), name);
    put32(data, 520, address);
    put32(data, 524, structure);
    for (int i = 0; i < versions.size(); i++) {
        put32(data, 528 + 4 * i, versions[i]);
    }
    put32(data, 600, reservedBreaking);
    return data;
}

/// A plugin whose HEDR gives @p version
QByteArray pluginWithHeader(float version, int headerSize = 24)
{
    QByteArray hedr(12, '\0');
    std::memcpy(hedr.data(), &version, sizeof(version));
    QByteArray body = field("HEDR", hedr);
    QByteArray header("TES4");
    for (int i = 0; i < 4; i++) {
        header.append(static_cast<char>((body.size() >> (8 * i)) & 0xFF));
    }
    header += QByteArray(headerSize - 8, '\0');
    return header + body;
}

std::unique_ptr<INISettingsObject> globalSettings(const QString& dir)
{
    auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(dir, "global.cfg"));
    for (const auto& [key, value] : { std::pair<const char*, QVariant>{ "ShowGameTime", true }, { "RecordGameTime", false },
                                      { "PreLoadCommand", "" }, { "PreLaunchCommand", "" }, { "PostExitCommand", "" }, { "ShowConsole", false },
                                      { "AutoCloseConsole", false }, { "ShowConsoleOnError", false }, { "LogPrePostOutput", true },
                                      { "ConsoleMaxLines", 1000 }, { "ConsoleOverflowStop", true }, { "Env", "{}" },
                                      { "QuitAfterGameStop", false }, { "CloseAfterLaunch", false } }) {
        settings->registerSetting(key, value);
    }
    return settings;
}

const QByteArray g_log =
    "Skyrim SSE v1.7.104\n"
    "CrashLoggerSSE v1-17-0-0\n\n"
    "Unhandled exception \"EXCEPTION_ACCESS_VIOLATION\" at 0x7FFE1C2D3E4F ModACrash.dll+0012345\n\n"
    "PROBABLE CALL STACK:\n"
    "\t[ 0] 0x7FFE1C2D3E4F ModACrash.dll+0012345\n"
    "\t[ 1] 0x7FFE1C2D0000 ModACrash.dll+000F000\n"
    "\t[ 2] 0x7FF6CDB9C4E4 SkyrimSE.exe+0BFC4E4\n\n"
    "REGISTERS:\n"
    "\tRCX 0x1E1A0000 (TESObjectREFR*)\n"
    "\t\tName: \"Bandit\"\n"
    "\t\tFile: \"Base.esm\"\n"
    "\t\tModified by: Base.esm -> ModB.esp\n\n"
    "MODULES:\n"
    "\tModACrash.dll 0x7FFE1C2C0000\n"
    "\tKERNEL32.dll 0x7FFE00000000\n"
    "SKSE PLUGINS:\n"
    "\tModACrash.dll v1\n"
    "PLUGINS:\n"
    "\t[00] Base.esm\n"
    "\t[01] ModA.esp\n"
    "\t[02] ModB.esp\n";

}  // namespace

class CrashReportTest : public QObject {
    Q_OBJECT

   private slots:
    void test_describe_code()
    {
        QVERIFY(CrashReport::describeCode(0xC0000005).contains("access violation"));
        QCOMPARE(CrashReport::describeCode(0x12345678), QString("error 0x12345678"));
        QCOMPARE(CrashReport::describeCode(0xDEAD), QString("error 0x0000DEAD"));
    }

    void test_section_and_mentions()
    {
        const auto section = CrashReport::crashSection(QString::fromUtf8(g_log));
        QVERIFY(section.contains("PROBABLE CALL STACK"));
        // the lists of everything loaded are cut off: they name every mod, crashed or not
        QVERIFY(!section.contains("MODULES:"));
        QVERIFY(!section.contains("[01] ModA.esp"));

        const QSet<QString> known{ "modacrash.dll", "moda.esp", "modb.esp", "unofficial skyrim special edition patch.esp" };
        const auto counts = CrashReport::mentions(section, known);
        QCOMPARE(counts.value("modacrash.dll"), 3);  // the exception line and two stack frames
        QCOMPARE(counts.value("modb.esp"), 1);
        QVERIFY(!counts.contains("moda.esp"));
        QVERIFY(!counts.contains("base.esm"));  // not a mod's file

        // names with spaces, after arrows, in quotes and after backticks (.NET Script Framework)
        const auto spaced = CrashReport::mentions(
            "Modified by: Skyrim.esm -> Unofficial Skyrim Special Edition Patch.esp\n"
            "File: `Skyrim.esm <- Unofficial Skyrim Special Edition Patch.esp`\n"
            "File: \"ModA.esp\"",
            known);
        QCOMPARE(spaced.value("unofficial skyrim special edition patch.esp"), 2);
        QCOMPARE(spaced.value("moda.esp"), 1);
    }

    void test_find_crash_log()
    {
        QTemporaryDir dir;
        const auto since = QDateTime::currentDateTime();
        writeFile(FS::PathCombine(dir.path(), "SKSE/crash-old.log"), "old");
        QFile old(FS::PathCombine(dir.path(), "SKSE/crash-old.log"));
        QVERIFY(old.open(QIODevice::ReadWrite));
        QVERIFY(old.setFileTime(since.addSecs(-3600), QFileDevice::FileModificationTime));
        old.close();
        writeFile(FS::PathCombine(dir.path(), "Saves/crash-in-saves.log"), "saves are never looked at");
        writeFile(FS::PathCombine(dir.path(), "a/b/c/crash-too-deep.log"), "deep");
        writeFile(FS::PathCombine(dir.path(), "SKSE/notes.log"), "not a crash log");
        QVERIFY(CrashReport::findCrashLog({ dir.path() }, since).isEmpty());

        writeFile(FS::PathCombine(dir.path(), "SKSE/Crashlogs/crash-2026-11-16.log"), "new");
        QCOMPARE(CrashReport::findCrashLog({ dir.path(), FS::PathCombine(dir.path(), "missing") }, since),
                 QDir(dir.path()).absoluteFilePath("SKSE/Crashlogs/crash-2026-11-16.log"));
    }

    void test_read_masters()
    {
        QTemporaryDir dir;
        writeFile(FS::PathCombine(dir.path(), "Skyrim.esp"), plugin({ "Skyrim.esm", "Update.esm" }));
        QCOMPARE(PluginList::readMasters(FS::PathCombine(dir.path(), "Skyrim.esp")), QStringList({ "Skyrim.esm", "Update.esm" }));
        writeFile(FS::PathCombine(dir.path(), "Oblivion.esp"), plugin({ "Oblivion.esm" }, 20));
        QCOMPARE(PluginList::readMasters(FS::PathCombine(dir.path(), "Oblivion.esp")), QStringList({ "Oblivion.esm" }));
        writeFile(FS::PathCombine(dir.path(), "None.esp"), plugin({}));
        QVERIFY(PluginList::readMasters(FS::PathCombine(dir.path(), "None.esp")).isEmpty());
        writeFile(FS::PathCombine(dir.path(), "Broken.esp"), "TES4\xff\xff");
        QVERIFY(PluginList::readMasters(FS::PathCombine(dir.path(), "Broken.esp")).isEmpty());
        QVERIFY(PluginList::readMasters(FS::PathCombine(dir.path(), "Missing.esp")).isEmpty());
    }

    /// SKSE64, F4SE and SFSE refuse a plugin for the reasons their PluginManager gives, per game version
    void test_wrong_version_dlls()
    {
        using VersionCheck::packVersion;
        using VersionCheck::ScriptExtender;
        QTemporaryDir dir;
        const auto data = dir.path();
        const auto skse = FS::PathCombine(data, "SKSE/Plugins");
        auto install = [&](const QString& folder, const QString& name, const QByteArray& bytes) { writeFile(FS::PathCombine(folder, name), bytes); };
        auto reasons = [&](ScriptExtender se, quint32 runtime, quint32 seVersion = 0) {
            QMap<QString, QString> out;
            for (const auto& finding : VersionCheck::checkPlugins(se, runtime, seVersion, data)) {
                out.insert(QFileInfo(finding.file).fileName(), finding.reason);
            }
            return out;
        };
        const auto v1_7_104 = packVersion(1, 7, 104);
        const auto v1_6_1170 = packVersion(1, 6, 1170);
        constexpr quint32 addressLibrary = 1, structs629 = 4, noStructUse = 1, libraryV5 = 2;
        install(skse, "Listed.dll", fakeDll({ { "SKSEPlugin_Version", skseData(0, { v1_6_1170, v1_7_104 }) }, { "SKSEPlugin_Load", "x" } }));
        install(skse, "OldListed.dll", fakeDll({ { "SKSEPlugin_Version", skseData(0, { v1_6_1170 }) } }));
        install(skse, "Independent.dll", fakeDll({ { "SKSEPlugin_Version", skseData(addressLibrary | structs629, {}, libraryV5) } }));
        install(skse, "BeforeV5.dll", fakeDll({ { "SKSEPlugin_Version", skseData(addressLibrary | structs629, {}) } }, 0x8664, 1700000000));
        install(skse, "AfterV5.dll", fakeDll({ { "SKSEPlugin_Version", skseData(addressLibrary | structs629, {}) } }, 0x8664, 1760000000));
        install(skse, "OldLayout.dll", fakeDll({ { "SKSEPlugin_Version", skseData(addressLibrary, {}, libraryV5) } }));
        install(skse, "NoStructs.dll", fakeDll({ { "SKSEPlugin_Version", skseData(addressLibrary, {}, libraryV5 | noStructUse) } }));
        install(skse, "SpecialEdition.dll", fakeDll({ { "SKSEPlugin_Query", "x" }, { "SKSEPlugin_Load", "x" } }));
        install(skse, "Helper.dll", fakeDll({ { "SomethingElse", "x" } }));
        install(skse, "Legendary.dll", fakeDll({ { "SKSEPlugin_Version", skseData(0, { v1_7_104 }) } }, 0x14c));
        install(skse, "Future.dll", fakeDll({ { "SKSEPlugin_Version", skseData(8, { v1_7_104 }) } }));
        install(skse, "NeedsNewer.dll", fakeDll({ { "SKSEPlugin_Version", skseData(0, { v1_7_104 }, 0, packVersion(2, 4, 0)) } }));
        install(skse, "NotADll.dll", "plain text");

        // game version 1.7.104 (SKSE 2.3.1), Address Library for 1.6.1170 only
        install(skse, "versionlib-1-6-1170-0.bin", "lib");
        auto found = reasons(ScriptExtender::SKSE64, v1_7_104, packVersion(2, 3, 1));
        QCOMPARE(found.keys(), (QStringList{ "AfterV5.dll", "BeforeV5.dll", "Future.dll", "Independent.dll", "Legendary.dll", "NeedsNewer.dll",
                                             "NoStructs.dll", "OldLayout.dll", "OldListed.dll", "SpecialEdition.dll" }));
        QVERIFY2(found["OldListed.dll"].contains("1.6.1170") && found["OldListed.dll"].contains("1.7.104"), qPrintable(found["OldListed.dll"]));
        QVERIFY2(found["Independent.dll"].contains("versionlib-1-7-104-0.bin") && found["Independent.dll"].contains("installed one is for 1.6.1170"),
                 qPrintable(found["Independent.dll"]));
        QVERIFY(found["Legendary.dll"].contains("32-bit"));
        QVERIFY(found["Future.dll"].contains("newer version"));
        QVERIFY(found["NeedsNewer.dll"].contains("2.4.0"));
        QVERIFY(found["SpecialEdition.dll"].contains("older version"));
        // with the Address Library for 1.7.104 the independent plugins load, except one built for the old Address Library
        install(skse, "versionlib-1-7-104-0.bin", "lib");
        found = reasons(ScriptExtender::SKSE64, v1_7_104, packVersion(2, 3, 1));
        QCOMPARE(found.keys(), (QStringList{ "BeforeV5.dll", "Future.dll", "Legendary.dll", "NeedsNewer.dll", "OldLayout.dll", "OldListed.dll",
                                             "SpecialEdition.dll" }));
        QVERIFY(found["BeforeV5.dll"].contains("Address Library changed"));
        QVERIFY(found["OldLayout.dll"].contains("before 1.6.629"));
        // the installed SKSE's own version unknown: no claim about needing a newer one
        QVERIFY(!reasons(ScriptExtender::SKSE64, v1_7_104).contains("NeedsNewer.dll"));

        // game version 1.6.1170 (SKSE 2.2.6-2.2.8): no Address Library v5 rule, unknown independence flags refused
        install(skse, "versionlib-1-6-1170-0.bin", "lib");
        found = reasons(ScriptExtender::SKSE64, v1_6_1170);
        QVERIFY(!found.contains("BeforeV5.dll"));
        QVERIFY(!found.contains("OldListed.dll"));
        QVERIFY(found.contains("Future.dll"));
        // 1.6.629 (SKSE 2.2.0) does not refuse unknown flags; 1.6.353 (SKSE 2.1.5) has no structure layout rule
        install(skse, "FlagListed.dll", fakeDll({ { "SKSEPlugin_Version", skseData(8, { packVersion(1, 6, 629), packVersion(1, 6, 640) }) } }));
        QVERIFY(!reasons(ScriptExtender::SKSE64, packVersion(1, 6, 629)).contains("FlagListed.dll"));
        QVERIFY(reasons(ScriptExtender::SKSE64, packVersion(1, 6, 640))["FlagListed.dll"].contains("newer version"));
        found = reasons(ScriptExtender::SKSE64, packVersion(1, 6, 353));
        QVERIFY(found.contains("Future.dll"));  // only address library and signature flags were known
        QVERIFY(found.contains("Independent.dll"));  // no Address Library for 1.6.353
        // 1.6.318 (SKSE 2.1.2) neither checks the Address Library nor the machine
        found = reasons(ScriptExtender::SKSE64, packVersion(1, 6, 318));
        QVERIFY(!found.contains("NoStructs.dll"));  // declares the Address Library, which is not there for 1.6.318
        QVERIFY(found["Independent.dll"].contains("newer version"));  // the structure flag came later
        QVERIFY(!found.contains("Legendary.dll") || !found["Legendary.dll"].contains("32-bit"));
        // 1.5.97 (SKSE 2.0.20) loads plugins through SKSEPlugin_Query: Anniversary Edition-only plugins are refused
        found = reasons(ScriptExtender::SKSE64, packVersion(1, 5, 97));
        QVERIFY(!found.contains("SpecialEdition.dll"));
        QVERIFY2(found["OldListed.dll"].contains("newer version"), qPrintable(found["OldListed.dll"]));
        QVERIFY(found["Legendary.dll"].contains("32-bit"));
        // a GOG game version only matches GOG builds
        QVERIFY(reasons(ScriptExtender::SKSE64, packVersion(1, 7, 104, 1)).contains("Listed.dll"));

        // Fallout 4 (F4SE)
        const auto f4se = FS::PathCombine(data, "F4SE/Plugins");
        const auto v1_11_240 = packVersion(1, 11, 240);
        install(f4se, "NextGen.dll", fakeDll({ { "F4SEPlugin_Version", xseData(2, 2, {}) } }));
        install(f4se, "Anniversary.dll", fakeDll({ { "F4SEPlugin_Version", xseData(4, 4, {}) } }));
        install(f4se, "Breaking.dll", fakeDll({ { "F4SEPlugin_Version", xseData(4, 4, {}, "Breaking", 1, 1) } }));
        install(f4se, "OldGen.dll", fakeDll({ { "F4SEPlugin_Query", "x" }, { "F4SEPlugin_Load", "x" } }));
        install(f4se, "version-1-11-240-0.bin", "lib");
        found = reasons(ScriptExtender::F4SE, v1_11_240);
        QCOMPARE(found.keys(), (QStringList{ "Breaking.dll", "NextGen.dll", "OldGen.dll" }));
        found = reasons(ScriptExtender::F4SE, packVersion(1, 10, 984));
        QVERIFY2(found["NextGen.dll"].contains("version-1-10-984-0.bin"), qPrintable(found["NextGen.dll"]));
        QVERIFY(found.contains("Anniversary.dll"));
        found = reasons(ScriptExtender::F4SE, packVersion(1, 10, 163));
        QCOMPARE(found.keys(), (QStringList{ "Anniversary.dll", "Breaking.dll", "NextGen.dll" }));

        // Starfield (SFSE)
        const auto sfse = FS::PathCombine(data, "SFSE/Plugins");
        const auto v1_16_244 = packVersion(1, 16, 244);
        install(sfse, "versionlib-1-16-244-0.bin", "lib");
        install(sfse, "Current.dll", fakeDll({ { "SFSEPlugin_Version", xseData(4, 8, {}) } }));
        install(sfse, "OldLayout.dll", fakeDll({ { "SFSEPlugin_Version", xseData(2, 4, {}) } }));
        install(sfse, "Baka.dll", fakeDll({ { "SFSEPlugin_Version", xseData(4, 8, {}, "BakaQuitGameFix", 1) } }));
        install(sfse, "EngineFixes.dll", fakeDll({ { "SFSEPlugin_Version", xseData(4, 8, {}, "Starfield Engine Fixes - SFSE Plugin by LarannKiar") } }));
        found = reasons(ScriptExtender::SFSE, v1_16_244);
        QCOMPARE(found.keys(), (QStringList{ "Baka.dll", "EngineFixes.dll", "OldLayout.dll" }));
        QVERIFY(found["Baka.dll"].contains("broken before plugin version 2.0.0"));
        QVERIFY(!reasons(ScriptExtender::SFSE, packVersion(1, 12, 32)).contains("OldLayout.dll") ||
                reasons(ScriptExtender::SFSE, packVersion(1, 12, 32))["OldLayout.dll"].contains("versionlib-1-12-32-0.bin"));
    }

    void test_wrong_version_extender_and_plugins()
    {
        using VersionCheck::packVersion;
        using VersionCheck::ScriptExtender;
        QTemporaryDir dir;
        const auto game = dir.path();
        // a script extender built for another game version
        QVERIFY(VersionCheck::checkScriptExtender(ScriptExtender::SKSE64, packVersion(1, 7, 104), game).isEmpty());
        writeFile(FS::PathCombine(game, "skse64_loader.exe"), "MZ");
        writeFile(FS::PathCombine(game, "skse64_1_6_1170.dll"), "MZ");
        writeFile(FS::PathCombine(game, "skse64_steam_loader.dll"), "MZ");
        const auto extender = VersionCheck::checkScriptExtender(ScriptExtender::SKSE64, packVersion(1, 7, 104), game);
        QCOMPARE(extender.size(), 1);
        QCOMPARE(extender[0].file, QString("skse64_1_6_1170.dll"));
        QVERIFY2(extender[0].reason.contains("1.6.1170") && extender[0].reason.contains("1.7.104"), qPrintable(extender[0].reason));
        writeFile(FS::PathCombine(game, "skse64_1_7_104.dll"), "MZ");
        QVERIFY(VersionCheck::checkScriptExtender(ScriptExtender::SKSE64, packVersion(1, 7, 104), game).isEmpty());

        // plugins below LOOT's minimum header version for the game: made for an older game
        const auto data = FS::PathCombine(game, "Data");
        writeFile(FS::PathCombine(data, "Legendary.esp"), pluginWithHeader(0.94f));
        writeFile(FS::PathCombine(data, "Special.esp"), pluginWithHeader(1.7f));
        writeFile(FS::PathCombine(data, "Creations.esp"), pluginWithHeader(1.71f));
        writeFile(FS::PathCombine(data, "Oblivion.esp"), pluginWithHeader(0.8f, 20));
        writeFile(FS::PathCombine(data, "Older.esp"), pluginWithHeader(0.7f, 20));
        const QStringList all{ "Legendary.esp", "Special.esp", "Creations.esp", "Missing.esp" };
        const auto skyrimSE = VersionCheck::checkPluginHeaders("skyrimspecialedition", data, all);
        QCOMPARE(skyrimSE.size(), 1);
        QCOMPARE(skyrimSE[0].file, QString("Legendary.esp"));
        QVERIFY2(skyrimSE[0].reason.contains("0.94") && skyrimSE[0].reason.contains("Legendary Edition"), qPrintable(skyrimSE[0].reason));
        QVERIFY(VersionCheck::checkPluginHeaders("skyrim", data, all).isEmpty());
        const auto oblivion = VersionCheck::checkPluginHeaders("oblivion", data, { "Oblivion.esp", "Older.esp" });
        QCOMPARE(oblivion.size(), 1);
        QCOMPARE(oblivion[0].file, QString("Older.esp"));
        QVERIFY(VersionCheck::checkPluginHeaders("cyberpunk2077", data, all).isEmpty());
    }

    void test_report()
    {
        QTemporaryDir dir;
        auto global = globalSettings(dir.path());
        const auto steam = FS::PathCombine(dir.path(), "steam");
        writeFile(FS::PathCombine(steam, "Data/Base.esm"), plugin({}));
        const auto root = FS::PathCombine(dir.path(), "instance");
        auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(root, "instance.cfg"));
        settings->registerSetting("InstanceType", "Game");
        GameInstance instance(global.get(), std::move(settings), root);
        GameDefinition game;
        game.domain = "testgame";
        game.name = "Test Game";
        game.executable = "Game.exe";
        game.dataPath = "Data";
        game.pluginFormat = PluginFormat::Asterisk;
        game.implicitPlugins = { "Base.esm" };
        GameInstance::applyDefinition(instance.settings(), game);
        instance.settings()->set("InstanceKind", "Main");
        instance.settings()->set("SourceGameDir", steam);

        const auto mods = instance.modsRoot();
        writeFile(FS::PathCombine(mods, "ModA/Data/ModA.esp"), plugin({ "Base.esm", "Missing.esm" }));
        writeFile(FS::PathCombine(mods, "ModA/Data/SKSE/Plugins/ModACrash.dll"), "dll");
        writeFile(FS::PathCombine(mods, "ModA/Data/textures/shared.dds"), "a");
        writeFile(FS::PathCombine(mods, "ModB/Data/ModB.esp"), plugin({ "Base.esm", "ModC.esm" }));
        writeFile(FS::PathCombine(mods, "ModB/Data/textures/shared.dds"), "b");
        writeFile(FS::PathCombine(mods, "ModC/Data/ModC.esm"), plugin({ "Base.esm" }));
        QVERIFY(instance.modList()->load().has_value());
        instance.modList()->setEnabled({ instance.modList()->indexOf("ModA"), instance.modList()->indexOf("ModB"),
                                         instance.modList()->indexOf("ModC") },
                                       true);
        auto deploy = instance.createDeployTask();
        QSignalSpy deployed(deploy.get(), &Task::finished);
        deploy->start();
        QVERIFY(deploy->isFinished() || deployed.wait(30000));
        QVERIFY2(deploy->wasSuccessful(), qPrintable(deploy->failReason()));
        // ModC.esm is there but not active
        writeFile(FS::PathCombine(root, "loadorder.txt"), "*ModA.esp\r\n*ModB.esp\r\nModC.esm\r\n");

        const auto since = QDateTime::currentDateTime().addSecs(-60);
        writeFile(FS::PathCombine(steam, "CrashLogs/crash-2026-11-16-12-00-00.log"), g_log);
        const auto report = CrashReport::analyze(&instance, since);

        QCOMPARE(QDir(report.logPath), QDir(FS::PathCombine(steam, "CrashLogs/crash-2026-11-16-12-00-00.log")));
        QCOMPARE(report.suspects.size(), 2);
        QCOMPARE(report.suspects[0].modName, QString("ModA"));
        QCOMPARE(report.suspects[0].files.value("ModACrash.dll"), 3);
        QCOMPARE(report.suspects[1].modName, QString("ModB"));

        QCOMPARE(report.missingMasters.size(), 2);
        for (const auto& missing : report.missingMasters) {
            if (missing.plugin == "ModA.esp") {
                QCOMPARE(missing.master, QString("Missing.esm"));
                QVERIFY(!missing.presentButInactive);
                QCOMPARE(missing.pluginMod, QString("ModA"));
            } else {
                QCOMPARE(missing.plugin, QString("ModB.esp"));
                QCOMPARE(missing.master, QString("ModC.esm"));
                QVERIFY(missing.presentButInactive);
            }
        }

        QCOMPARE(report.conflicts.size(), 1);
        QCOMPARE(report.conflicts[0].winner, QString("ModB"));
        QCOMPARE(report.conflicts[0].loser, QString("ModA"));
        QCOMPARE(report.conflicts[0].files, 1);
        QVERIFY(report.conflicts[0].involvesSuspect);

        const auto html = CrashReportDialog::toHtml(&instance, GameCrash{ .program = "Game.exe", .code = 0xC0000005, .started = since }, report);
        QVERIFY(html.contains("access violation"));
        QVERIFY(html.contains("ModACrash.dll"));
        QVERIFY(html.contains("Missing.esm"));
        QVERIFY(html.contains("installed but not active"));
        QVERIFY(html.contains("overwrites"));
        QVERIFY(report.tooManyPlugins.isEmpty());  // testgame has no known plugin limit
        QVERIFY(!html.contains("Too many plugins"));
        auto crowded = report;
        crowded.tooManyPlugins = { "255 full plugins are active; Test Game loads at most 254." };
        const auto crowdedHtml = CrashReportDialog::toHtml(&instance, GameCrash{ .program = "Game.exe", .code = 0xC0000005, .started = since }, crowded);
        QVERIFY(crowdedHtml.contains("Too many plugins") && crowdedHtml.contains("loads at most 254"));
        QVERIFY(report.wrongVersions.isEmpty());  // testgame has no minimum plugin header version
        QVERIFY(html.contains("Mods made for another version of the game"));

        // the plugins here have header version 0: in Skyrim Special Edition they are made for an older game
        instance.settings()->set("GameDomain", "skyrimspecialedition");
        const auto wrong = CrashReport::analyze(&instance, since);
        QStringList wrongFiles;
        for (const auto& item : wrong.wrongVersions) {
            wrongFiles << item.file + " " + item.modName;
        }
        std::sort(wrongFiles.begin(), wrongFiles.end());
        QCOMPARE(wrongFiles, (QStringList{ "Data/Base.esm ", "Data/ModA.esp ModA", "Data/ModB.esp ModB" }));
        const auto wrongHtml = CrashReportDialog::toHtml(&instance, GameCrash{ .program = "Game.exe", .code = 0xC0000005, .started = since }, wrong);
        QVERIFY2(wrongHtml.contains("<b>ModA.esp</b> from <b>ModA</b>") && wrongHtml.contains("Legendary Edition"), qPrintable(wrongHtml));
        QVERIFY(wrongHtml.contains("<b>Base.esm</b> (not from a mod)"));
        instance.settings()->set("GameDomain", "testgame");

        // without a crash log the report says what to install, for games that have a crash logger
        QVERIFY(QFile::remove(FS::PathCombine(steam, "CrashLogs/crash-2026-11-16-12-00-00.log")));
        const auto bare = CrashReport::analyze(&instance, since);
        QVERIFY(bare.logPath.isEmpty());
        QVERIFY(bare.suspects.isEmpty());
        QCOMPARE(bare.missingMasters.size(), 2);
        const GameCrash early{ .program = "Game.exe", .code = 0xC0000005, .started = QDateTime::currentDateTime().addSecs(-75) };
        QVERIFY(CrashReportDialog::toHtml(&instance, early, bare).contains("only the checks below"));  // testgame has no crash logger
        instance.settings()->set("GameDomain", "skyrimspecialedition");
        const auto hint = CrashReportDialog::toHtml(&instance, early, bare);
        QVERIFY2(hint.contains("Crash Logger SSE") && hint.contains("skyrimspecialedition/mods/59818"), qPrintable(hint));
        QVERIFY2(hint.contains("1min 15s after it started"), qPrintable(hint));
    }
};

QTEST_MAIN(CrashReportTest)

#include "CrashReport_test.moc"
