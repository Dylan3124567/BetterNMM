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
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <FileSystem.h>
#include <game/ModList.h>
#include <game/ModListExport.h>
#include <game/PluginList.h>

namespace {

void writeFile(const QString& path, const QByteArray& content = "x")
{
    QVERIFY(FS::ensureFilePathExists(path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
}

/// A plugin with a TES4 header carrying @p flags
QByteArray plugin(quint32 flags)
{
    QByteArray data("TES4");
    data += QByteArray(4, '\0');
    for (int i = 0; i < 4; i++) {
        data.append(static_cast<char>((flags >> (8 * i)) & 0xFF));
    }
    return data + QByteArray(12, '\0');
}

/// A plugin whose header lists @p masters (MAST fields after HEDR)
QByteArray pluginNeeding(const QStringList& masters, quint32 flags = 0)
{
    QByteArray fields("HEDR");
    fields += QByteArray("\x0c\x00", 2) + QByteArray(12, '\0');
    for (const auto& master : masters) {
        const auto name = master.toLatin1() + '\0';
        fields += "MAST";
        fields.append(static_cast<char>(name.size() & 0xFF)).append(static_cast<char>(name.size() >> 8));
        fields += name;
    }
    QByteArray data("TES4");
    for (int i = 0; i < 4; i++) {
        data.append(static_cast<char>((fields.size() >> (8 * i)) & 0xFF));
    }
    for (int i = 0; i < 4; i++) {
        data.append(static_cast<char>((flags >> (8 * i)) & 0xFF));
    }
    return data + QByteArray(12, '\0') + fields;
}

QString readText(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QStringList names(const PluginList& list)
{
    QStringList out;
    for (const auto& p : list.plugins()) {
        out << (p.active ? "*" : "") + p.name;
    }
    return out;
}

}  // namespace

class LoadOrderTest : public QObject {
    Q_OBJECT

   private slots:
    void test_asterisk_format()
    {
        QTemporaryDir dir;
        const auto data = FS::PathCombine(dir.path(), "game/Data");
        writeFile(FS::PathCombine(data, "Skyrim.esm"), plugin(1));
        writeFile(FS::PathCombine(data, "Update.esm"), plugin(1));
        writeFile(FS::PathCombine(data, "ccFish.esm"), plugin(1));
        writeFile(FS::PathCombine(data, "Apple.esp"), plugin(0));
        writeFile(FS::PathCombine(data, "LightMaster.esp"), plugin(0x201));
        writeFile(FS::PathCombine(data, "Zed.esp"), plugin(0));
        writeFile(FS::PathCombine(dir.path(), "game/Skyrim.ccc"), "ccFish.esm\r\n");
        const auto appData = FS::PathCombine(dir.path(), "AppData/Skyrim Special Edition");
        // what the game had before BetterNMM: Zed before Apple, Apple off
        writeFile(FS::PathCombine(appData, "plugins.txt"), "# comment\n*Zed.esp\nApple.esp\n*Missing.esp\n");

        GameDefinition game;
        game.pluginFormat = PluginFormat::Asterisk;
        game.implicitPlugins = { "Skyrim.esm", "Update.esm", "Dawnguard.esm" };
        game.cccFile = "Skyrim.ccc";
        PluginList list(game, { .dataDir = data,
                                .gameDir = FS::PathCombine(dir.path(), "game"),
                                .orderFile = FS::PathCombine(dir.path(), "loadorder.txt"),
                                .gameListDir = appData });
        QVERIFY(list.refresh().has_value());
        // implicit first, then the master-flagged plugin, then the imported order, then new ones
        QCOMPARE(names(list), QStringList({ "*Skyrim.esm", "*Update.esm", "*ccFish.esm", "LightMaster.esp", "*Zed.esp", "Apple.esp" }));
        QVERIFY(list.plugins()[3].master);
        QVERIFY(list.plugins()[3].light);

        list.setActive({ 5 }, true);
        list.moveRows({ 5 }, -1);
        QCOMPARE(names(list), QStringList({ "*Skyrim.esm", "*Update.esm", "*ccFish.esm", "LightMaster.esp", "*Apple.esp", "*Zed.esp" }));
        // implicit plugins can not be moved or switched off
        list.setActive({ 0 }, false);
        list.moveRows({ 0 }, 2);
        QVERIFY(list.plugins()[0].active);
        QCOMPARE(list.plugins()[0].name, QString("Skyrim.esm"));
        // a plugin can not move above the masters
        list.moveRows({ 4 }, -3);
        QCOMPARE(list.plugins()[4].name, QString("Apple.esp"));

        const auto written = readText(FS::PathCombine(appData, "plugins.txt"));
        QCOMPARE(written.split('\n', Qt::SkipEmptyParts).mid(1),
                 QStringList({ "LightMaster.esp", "*Apple.esp", "*Zed.esp" }));

        // the game rewrites plugins.txt on start; BetterNMM's own order survives and is written back
        writeFile(FS::PathCombine(appData, "plugins.txt"), "Zed.esp\nApple.esp\n");
        PluginList again(game, { .dataDir = data,
                                 .gameDir = FS::PathCombine(dir.path(), "game"),
                                 .orderFile = FS::PathCombine(dir.path(), "loadorder.txt"),
                                 .gameListDir = appData });
        QVERIFY(again.refresh().has_value());
        QVERIFY(again.writeGameFiles().has_value());
        QCOMPARE(readText(FS::PathCombine(appData, "plugins.txt")).split('\n', Qt::SkipEmptyParts).mid(1),
                 QStringList({ "LightMaster.esp", "*Apple.esp", "*Zed.esp" }));
    }

    void test_mod_list_export()
    {
        const QList<ModEntry> mods{
            ModEntry{ .id = "SkyUI", .name = "SkyUI", .enabled = true, .version = "5.2SE", .nexusGame = "skyrimspecialedition", .nexusModId = 12604 },
            ModEntry{ .id = "Mine", .name = "My | own, \"tweaks\"", .enabled = false },
        };
        const QStringList plugins{ "Skyrim.esm", "SkyUI_SE.esp" };
        QCOMPARE(ModListExport::formatFor("list.CSV"), ModListExport::Format::Csv);
        QCOMPARE(ModListExport::formatFor("list.txt"), ModListExport::Format::Text);
        QCOMPARE(ModListExport::formatFor("list.md"), ModListExport::Format::Markdown);
        QCOMPARE(ModListExport::formatFor("list"), ModListExport::Format::Markdown);

        const auto markdown = ModListExport::write("Main (Skyrim Special Edition)", mods, plugins, ModListExport::Format::Markdown);
        QVERIFY(markdown.startsWith("# Main (Skyrim Special Edition)\n"));
        QVERIFY(markdown.contains("2 mods, 1 enabled, 2 active plugins."));
        QVERIFY2(markdown.contains("| 1 | ✓ | SkyUI | 5.2SE | [12604](https://www.nexusmods.com/skyrimspecialedition/mods/12604) |"),
                 qPrintable(markdown));
        // a '|' in a name would split the table
        QVERIFY2(markdown.contains("| 2 |   | My \\| own, \"tweaks\" |  |   |"), qPrintable(markdown));
        QVERIFY(markdown.contains("## Load order\n\n1. Skyrim.esm\n2. SkyUI_SE.esp\n"));

        const auto csv = ModListExport::write("Main", mods, plugins, ModListExport::Format::Csv);
        QCOMPARE(csv, QString("Priority,Enabled,Name,Version,Nexus Mods page\r\n"
                              "1,yes,SkyUI,5.2SE,https://www.nexusmods.com/skyrimspecialedition/mods/12604\r\n"
                              "2,no,\"My | own, \"\"tweaks\"\"\",,\r\n"));

        const auto text = ModListExport::write("Main", mods, plugins, ModListExport::Format::Text);
        QVERIFY2(text.contains("  1. [x] SkyUI 5.2SE  https://www.nexusmods.com/skyrimspecialedition/mods/12604\r\n"), qPrintable(text));
        QVERIFY(text.contains("  2. [ ] My | own, \"tweaks\"\r\n"));
        QVERIFY(text.contains("Load order\r\n  1. Skyrim.esm\r\n  2. SkyUI_SE.esp\r\n"));

        // separators are headings, not mods
        auto grouped = mods;
        grouped.insert(1, ModEntry{ .id = "|separator|1", .name = "Tweaks", .separator = true });
        const auto groupedMarkdown = ModListExport::write("Main", grouped, plugins, ModListExport::Format::Markdown);
        QVERIFY(groupedMarkdown.contains("2 mods, 1 enabled"));
        QVERIFY2(groupedMarkdown.contains("|   |   | **Tweaks** |   |   |\n| 2 |"), qPrintable(groupedMarkdown));
        QCOMPARE(ModListExport::write("Main", grouped, plugins, ModListExport::Format::Csv),
                 QString("Priority,Enabled,Name,Version,Nexus Mods page\r\n"
                         "1,yes,SkyUI,5.2SE,https://www.nexusmods.com/skyrimspecialedition/mods/12604\r\n"
                         ",,== Tweaks ==,,\r\n"
                         "2,no,\"My | own, \"\"tweaks\"\"\",,\r\n"));
        QVERIFY(ModListExport::write("Main", grouped, plugins, ModListExport::Format::Text).contains("     --- Tweaks ---\r\n  2. [ ] My"));
    }

    void test_plugin_slots()
    {
        QTemporaryDir dir;
        const auto data = FS::PathCombine(dir.path(), "game/Data");
        writeFile(FS::PathCombine(data, "Skyrim.esm"), plugin(1));
        // 253 full plugins besides the base game, 3 light ones (an .esl, and two ESL-flagged .esp files)
        for (int i = 0; i < 253; i++) {
            writeFile(FS::PathCombine(data, QString("Full%1.esp").arg(i, 3, 10, QChar('0'))), plugin(0));
        }
        writeFile(FS::PathCombine(data, "Light.esl"), plugin(1));
        writeFile(FS::PathCombine(data, "LightA.esp"), plugin(0x200));
        writeFile(FS::PathCombine(data, "LightB.esp"), plugin(0x200));
        GameDefinition game;
        game.name = "Skyrim Special Edition";
        game.pluginFormat = PluginFormat::Asterisk;
        game.implicitPlugins = { "Skyrim.esm" };
        game.fullPluginSlots = 254;
        game.lightPluginSlots = 4096;
        PluginList list(game, { .dataDir = data,
                                .gameDir = FS::PathCombine(dir.path(), "game"),
                                .orderFile = FS::PathCombine(dir.path(), "loadorder.txt"),
                                .gameListDir = FS::PathCombine(dir.path(), "AppData") });
        QVERIFY(list.refresh().has_value());
        QList<int> all;
        for (int i = 0; i < list.plugins().size(); i++) {
            all << i;
        }
        list.setActive(all, true);
        QCOMPARE(list.slotUse().full, 254);
        QCOMPARE(list.slotUse().light, 3);
        QVERIFY2(list.loadProblems().isEmpty(), qPrintable(list.loadProblems().join(" | ")));

        // one more full plugin than the game has slots for
        writeFile(FS::PathCombine(data, "OneTooMany.esp"), plugin(0));
        QVERIFY(list.refresh().has_value());
        const int added = static_cast<int>(list.plugins().size()) - 1;
        QCOMPARE(list.plugins()[added].name, QString("OneTooMany.esp"));
        list.setActive({ added }, true);
        QCOMPARE(list.slotUse().full, 255);
        QCOMPARE(list.loadProblems().size(), 1);
        QVERIFY2(list.loadProblems().first().contains("255 full plugins are active; Skyrim Special Edition loads at most 254"),
                 qPrintable(list.loadProblems().first()));
        QCOMPARE(list.slotProblems(), list.loadProblems());

        // a game without light plugins: the ESL flag means nothing there, so every plugin takes a full slot
        game.lightPluginSlots = 0;
        game.fullPluginSlots = 255;
        game.name = "Fallout: New Vegas";
        PluginList old(game, { .dataDir = data,
                               .gameDir = FS::PathCombine(dir.path(), "game"),
                               .orderFile = FS::PathCombine(dir.path(), "old-order.txt"),
                               .gameListDir = FS::PathCombine(dir.path(), "OldAppData") });
        QVERIFY(old.refresh().has_value());
        QList<int> everything;
        for (int i = 0; i < old.plugins().size(); i++) {
            everything << i;
        }
        old.setActive(everything, true);
        QCOMPARE(old.slotUse().full, 258);
        QCOMPARE(old.slotUse().light, 0);
        QVERIFY(old.loadProblems().join(" ").contains("258 plugins are active; Fallout: New Vegas loads at most 255"));

        // a plugin whose master is missing is named too, with its master
        writeFile(FS::PathCombine(data, "Orphan.esp"), pluginNeeding({ "Gone.esm" }));
        QVERIFY(list.refresh().has_value());
        list.setActive({ static_cast<int>(list.plugins().size()) - 1 }, true);
        QVERIFY(list.loadProblems().contains("Orphan.esp: Missing master: Gone.esm"));
    }

    void test_missing_and_inactive_masters_are_shown()
    {
        QTemporaryDir dir;
        const auto data = FS::PathCombine(dir.path(), "game/Data");
        writeFile(FS::PathCombine(data, "Skyrim.esm"), plugin(1));
        writeFile(FS::PathCombine(data, "Addon.esm"), pluginNeeding({ "Skyrim.esm" }, 1));
        writeFile(FS::PathCombine(data, "Patch.esp"), pluginNeeding({ "Skyrim.esm", "Addon.esm" }));
        writeFile(FS::PathCombine(data, "Orphan.esp"), pluginNeeding({ "Skyrim.esm", "Gone.esm" }));
        GameDefinition game;
        game.pluginFormat = PluginFormat::Asterisk;
        game.implicitPlugins = { "Skyrim.esm" };
        PluginList list(game, { .dataDir = data,
                                .gameDir = FS::PathCombine(dir.path(), "game"),
                                .orderFile = FS::PathCombine(dir.path(), "loadorder.txt"),
                                .gameListDir = FS::PathCombine(dir.path(), "AppData") });
        QVERIFY(list.refresh().has_value());
        QCOMPARE(names(list), QStringList({ "*Skyrim.esm", "Addon.esm", "Orphan.esp", "Patch.esp" }));
        auto flags = [&list](int row) { return list.data(list.index(row, PluginList::FlagsColumn), Qt::DisplayRole).toString(); };
        auto red = [&list](int row) { return list.data(list.index(row, PluginList::NameColumn), Qt::ForegroundRole).isValid(); };

        // inactive plugins do not load, so their masters do not matter
        QVERIFY(!flags(2).contains("master:"));
        list.setActive({ 2, 3 }, true);
        QCOMPARE(flags(2), QString("Missing master: Gone.esm"));
        QCOMPARE(flags(3), QString("Inactive master: Addon.esm"));
        QVERIFY(red(2) && red(3) && !red(0));
        QVERIFY(list.data(list.index(2, PluginList::NameColumn), Qt::ToolTipRole).toString().contains("crashes"));

        list.setActive({ 1 }, true);
        QVERIFY(!flags(3).contains("master:"));
        QVERIFY(!red(3));
        list.setActive({ 2 }, false);
        QVERIFY(!red(2));
    }

    void test_timestamp_and_textfile_formats()
    {
        QTemporaryDir dir;
        const auto data = FS::PathCombine(dir.path(), "Data");
        const auto base = QDateTime(QDate(2010, 1, 1), QTime(12, 0), QTimeZone::UTC);
        for (const auto& [name, minutes] : { std::pair{ "FalloutNV.esm", 0 }, std::pair{ "B.esp", 5 }, std::pair{ "A.esp", 10 } }) {
            const auto path = FS::PathCombine(data, name);
            writeFile(path, plugin(QString(name).endsWith(".esm") ? 1 : 0));
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(base.addSecs(minutes * 60), QFileDevice::FileModificationTime));
        }
        GameDefinition game;
        game.pluginFormat = PluginFormat::Timestamp;
        game.implicitPlugins = { "FalloutNV.esm" };
        const auto appData = FS::PathCombine(dir.path(), "AppData");
        PluginList list(game, { .dataDir = data, .gameDir = dir.path(), .orderFile = FS::PathCombine(dir.path(), "order.txt"), .gameListDir = appData });
        QVERIFY(list.refresh().has_value());
        QCOMPARE(names(list), QStringList({ "*FalloutNV.esm", "B.esp", "A.esp" }));
        list.setActive({ 1, 2 }, true);
        list.moveRows({ 2 }, -1);  // A before B: A's time has to move before B's
        auto time = [&](const char* name) { return QFileInfo(FS::PathCombine(data, name)).lastModified(QTimeZone::UTC); };
        QVERIFY(time("FalloutNV.esm") < time("A.esp"));
        QVERIFY(time("A.esp") < time("B.esp"));
        QCOMPARE(time("FalloutNV.esm"), base);  // the base master keeps its date
        QCOMPARE(readText(FS::PathCombine(appData, "plugins.txt")).split('\n', Qt::SkipEmptyParts).mid(1),
                 QStringList({ "FalloutNV.esm", "A.esp", "B.esp" }));

        GameDefinition skyrimLe;
        skyrimLe.pluginFormat = PluginFormat::Textfile;
        skyrimLe.implicitPlugins = { "FalloutNV.esm" };
        const auto leData = FS::PathCombine(dir.path(), "le");
        PluginList le(skyrimLe, { .dataDir = data, .gameDir = dir.path(), .orderFile = FS::PathCombine(dir.path(), "le-order.txt"), .gameListDir = leData });
        QVERIFY(le.refresh().has_value());
        le.setActive({ 2 }, true);
        QCOMPARE(readText(FS::PathCombine(leData, "loadorder.txt")).split('\n', Qt::SkipEmptyParts),
                 QStringList({ "FalloutNV.esm", "A.esp", "B.esp" }));
        QCOMPARE(readText(FS::PathCombine(leData, "plugins.txt")).split('\n', Qt::SkipEmptyParts).mid(1), QStringList({ "B.esp" }));
    }

    void test_mod_list()
    {
        QTemporaryDir dir;
        const auto mods = FS::PathCombine(dir.path(), "mods");
        writeFile(FS::PathCombine(mods, "A/Data/shared.dds"));
        writeFile(FS::PathCombine(mods, "B/Data/shared.dds"));
        writeFile(FS::PathCombine(mods, "Manual/Data/own.esp"));
        const auto file = FS::PathCombine(dir.path(), "mods.json");
        {
            ModList list(mods, file);
            QVERIFY(list.load().has_value());
            QCOMPARE(list.mods().size(), 3);  // folders put in by hand show up, disabled
            QVERIFY(list.enabledMods().isEmpty());
            const int a = list.indexOf("A");
            const int b = list.indexOf("B");
            list.setEnabled({ a, b }, true);
            QCOMPARE(list.overrides(list.mods()[std::max(a, b)].id).size(), 1);
            QCOMPARE(list.uniqueId("A"), QString("A (2)"));
            QCOMPARE(list.uniqueId("Bad:Name?"), QString("Bad-Name-"));
            ModEntry nexus{ .id = "A", .name = "Mod A", .version = "1.0", .nexusGame = "skyrimspecialedition", .nexusModId = 42, .nexusFileId = 7 };
            list.upsert(nexus);
            QVERIFY(list.mods()[list.indexOf("A")].enabled);  // replacing keeps the state
            list.setLatestVersion("A", "1.1");
            QVERIFY(list.mods()[list.indexOf("A")].hasUpdate());
        }
        ModList reloaded(mods, file);
        QVERIFY(reloaded.load().has_value());
        const auto& a = reloaded.mods()[reloaded.indexOf("A")];
        QCOMPARE(a.name, QString("Mod A"));
        QCOMPARE(a.nexusModId, 42);
        QCOMPARE(a.latestVersion, QString("1.1"));
        QVERIFY(a.enabled);
        // reinstalling a Nexus mod's archive from disk replaces it (no Nexus details, a name made from the file name)
        reloaded.upsert(ModEntry{ .id = "A", .name = "Mod A", .nexusModId = 42, .archive = "Mod A-42-1-0-1700000000.7z" });
        QCOMPARE(reloaded.indexOfInstalled(ModEntry{ .name = "Mod A-42-1-0-1700000000" }, "mod a-42-1-0-1700000000.7z"), reloaded.indexOf("A"));
        QCOMPARE(reloaded.indexOfInstalled(ModEntry{ .name = "Mod A", .nexusModId = 42 }, "other.7z"), reloaded.indexOf("A"));
        QCOMPARE(reloaded.indexOfInstalled(ModEntry{ .name = "Mod A" }, "other.7z"), -1);  // a different mod of that name
        QCOMPARE(reloaded.indexOfInstalled(ModEntry{ .name = "Mod A", .nexusModId = 43 }, "Mod A-42-1-0-1700000000.7z"), -1);
        const int before = reloaded.indexOf("Manual");
        reloaded.moveRows({ before }, -before);
        QCOMPARE(reloaded.indexOf("Manual"), 0);
        QVERIFY(reloaded.remove({ reloaded.indexOf("Manual") }).has_value());
        QVERIFY(!QFileInfo::exists(FS::PathCombine(mods, "Manual")));
        QCOMPARE(reloaded.mods().size(), 2);
    }

    void test_ignored_updates()
    {
        QTemporaryDir dir;
        const auto mods = FS::PathCombine(dir.path(), "mods");
        writeFile(FS::PathCombine(mods, "A/Data/a.esp"));
        const auto file = FS::PathCombine(dir.path(), "mods.json");
        {
            ModList list(mods, file);
            QVERIFY(list.load().has_value());
            list.upsert(ModEntry{ .id = "A", .name = "A", .version = "1.0", .nexusGame = "skyrimspecialedition", .nexusModId = 1 });
            list.setLatestVersion("A", "2.0");
            QVERIFY(list.mods()[0].hasUpdate());
            list.setIgnoreUpdates({ 0 }, true);
            QVERIFY(!list.mods()[0].hasUpdate());
            QCOMPARE(list.data(list.index(0, ModList::LatestColumn), Qt::DisplayRole).toString(), QString("2.0 (ignored)"));
            QVERIFY(list.data(list.index(0, ModList::NameColumn), Qt::ToolTipRole).toString().contains("Updates are ignored"));
        }
        ModList reloaded(mods, file);
        QVERIFY(reloaded.load().has_value());
        QVERIFY(reloaded.mods()[0].ignoreUpdates);
        QVERIFY(!reloaded.mods()[0].hasUpdate());
        reloaded.setIgnoreUpdates({ 0 }, false);
        QVERIFY(reloaded.mods()[0].hasUpdate());
    }

    void test_separators()
    {
        QTemporaryDir dir;
        const auto mods = FS::PathCombine(dir.path(), "mods");
        for (const auto* id : { "A", "B", "C", "D" }) {
            writeFile(FS::PathCombine(mods, QString("%1/Data/%1.esp").arg(id)));
        }
        const auto file = FS::PathCombine(dir.path(), "mods.json");
        {
            ModList list(mods, file);
            QVERIFY(list.load().has_value());
            list.setOrder({ "A", "B", "C", "D" });
            list.setEnabled({ 0, 1, 2, 3 }, true);
            // [A] [Group: B C] [D]... with the separator before B and another before D
            QCOMPARE(list.addSeparator("Group", 1), 1);
            QCOMPARE(list.addSeparator("Last", 4), 4);
            QCOMPARE(list.mods().size(), 6);
            QCOMPARE(list.modCount(), 4);
            QCOMPARE(list.enabledMods().size(), 4);
            QCOMPARE(list.groupEnd(1), 3);
            QCOMPARE(list.data(list.index(1, ModList::NameColumn), Qt::DisplayRole).toString(), QString("Group  (2 mod(s))"));
            QCOMPARE(list.data(list.index(1, ModList::NameColumn), Qt::EditRole).toString(), QString("Group"));
            QVERIFY(!(list.flags(list.index(1, ModList::EnabledColumn)) & Qt::ItemIsUserCheckable));
            list.setEnabled({ 1 }, true);  // a separator is never enabled
            QVERIFY(!list.mods()[1].enabled);

            list.setCollapsed(1, true);
            QVERIFY(list.hiddenByCollapse(2));
            QVERIFY(list.hiddenByCollapse(3));
            QVERIFY(!list.hiddenByCollapse(1));
            QVERIFY(!list.hiddenByCollapse(0));
            QVERIFY(!list.hiddenByCollapse(5));
            QCOMPARE(list.withGroups({ 1 }), (QList<int>{ 1, 2, 3 }));

            // a collapsed group moves as a whole: up past A
            list.moveRows({ 1 }, -1);
            QStringList order;
            for (const auto& entry : list.mods()) {
                order << (entry.separator ? "|" + entry.name : entry.id);
            }
            QCOMPARE(order, (QStringList{ "|Group", "B", "C", "A", "|Last", "D" }));
            // a mod with the same name as a separator is not taken for it when installing
            QCOMPARE(list.indexOfInstalled(ModEntry{ .name = "Group" }, "Group.7z"), -1);
        }
        ModList reloaded(mods, file);
        QVERIFY(reloaded.load().has_value());
        QCOMPARE(reloaded.mods().size(), 6);  // no folder for the separators, and none taken for a mod
        QVERIFY(reloaded.mods()[0].separator);
        QVERIFY(reloaded.mods()[0].collapsed);
        QCOMPARE(reloaded.mods()[0].name, QString("Group"));
        QVERIFY(reloaded.mods()[0].id.contains('|'));
        // removing a separator leaves its mods
        QVERIFY(reloaded.remove({ 0 }).has_value());
        QCOMPARE(reloaded.modCount(), 4);
        QCOMPARE(reloaded.mods().size(), 5);
        QVERIFY(QFileInfo::exists(FS::PathCombine(mods, "B")));
    }

    void test_hidden_files()
    {
        QTemporaryDir dir;
        const auto mods = FS::PathCombine(dir.path(), "mods");
        writeFile(FS::PathCombine(mods, "A/Data/shared.dds"));
        writeFile(FS::PathCombine(mods, "A/Data/a.esp"));
        writeFile(FS::PathCombine(mods, "B/Data/Shared.dds"));
        writeFile(FS::PathCombine(mods, "C/Data/shared.dds"));
        ModList list(mods, FS::PathCombine(dir.path(), "mods.json"));
        QVERIFY(list.load().has_value());
        list.setOrder({ "A", "B", "C" });
        list.setEnabled({ 0, 1 }, true);  // C stays disabled
        QCOMPARE(list.overrides("B"), QStringList{ "A" });
        const auto byKey = [](const QList<ModList::ModFile>& files) {
            QMap<QString, ModList::ModFile> out;
            for (const auto& file : files) {
                out.insert(file.mod + ":" + file.path.toLower(), file);
            }
            return out;
        };
        auto all = byKey(list.files());
        QCOMPARE(all.size(), 3);  // the enabled mods' files
        QVERIFY(all["B:data/shared.dds"].wins);
        QCOMPARE(all["B:data/shared.dds"].others, QStringList{ "A" });
        QVERIFY(!all["A:data/shared.dds"].wins);
        QCOMPARE(all["A:data/shared.dds"].others, QStringList{ "B" });
        QVERIFY(all["A:data/a.esp"].wins);
        QVERIFY(all["A:data/a.esp"].others.isEmpty());
        // a disabled mod's files when asked for: never winning, with the enabled mods that have them too
        const auto disabled = list.files({ "C" });
        QCOMPARE(disabled.size(), 1);
        QVERIFY(!disabled[0].wins);
        QCOMPARE(disabled[0].others, (QStringList{ "A", "B" }));

        QSignalSpy redeploy(&list, &ModList::deploymentChanged);
        QVERIFY(list.setFilesHidden({ { "B", "Data/Shared.dds" } }, true).has_value());
        QCOMPARE(redeploy.size(), 1);
        QVERIFY(QFileInfo::exists(FS::PathCombine(mods, "B/Data/Shared.dds.mohidden")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(mods, "B/Data/Shared.dds")));
        // a hidden file is in no conflict
        QVERIFY(list.overrides("B").isEmpty());
        QVERIFY(list.overriddenBy("A").isEmpty());
        QVERIFY(list.overwrites().isEmpty());
        QVERIFY(ModList::sharingPairs(mods, { "A", "B" }).isEmpty());
        all = byKey(list.files());
        QVERIFY(all["B:data/shared.dds"].hidden);
        QVERIFY(!all["B:data/shared.dds"].wins);
        QCOMPARE(all["B:data/shared.dds"].others, QStringList{ "A" });
        QVERIFY(all["A:data/shared.dds"].wins);
        QVERIFY(all["A:data/shared.dds"].others.isEmpty());

        // hiding it again changes nothing
        QVERIFY(list.setFilesHidden({ { "B", "Data/Shared.dds" } }, true).has_value());
        QCOMPARE(redeploy.size(), 1);
        // a file in the way of showing it again is reported, and both stay as they are
        writeFile(FS::PathCombine(mods, "B/Data/Shared.dds"), "new");
        const auto blocked = list.setFilesHidden({ { "B", "Data/Shared.dds" } }, false);
        QVERIFY(!blocked.has_value());
        QVERIFY(blocked.error().contains("in the way"));
        QCOMPARE(redeploy.size(), 1);
        QVERIFY(QFileInfo::exists(FS::PathCombine(mods, "B/Data/Shared.dds.mohidden")));
        QVERIFY(QFile::remove(FS::PathCombine(mods, "B/Data/Shared.dds")));
        QVERIFY(list.setFilesHidden({ { "B", "Data/Shared.dds" } }, false).has_value());
        QCOMPARE(redeploy.size(), 2);
        QVERIFY(QFileInfo::exists(FS::PathCombine(mods, "B/Data/Shared.dds")));
        QCOMPARE(list.overrides("B"), QStringList{ "A" });

        // both copies hidden: the two hidden files are in no conflict with each other either
        QVERIFY(list.setFilesHidden({ { "A", "Data/shared.dds" }, { "B", "Data/Shared.dds" } }, true).has_value());
        QCOMPARE(redeploy.size(), 3);
        QVERIFY(list.overrides("B").isEmpty());
        QVERIFY(list.overwrites().isEmpty());
        QVERIFY(ModList::sharingPairs(mods, { "A", "B" }).isEmpty());
    }
};

QTEST_GUILESS_MAIN(LoadOrderTest)

#include "LoadOrder_test.moc"
