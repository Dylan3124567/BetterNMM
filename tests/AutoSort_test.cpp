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

// The auto sort is a port of LOOT's sorting (libloot 0.29, esplugin, loot-condition-interpreter). Where those projects
// have tests with known answers, the same cases are checked here; testdata/AutoSort/testing-plugins holds LOOT's own
// test plugins (MIT, see its LICENSE).

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <FileSystem.h>
#include <game/AutoSort.h>
#include <game/ModList.h>
#include <game/PluginList.h>
#include <game/loot/Plugin.h>
#include <game/loot/Sorter.h>
#include <game/loot/Yaml.h>

using Loot::GameType;
using Loot::YamlNode;

namespace {

QString testingPlugins(const QString& game)
{
    return QFINDTESTDATA("testdata/AutoSort/testing-plugins/" + game + "/Data");
}

void writeFile(const QString& path, const QByteArray& content)
{
    QVERIFY(FS::ensureFilePathExists(path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(content), content.size());
}

void copyInto(const QString& from, const QString& folder, const QString& name = {})
{
    const auto target = FS::PathCombine(folder, name.isEmpty() ? QFileInfo(from).fileName() : name);
    QVERIFY(FS::ensureFilePathExists(target));
    QVERIFY2(QFile::copy(from, target), qPrintable(from));
}

QByteArray le32(quint32 value)
{
    QByteArray out;
    for (int i = 0; i < 4; i++) {
        out.append(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
    return out;
}

QByteArray subrecord(const char* type, const QByteArray& data)
{
    return QByteArray(type, 4) + static_cast<char>(data.size() & 0xFF) + static_cast<char>((data.size() >> 8) & 0xFF) + data;
}

QByteArray record(const char* type, quint32 formId, const QByteArray& payload, quint32 flags = 0)
{
    return QByteArray(type, 4) + le32(payload.size()) + le32(flags) + le32(formId) + QByteArray(8, '\0') + payload;
}

QByteArray group(const QByteArray& contents)
{
    return QByteArray("GRUP") + le32(contents.size() + 24) + QByteArray("WEAP") + QByteArray(12, '\0') + contents;
}

/// A 24-byte-header plugin (Skyrim and later) with the given header and records
QByteArray pluginBytes(quint32 flags, const QStringList& masters, const QString& description, const QList<quint32>& formIds)
{
    QByteArray header = subrecord("HEDR", QByteArray("\x9a\x99\xd9\x3f", 4) + le32(formIds.size()) + le32(0x800));
    if (!description.isNull()) {
        header += subrecord("SNAM", description.toLatin1() + '\0');
    }
    for (const auto& master : masters) {
        header += subrecord("MAST", master.toLatin1() + '\0') + subrecord("DATA", QByteArray(8, '\0'));
    }
    QByteArray records;
    for (const auto formId : formIds) {
        records += record("WEAP", formId, subrecord("EDID", "x"));
    }
    return record("TES4", 0, header, flags) + (formIds.isEmpty() ? QByteArray() : group(records));
}

/// A plugin as the sorter sees it, built in memory like libloot's TestPlugin
Loot::Plugin testPlugin(const QString& name, bool master = false, const QStringList& masters = {})
{
    Loot::Plugin plugin;
    plugin.name = name;
    plugin.master = master;
    plugin.header.masters = masters;
    return plugin;
}

/// libloot's test fixture groups: A, B after A, C after B, default after C, E after default, F after E
QList<Loot::Group> fixtureGroups()
{
    return { { "A", {} }, { "B", { "A" } }, { "C", { "B" } }, { "default", { "C" } }, { "E", { "default" } }, { "F", { "E" } } };
}

Result<QStringList> sortTest(const std::vector<std::pair<const Loot::Plugin*, int>>& plugins,
                             const std::function<void(Loot::SortingPlugin&)>& edit = {}, const QStringList& early = {})
{
    std::vector<Loot::SortingPlugin> input;
    for (const auto& [plugin, index] : plugins) {
        Loot::SortingPlugin sorting;
        sorting.plugin = plugin;
        sorting.loadOrderIndex = index;
        if (edit) {
            edit(sorting);
        }
        input.push_back(sorting);
    }
    return Loot::sortPlugins(input, fixtureGroups(), early);
}

/// libloot's generated test masterlist (cpp/src/tests/api/interface/api_game_operations_test.h), sorting parts
const char* const libloot_test_masterlist = R"(bash_tags:
  - Actors.ACBS
  - C.Climate
globals:
  - type: say
    content: 'A general masterlist message.'
    condition: 'file("Blank.missing.esp")'
groups:
  - name: group1
  - name: group2
    after:
      - group1
plugins:
  - name: Blank.esm
    after:
      - master.esm
    msg:
      - type: say
        content: 'Do not clean ITM records, they are intentional and required for the mod to function.'
        condition: 'file("Blank.missing.esp")'
    tag:
      - Actors.ACBS
      - Actors.AIData
      - '-C.Water'
  - name: Blank - Different.esm
    after:
      - Blank - Master Dependent.esm
    msg:
      - type: warn
        content: 'Check you are using v2+. If not, Update. v1 has a severe bug with the Mystic Emporium disappearing.'
    dirty:
      - crc: 0x7d22f9df
        util: TES4Edit
        udr: 4
  - name: Blank - Different.esp
    after:
      - Blank - Plugin Dependent.esp
    msg:
      - type: error
        content: 'Obsolete. Remove this and install Enhanced Weather.'
  - name: Blank.esp
    after:
      - Blank - Different Master Dependent.esp
      - Blank - Override.esp
  - name: Blank - Different Master Dependent.esp
    after:
      - Blank - Master Dependent.esp
    msg:
      - type: say
        content: 'Do not clean ITM records, they are intentional and required for the mod to function.'
      - type: warn
        content: 'Check you are using v2+. If not, Update. v1 has a severe bug with the Mystic Emporium disappearing.'
      - type: error
        content: 'Obsolete. Remove this and install Enhanced Weather.'
)";

}  // namespace

class AutoSortTest : public QObject {
    Q_OBJECT

   private slots:
    ////// YAML //////

    void test_yaml_constructs()
    {
        const auto document = Loot::parseYaml(R"(# a comment
common:
  - &note
    type: say
    content: 'It''s
      folded

      twice'
  - &tag Relev
groups:
- name: &main Main Plugins
  after: [ default, 'Early' ]   # flow, quoted
plugins:
  - name: 'A.esp'
    group: *main
    after: [ *tag, { name: B.esp, condition: 'file("C.esp") and
        not active("D.esp")' } ]
    msg:
      - <<: *note
        type: warn
      - <<: [ *note ]
        extra: "tab\there"
    req:
      - -C.Climate
    url: [ https://example.com/a:b ]
    detail: |
      line one
      line two
    folded: >-
      one
      two
empty:
nothing: ~
)");
        QVERIFY2(document, qPrintable(document ? QString() : document.error()));
        const auto* plugins = document->get(u"plugins");
        QVERIFY(plugins && plugins->kind == YamlNode::Kind::Sequence && plugins->items.size() == 1);
        const auto& a = plugins->items[0];
        QCOMPARE(a.get(u"name")->scalar, "A.esp");
        QCOMPARE(a.get(u"group")->scalar, "Main Plugins");
        const auto* after = a.get(u"after");
        QCOMPARE(after->items.size(), size_t(2));
        QCOMPARE(after->items[0].scalar, "Relev");
        QCOMPARE(after->items[1].get(u"name")->scalar, "B.esp");
        QCOMPARE(after->items[1].get(u"condition")->scalar, "file(\"C.esp\") and not active(\"D.esp\")");
        const auto* msg = a.get(u"msg");
        QCOMPARE(msg->items[0].get(u"type")->scalar, "warn");  // the mapping's own key wins over the merged one
        QCOMPARE(msg->items[0].get(u"content")->scalar, "It's folded\ntwice");
        QVERIFY(!msg->items[0].get(u"<<"));
        QCOMPARE(msg->items[1].get(u"type")->scalar, "say");
        QCOMPARE(msg->items[1].get(u"extra")->scalar, "tab\there");
        QCOMPARE(a.get(u"req")->items[0].scalar, "-C.Climate");
        QCOMPARE(a.get(u"url")->items[0].scalar, "https://example.com/a:b");
        QCOMPARE(a.get(u"detail")->scalar, "line one\nline two\n");
        QCOMPARE(a.get(u"folded")->scalar, "one two");
        const auto* groups = document->get(u"groups");
        QCOMPARE(groups->items[0].get(u"after")->items[1].scalar, "Early");
        QCOMPARE(document->get(u"empty")->kind, YamlNode::Kind::Null);
        QCOMPARE(document->get(u"nothing")->kind, YamlNode::Kind::Null);
    }

    void test_yaml_errors_name_the_line()
    {
        const auto unterminated = Loot::parseYaml("a: 'open\nb: c\n");
        QVERIFY(!unterminated);
        QVERIFY2(unterminated.error().contains("line 1"), qPrintable(unterminated.error()));
        const auto alias = Loot::parseYaml("a: x\nb: *missing\n");
        QVERIFY(!alias);
        QVERIFY2(alias.error().contains("line 2") && alias.error().contains("missing"), qPrintable(alias.error()));
        QVERIFY(!Loot::parseYaml("a:\n  - b\n c: d\n"));
        QVERIFY(!Loot::parseYaml("- &x scalar\n- <<: *x\n"));  // merging something that is not a map
    }

    ////// Masterlist //////

    void test_prelude_replacement_data()
    {
        QTest::addColumn<QString>("masterlist");
        QTest::addColumn<QString>("prelude");
        QTest::addColumn<QString>("expected");
        // libloot's replace_prelude tests
        const QString prelude = "globals:\n  - type: note\n    content: A message.\n";
        const QString preludeNoBreak = "globals:\n  - type: note\n    content: A message.";
        QTest::newRow("empty") << "" << "" << "";
        QTest::newRow("no prelude") << "plugins:\n  - name: a.esp\n" << prelude << "plugins:\n  - name: a.esp\n";
        QTest::newRow("flow style") << "{prelude: {}, plugins: [{name: a.esp}]}" << "globals: [{type: note, content: A message.}]"
                                    << "{prelude: {}, plugins: [{name: a.esp}]}";
        QTest::newRow("at the start") << "prelude:\n  a: b\n\nplugins:\n  - name: a.esp\n" << prelude
                                      << "prelude:\n  globals:\n    - type: note\n      content: A message.\n\nplugins:\n  - name: a.esp\n";
        QTest::newRow("no line break") << "prelude:\n  a: b\nplugins:\n  - name: a.esp\n" << prelude
                                       << "prelude:\n  globals:\n    - type: note\n      content: A message.\n\nplugins:\n  - name: a.esp\n";
        QTest::newRow("prelude without trailing break")
            << "prelude:\n  a: b\n\nplugins:\n  - name: a.esp\n" << preludeNoBreak
            << "prelude:\n  globals:\n    - type: note\n      content: A message.\nplugins:\n  - name: a.esp\n";
        QTest::newRow("both without breaks") << "prelude:\n  a: b\nplugins:\n  - name: a.esp\n" << preludeNoBreak
                                             << "prelude:\n  globals:\n    - type: note\n      content: A message.\nplugins:\n  - name: a.esp\n";
        QTest::newRow("at the end") << "plugins:\n  - name: a.esp\nprelude:\n  a: b\n\n" << prelude
                                    << "plugins:\n  - name: a.esp\nprelude:\n  globals:\n    - type: note\n      content: A message.\n";
        QTest::newRow("only the prelude") << "\ncommon:\n  key: value\nprelude:\n  a: b\nplugins:\n  - name: a.esp\n"
                                          << "\n\nglobals:\n  - type: note\n    content: A message.\n\n"
                                          << "\ncommon:\n  key: value\nprelude:\n\n\n  globals:\n    - type: note\n      content: A "
                                             "message.\n\n\nplugins:\n  - name: a.esp\n";
    }

    void test_prelude_replacement()
    {
        QFETCH(QString, masterlist);
        QFETCH(QString, prelude);
        QFETCH(QString, expected);
        QCOMPARE(Loot::Masterlist::replacePrelude(masterlist, prelude), expected);
    }

    void test_masterlist_lookup()
    {
        const auto masterlist = Loot::Masterlist::load(R"(prelude:
  common: &ussep Unofficial Skyrim Special Edition Patch.esp
groups:
  - name: Early
  - name: Late
    after: [ Early ]
plugins:
  - name: 'Exact.esp'
    after: [ *ussep, { name: 'Other.esp', condition: 'file("Other.esp")' } ]
  - name: 'Ex.*\.esp'
    group: Late
    after: [ Regex.esp ]
    req: [ Needed.esp ]
  - name: 'Grouped.esp'
    group: Early
  - name: 'Grou.*'
    group: Late
)",
                                                       "common:\n  - &ussep 'Unofficial Skyrim Special Edition Patch.esp'\n");
        QVERIFY2(masterlist, qPrintable(masterlist ? QString() : masterlist.error()));
        QCOMPARE(masterlist->groups().size(), 3);
        QCOMPARE(masterlist->groups().first().name, "default");  // added when the masterlist has none

        const auto exact = masterlist->find("EXACT.ESP");  // names compare without case
        QCOMPARE(exact.group, std::optional<QString>("Late"));  // from the regex entry: the exact entry has none
        QCOMPARE(exact.after.size(), size_t(3));
        QCOMPARE(exact.after[0].name, "Unofficial Skyrim Special Edition Patch.esp");
        QCOMPARE(exact.after[1].name, "Other.esp");
        QVERIFY(exact.after[1].condition.has_value());
        QCOMPARE(exact.after[2].name, "Regex.esp");
        QCOMPARE(exact.requirements.size(), size_t(1));

        QCOMPARE(masterlist->find("Grouped.esp").group, std::optional<QString>("Early"));  // its own group stays
        QCOMPARE(masterlist->find("Example.esp").group, std::optional<QString>("Late"));
        QVERIFY(!masterlist->find("Exact.esm").group);   // the regex matches whole names only
        QVERIFY(!masterlist->find("Unknown.esp").group);
    }

    void test_masterlist_errors()
    {
        QVERIFY(!Loot::Masterlist::load("plugins:\n  - name: a.esp\n  - name: A.ESP\n", ""));  // the same plugin twice
        QVERIFY(!Loot::Masterlist::load("groups:\n  - name: a\n  - name: a\n", ""));
        QVERIFY(!Loot::Masterlist::load("plugins:\n  - name: 'broken[*.esp'\n", ""));  // a broken regex
        const auto badCondition = Loot::Masterlist::load("plugins:\n  - name: a.esp\n    after:\n      - name: b.esp\n        condition: 'fle(\"x\")'\n", "");
        QVERIFY(!badCondition);
        QVERIFY2(badCondition.error().contains("fle"), qPrintable(badCondition.error()));
    }

    ////// Conditions //////

    void test_version_comparisons()
    {
        // loot-condition-interpreter's Version tests
        struct Row {
            const char* left;
            const char* op;
            const char* right;
        };
        const Row rows[] = {
            { "", "==", "" },
            { "", "==", "0" },
            { "0", "==", "" },
            { "", "!=", "5" },
            { "5", "!=", "" },
            { "", "<", "1" },
            { "1", ">", "" },
            { "5", "==", "5" },
            { "5", "==", "5" },
            { "4", "!=", "5" },
            { "5", "!=", "4" },
            { "4", "<", "5" },
            { "5", ">", "4" },
            { "5", "!=", "10" },
            { "10", "!=", "5" },
            { "5", "<", "10" },
            { "10", ">", "5" },
            { "0.0.5", "==", "0.0.5" },
            { "0.0.5", "!=", "0.0.10" },
            { "0.0.10", "!=", "0.0.5" },
            { "0.0.5", "<", "0.0.10" },
            { "0.0.10", ">", "0.0.5" },
            { "0.5.0", "==", "0.5.0" },
            { "0.5.0", "!=", "0.10.0" },
            { "0.10.0", "!=", "0.5.0" },
            { "0.5.0", "<", "0.10.0" },
            { "0.10.0", ">", "0.5.0" },
            { "0.5.10", "<", "0.10.5" },
            { "0.10.5", ">", "0.5.10" },
            { "5.0.0", "==", "5.0.0" },
            { "5.0.0", "!=", "10.0.0" },
            { "10.0.0", "!=", "5.0.0" },
            { "5.0.0", "<", "10.0.0" },
            { "10.0.0", ">", "5.0.0" },
            { "5.10.0", "<", "10.5.0" },
            { "10.5.0", ">", "5.10.0" },
            { "5.0.10", "<", "10.0.5" },
            { "10.0.5", ">", "5.0.10" },
            { "1.0.0", "!=", "1.0.0-alpha" },
            { "1.0.0-alpha", "<", "1.0.0" },
            { "1.0.0", ">", "1.0.0-alpha" },
            { "0.0.5-5.alpha", "==", "0.0.5-5.alpha" },
            { "0.0.5-5.alpha", "!=", "0.0.5-10.beta" },
            { "0.0.5-10.beta", "!=", "0.0.5-5.alpha" },
            { "0.0.5-5", "<", "0.0.5-10" },
            { "0.0.5-10", ">", "0.0.5-5" },
            { "0.0.5-a", "<", "0.0.5-b" },
            { "0.0.5-b", ">", "0.0.5-a" },
            { "0.0.5-9", "<", "0.0.5-a" },
            { "0.0.5-a", ">", "0.0.5-9" },
            { "0.0.5-86", "<", "0.0.5-78b" },
            { "0.0.5-78b", ">", "0.0.5-86" },
            { "0.0.5-5.10", "<", "0.0.5-10.5" },
            { "0.0.5-10.5", ">", "0.0.5-5.10" },
            { "0.0.5-5", "<", "0.0.5-5.0" },
            { "0.0.5-5.0", ">", "0.0.5-5" },
            { "0.0.5-10", "<", "0.0.10-5" },
            { "0.0.10-5", ">", "0.0.5-10" },
            { "0.0.1+alpha", "==", "0.0.1+beta" },
            { "05.0.0", "==", "5.0.0" },
            { "5.0.0", "==", "05.0.0" },
            { "0.05.0", "==", "0.5.0" },
            { "0.5.0", "==", "0.05.0" },
            { "0.0.05", "==", "0.0.5" },
            { "0.0.5", "==", "0.0.05" },
            { "0.0.5-05", "==", "0.0.5-5" },
            { "0.0.5-5", "==", "0.0.5-05" },
            { "1.0.0.1.0.0", "==", "1.0.0.1.0.0" },
            { "1.0.0.0.0.0", "!=", "1.0.0.0.0.1" },
            { "1.0.0.0.0.1", "!=", "1.0.0.0.0.0" },
            { "1.0.0.0.0.0", "<", "1.0.0.0.0.1" },
            { "1.0.0.0.0.1", ">", "1.0.0.0.0.0" },
            { "1.0.0a", "==", "1.0.0a" },
            { "1.0.0a", "!=", "1.0.0b" },
            { "1.0.0b", "!=", "1.0.0a" },
            { "1.0.0a", "<", "1.0.0b" },
            { "1.0.0b", ">", "1.0.0a" },
            { "0.78b", "<", "0.86" },
            { "0.86", ">", "0.78b" },
            { "1.0.0", "<", "1.0.0a" },
            { "1.0.0a", ">", "1.0.0" },
            { "1.0.0alpha.2", "<", "1.0.0beta.2" },
            { "1.0.0beta.2", ">", "1.0.0alpha.2" },
            { "1.0.0A", "==", "1.0.0a" },
            { "1.0.0a", "==", "1.0.0A" },
            { "1.0.0a", "<", "1.0.0B" },
            { "1.0.0B", ">", "1.0.0a" },
            { "1.0.0-Alpha", "==", "1.0.0-alpha" },
            { "1.0.0-alpha", "==", "1.0.0-Alpha" },
            { "1.0.0-alpha", "<", "1.0.0-Beta" },
            { "1.0.0-Beta", ">", "1.0.0-alpha" },
            { "1-beta", "==", "1.0.0-beta" },
            { "1.0.0-beta", "==", "1-beta" },
            { "0.0.0.1", "==", "0.0.0.1.0.0" },
            { "0.0.0.1.0.0", "==", "0.0.0.1" },
            { "1.0.0.0", "!=", "1.0.0.0.0.1" },
            { "1.0.0.0.0.1", "!=", "1.0.0.0" },
            { "1.0.0.0.0.0", "<", "1.0.0.1" },
            { "1.0.0.1", ">", "1.0.0.0.0.0" },
            { "1.0.0.0", "<", "1.0.0.0.0.1" },
            { "1.0.0.0.0.1", ">", "1.0.0.0" },
        };
        int checked = 0;
        for (const auto& row : rows) {
            const Loot::Version left(QString::fromUtf8(row.left));
            const Loot::Version right(QString::fromUtf8(row.right));
            const QByteArray op(row.op);
            bool holds = false;
            if (op == "==") {
                holds = left == right && left.compare(right) == 0;
            } else if (op == "!=") {
                holds = !(left == right);
            } else if (op == "<") {
                holds = left.compare(right) < 0;
            } else if (op == ">") {
                holds = left.compare(right) > 0;
            }
            QVERIFY2(holds, qPrintable(QString("%1 %2 %3").arg(row.left, row.op, row.right)));
            checked++;
        }
        QCOMPARE(checked, 101);
        QVERIFY(Loot::Version("0, 2, 0, 12") == Loot::Version("0.2.0.12"));  // the comma-and-space special case
        QVERIFY(Loot::Version("1.2.3 alpha") == Loot::Version("1.2.3-alpha"));
        QVERIFY(Loot::Version("1.A").compare(Loot::Version("1.1")) > 0);
        QVERIFY(Loot::Version("1.1A").compare(Loot::Version("1.1")) > 0);
        QVERIFY(Loot::Version("1.2").compare(Loot::Version("1.1A")) > 0);
    }

    void test_conditions()
    {
        QTemporaryDir dir;
        const auto data = FS::PathCombine(dir.path(), "Data");
        writeFile(FS::PathCombine(data, "Master.esm"), pluginBytes(0x1, {}, "Version: 2.1.3b", {}));
        writeFile(FS::PathCombine(data, "Flagless.esm"), pluginBytes(0, {}, "", {}));
        writeFile(FS::PathCombine(data, "Plugin.esp"), pluginBytes(0, { "Master.esm" }, "Adds things. v1.0", {}));
        writeFile(FS::PathCombine(data, "Plugin2.esp"), pluginBytes(0, {}, QString(), {}));
        writeFile(FS::PathCombine(data, "Ghosted.esp.ghost"), pluginBytes(0, {}, QString(), {}));
        writeFile(FS::PathCombine(data, "SKSE/Plugins/engine-1-6-1170-0.dll"), "x");
        writeFile(FS::PathCombine(data, "SKSE/Plugins/other.dll"), "x");
        writeFile(FS::PathCombine(data, "SKSE/Plugins/engine-1.6.1170.dll"), "x");
        writeFile(FS::PathCombine(dir.path(), "Game.exe"), "MZ not really");

        Loot::ConditionState state;
        state.game = GameType::SkyrimSE;
        state.dataPath = data;
        state.activePlugins = { "master.esm", "plugin.esp", "plugin2.esp" };
        state.pluginVersions = { { "master.esm", "2.1.3b" }, { "plugin.esp", "1.0" } };
        auto eval = [&state](const QString& text) {
            const auto condition = Loot::Condition::parse(text);
            if (!condition) {
                qWarning() << condition.error();
                return std::optional<bool>();
            }
            const auto result = condition->eval(state);
            return result ? std::optional<bool>(*result) : std::optional<bool>();
        };
        const auto masterCrc = Loot::fileCrc32(FS::PathCombine(data, "Master.esm"));
        QVERIFY(masterCrc);
        const QList<std::pair<QString, bool>> cases{
            { R"(file("Master.esm"))", true },
            { R"(file("Missing.esp"))", false },
            { R"(file("Ghosted.esp"))", true },  // a ghosted plugin counts as installed
            { R"(file("SKSE/Plugins/engine-1-6-\d+-0\.dll"))", true },
            { R"(file("SKSE/Plugins/engine-1-5-\d+-0\.dll"))", false },
            { R"(file("Plug.*\.esp"))", true },
            { R"(many("Plug.*\.esp"))", true },
            { R"(many("Master\.es[mp]"))", false },
            { R"(file("../Game.exe"))", true },
            { R"(active("Plugin.esp"))", true },
            { R"(active("PLUGIN.ESP"))", true },
            { R"(active("Flagless.esm"))", false },
            { R"(active("Plugin\d?\.esp"))", true },
            { R"(many_active("Plugin\d?\.esp"))", true },
            { R"(many_active("Master.*"))", false },
            { R"(is_master("Master.esm"))", true },
            { R"(is_master("Flagless.esm"))", true },  // the .esm extension is enough in Skyrim SE
            { R"(is_master("Plugin.esp"))", false },
            { R"(is_master("Missing.esm"))", false },
            { QString(R"(checksum("Master.esm", %1))").arg(*masterCrc, 0, 16), true },
            { QString(R"(checksum("Master.esm", %1))").arg(*masterCrc ^ 1, 0, 16), false },
            { R"(checksum("Missing.esm", DEADBEEF))", false },
            { R"(version("Master.esm", ">=", "2.1.3"))", false },  // "version" needs its comparator unquoted
            { R"(version("Master.esm", >=, "2.1.3"))", true },
            { R"(version("Master.esm", "2.1.3", >))", true },  // the older argument order
            { R"(version("Master.esm", <, "2.1.3"))", false },
            { R"(version("Plugin2.esp", ==, "0"))", false },  // no version in its description
            { R"(version("Missing.esp", <, "9"))", false },
            { R"(file_size("Plugin2.esp", 0))", false },
            { R"(readable("Master.esm"))", true },
            { R"(readable("Missing.esm"))", false },
            { R"(description_contains("Plugin.esp", "adds THINGS"))", true },
            { R"(description_contains("Plugin.esp", "^things"))", false },
            { R"(filename_version("SKSE/Plugins/engine-(\d+\.\d+\.\d+)\.dll", >=, "1.6.640"))", true },
            { R"(filename_version("SKSE/Plugins/engine-(\d+\.\d+\.\d+)\.dll", <, "1.6.640"))", false },
            // "1-6-1170" reads as release 1 with pre-release 6.1170, which is lower than 1.6.640
            { R"(filename_version("SKSE/Plugins/engine-(\d+-\d+-\d+)-0\.dll", <, "1.6.640"))", true },
            { R"(is_executable("../Game.exe"))", false },
            { R"(not file("Missing.esp"))", true },
            { R"(file("Missing.esp") or file("Master.esm") and not active("Flagless.esm"))", true },
            { R"(not (file("Missing.esp") or file("Master.esm")))", false },
            { R"(file("Master.esm") and (file("Missing.esp") or active("Plugin.esp")))", true },
            { "file(\"Master.esm\")\n  and\tnot file(\"Missing.esp\")", true },
        };
        for (const auto& [text, expected] : cases) {
            const auto result = eval(text);
            QVERIFY2(result.has_value() || text.contains("\">=\""), qPrintable(text));
            if (text.contains("\">=\"")) {
                QVERIFY2(!result.has_value(), qPrintable(text));  // a parse error
                continue;
            }
            QVERIFY2(*result == expected, qPrintable(text));
        }
        auto fileSize = QFileInfo(FS::PathCombine(data, "Plugin2.esp")).size();
        QCOMPARE(eval(QString(R"(file_size("Plugin2.esp", %1))").arg(fileSize)), std::optional<bool>(true));

        // parse errors name the condition
        for (const auto& bad : { R"(file("x.esp"))", R"(nothing("x"))", R"(file("x.esp") and)", R"(many("dir/"))",
                                 R"x(filename_version("a(b)(c)", ==, "1"))x", R"(checksum("x", 1FFFFFFFF))" }) {
            const bool valid = QString(bad) == R"(file("x.esp"))";
            QCOMPARE(Loot::Condition::parse(bad).has_value(), valid);
        }
    }

#ifdef Q_OS_WIN
    /// The executable version reader against what Windows itself reads from the same files
    void test_executable_versions_match_windows()
    {
        int compared = 0;
        // the Qt libraries this test runs with; their version resources are exactly as long as they say
        for (const auto* module : { L"Qt6Core.dll", L"Qt6Test.dll" }) {
            wchar_t modulePath[MAX_PATH];
            QVERIFY(GetModuleFileNameW(GetModuleHandleW(module), modulePath, MAX_PATH) > 0);
            const auto path = QDir::fromNativeSeparators(QString::fromWCharArray(modulePath));
            const auto wide = QDir::toNativeSeparators(path).toStdWString();
            DWORD handle = 0;
            const DWORD size = GetFileVersionInfoSizeW(wide.c_str(), &handle);
            if (size == 0) {
                continue;
            }
            std::vector<char> buffer(size);
            QVERIFY(GetFileVersionInfoW(wide.c_str(), 0, size, buffer.data()));
            VS_FIXEDFILEINFO* fixed = nullptr;
            UINT length = 0;
            QVERIFY(VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&fixed), &length));
            const Loot::Version windowsFileVersion(HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS), HIWORD(fixed->dwFileVersionLS),
                                                   LOWORD(fixed->dwFileVersionLS));
            const auto fileVersion = Loot::readFileVersion(path);
            QVERIFY2(fileVersion && fileVersion->has_value(), qPrintable(path + (fileVersion ? QString() : fileVersion.error())));
            QVERIFY2(**fileVersion == windowsFileVersion, qPrintable(path));

            struct Translation {
                WORD language;
                WORD codePage;
            }* translations = nullptr;
            QVERIFY(VerQueryValueW(buffer.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &length));
            const auto key = QString("\\StringFileInfo\\%1%2\\ProductVersion").arg(translations[0].language, 4, 16, QChar('0')).arg(translations[0].codePage, 4, 16, QChar('0'));
            wchar_t* product = nullptr;
            QVERIFY(VerQueryValueW(buffer.data(), reinterpret_cast<const wchar_t*>(key.utf16()), reinterpret_cast<void**>(&product), &length));
            const auto productVersion = Loot::readProductVersion(path);
            QVERIFY2(productVersion && productVersion->has_value(), qPrintable(path + (productVersion ? QString() : productVersion.error())));
            QVERIFY2(**productVersion == Loot::Version(QString::fromWCharArray(product)), qPrintable(path + " " + QString::fromWCharArray(product)));
            compared++;
        }
        QCOMPARE(compared, 2);
        // Windows' own DLLs pad the resource past the length it states; loot-condition-interpreter refuses those, and so
        // does this port
        const auto padded = Loot::readFileVersion(QDir(qEnvironmentVariable("SystemRoot")).filePath("System32/kernel32.dll"));
        QVERIFY(!padded);
        QVERIFY2(padded.error().contains("Unexpected length of VS_VERSIONINFO"), qPrintable(padded.error()));
        const auto notExecutable = Loot::readFileVersion(QFINDTESTDATA("testdata/AutoSort/testing-plugins/LICENSE"));
        QVERIFY(!notExecutable);
        QVERIFY(notExecutable.error().contains("Unknown file magic"));
    }
#endif

    ////// Plugins //////

    void test_extract_version_data()
    {
        QTest::addColumn<QString>("description");
        QTest::addColumn<QString>("version");
        // libloot's extract_version tests
        QTest::newRow("single digit") << "5" << "5";
        QTest::newRow("multiple digits") << "10" << "10";
        QTest::newRow("multiple numbers") << "10.11.12.13" << "10.11.12.13";
        QTest::newRow("semantic") << "1.0.0-x.7.z.92+exp.sha.5114f85" << "1.0.0-x.7.z.92";
        QTest::newRow("stops at a space") << "01.0.0_alpha:1-2 3" << "01.0.0_alpha:1-2";
        QTest::newRow("substring") << "v5.0" << "5.0";
        QTest::newRow("none") << "The quick brown fox jumped over the lazy dog." << QString();
        QTest::newRow("timestamp") << "Updated: 10/09/2016 13:15:18\r\n\r\nRecords Changed: 43" << "10/09/2016 13:15:18";
        QTest::newRow("trailing period") << "Version 0.2." << "0.2";
        QTest::newRow("after text and version colon") << "Legendary Edition\r\n\r\nVersion: 3.0.0" << "3.0.0";
        QTest::newRow("numbers with commas") << "fixing over 2,300 bugs so far! Version: 3.5.3" << "3.5.3";
        QTest::newRow("before text") << "Version: 2.1 The Unofficial Fallout 3 Patch" << "2.1";
        QTest::newRow("preceding v") << "V2.11\r\n\r\n{{BASH:Invent}}" << "2.11";
        QTest::newRow("colon period") << "Version:. 1.09" << "1.09";
        QTest::newRow("letters after numbers") << "comprehensive bugfixing mod for The Elder Scrolls V: Skyrim\r\n\r\nVersion: 2.1.3b\r\n\r\n" << "2.1.3b";
        QTest::newRow("no identifier") << "SkyUI 5.1" << "5.1";
        QTest::newRow("single digit in a sentence") << "Adds 8 variants of Triss Merigold's outfit from \"The Witcher 2\"" << QString();
        QTest::newRow("prefers the version prefix")
            << "Requires Skyrim patch 1.9.32.0.8 or greater.\nRequires Unofficial Skyrim Legendary Edition Patch 3.0.0 or greater.\nVersion 2.0.0"
            << "2.0.0";
    }

    void test_extract_version()
    {
        QFETCH(QString, description);
        QFETCH(QString, version);
        QCOMPARE(Loot::extractVersion(description).value_or(QString()), version);
    }

    void test_reading_loot_test_plugins()
    {
        // esplugin's and libloot's expectations for LOOT's test plugins
        for (const auto& [folder, game] : { std::pair{ "Oblivion", GameType::Oblivion }, std::pair{ "Skyrim", GameType::Skyrim },
                                            std::pair{ "SkyrimSE", GameType::SkyrimSE } }) {
            const auto data = testingPlugins(folder);
            const auto plugins = Loot::loadPlugins(game, data, { "Blank.esm", "Blank - Master Dependent.esm", "Blank.esp" });
            QVERIFY2(plugins, qPrintable(plugins ? QString() : plugins.error()));
            const auto& blankEsm = (*plugins)[0];
            const auto& dependent = (*plugins)[1];
            const auto& blankEsp = (*plugins)[2];
            QVERIFY(blankEsm.master);
            QCOMPARE(blankEsm.version, std::optional<QString>("5.0"));
            QCOMPARE(blankEsm.header.description, std::optional<QString>("v5.0"));
            // no masters: its records can never decide an overlap, so they are not read
            QVERIFY(blankEsm.records.empty());
            QCOMPARE(blankEsm.overrideCount, 0);
            QVERIFY(dependent.master);
            QCOMPARE(dependent.header.masters, QStringList{ "Blank.esm" });
            QCOMPARE(dependent.records.size(), size_t(8));
            QCOMPARE(dependent.overrideCount, 4);
            QVERIFY(dependent.recordsOverlap(dependent));
            QVERIFY(!blankEsp.master);
            QCOMPARE(blankEsp.header.description, std::optional<QString>(QString::fromUtf8("€ƒŠ")));  // Windows-1252
            QCOMPARE(blankEsp.version, std::optional<QString>());
            QVERIFY(!blankEsp.recordsOverlap(blankEsm));
            // Blank.bsa holds one file and is loaded by Blank.esp (and by nothing else)
            QCOMPARE(blankEsp.assetCount, 1);
            QVERIFY(Loot::doAssetsOverlap(blankEsp.assets, blankEsp.assets));
            // Oblivion counts archives for .esp files only; Skyrim's Blank.esm loads Blank.bsa as well
            QCOMPARE(blankEsm.assetCount, game == GameType::Oblivion ? 0 : 1);
            QCOMPARE(dependent.assetCount, 0);
        }
        // a .esl is light and a master; its records are Blank.esp's
        const auto esl = Loot::loadPlugins(GameType::SkyrimSE, testingPlugins("SkyrimSE"), { "Blank.esl" });
        QVERIFY(esl);
        QVERIFY(esl->front().master);
        QVERIFY(Loot::isMasterFile(GameType::SkyrimSE, "x/Blank.esl", 0));
        QVERIFY(!Loot::isMasterFile(GameType::Skyrim, "x/Blank.esl", 0));
        QVERIFY(Loot::isMasterFile(GameType::Oblivion, "x/Blank.esp", 1));
        QVERIFY(!Loot::isMasterFile(GameType::Oblivion, "x/Blank.esm", 0));
        // Starfield resolves FormIDs with the master's scale: one record of the override plugin is Blank.full.esm's
        const auto starfield = Loot::loadPlugins(GameType::Starfield, testingPlugins("Starfield"), { "Blank.full.esm", "Blank - Override.esp" });
        QVERIFY2(starfield, qPrintable(starfield ? QString() : starfield.error()));
        QCOMPARE((*starfield)[1].overrideCount, 1);
        QVERIFY((*starfield)[1].recordsOverlap((*starfield)[1]));
        const auto missingMaster = Loot::loadPlugins(GameType::Starfield, testingPlugins("Starfield"), { "Blank - Override.esp" });
        QVERIFY(!missingMaster);
        QVERIFY(missingMaster.error().contains("Blank.full.esm"));
        // not a plugin
        QTemporaryDir dir;
        writeFile(FS::PathCombine(dir.path(), "Broken.esp"), "TES4\x10");
        const auto broken = Loot::loadPlugins(GameType::SkyrimSE, dir.path(), { "Broken.esp" });
        QVERIFY(!broken);
        QVERIFY(broken.error().contains("Broken.esp"));
    }

    void test_archives()
    {
        // the Fallout 4 archives: a general one and a texture one, one file each
        const auto fallout4 = testingPlugins("Fallout 4");
        const auto main = Loot::assetsInArchives({ FS::PathCombine(fallout4, "Blank - Main.ba2") });
        QCOMPARE(main.size(), size_t(1));
        QCOMPARE(main.begin()->first, QByteArray("dev\\git\\testing-plugins"));
        QCOMPARE(main.begin()->second, std::set<QByteArray>{ "license.txt" });
        const auto textures = Loot::assetsInArchives({ FS::PathCombine(fallout4, "Blank - Textures.ba2") });
        QCOMPARE(textures.begin()->second, std::set<QByteArray>{ "blank.dds" });
        QVERIFY(!Loot::doAssetsOverlap(main, textures));
        // the same LICENSE file in BSAs of three versions (103 compressed, 104, 105) reads the same
        const auto oblivion = Loot::assetsInArchives({ FS::PathCombine(testingPlugins("Oblivion"), "Blank.bsa") });
        const auto skyrim = Loot::assetsInArchives({ FS::PathCombine(testingPlugins("Skyrim"), "Blank.bsa") });
        const auto skyrimSE = Loot::assetsInArchives({ FS::PathCombine(testingPlugins("SkyrimSE"), "Blank.bsa") });
        // libloot's expectations: the LICENSE file, in folder "", "." and "dev\git\testing-plugins"
        for (const auto& [assets, folder] : { std::pair{ &oblivion, QByteArray("") }, std::pair{ &skyrim, QByteArray(".") },
                                              std::pair{ &skyrimSE, QByteArray("dev\\git\\testing-plugins") } }) {
            QCOMPARE(assets->size(), size_t(1));
            QCOMPARE(assets->begin()->first, folder);
            QCOMPARE(assets->begin()->second, std::set<QByteArray>{ "license" });
        }
        const auto combined = Loot::assetsInArchives({ FS::PathCombine(testingPlugins("Oblivion"), "Blank.bsa"),
                                                       FS::PathCombine(testingPlugins("Skyrim"), "Blank.bsa"),
                                                       FS::PathCombine(testingPlugins("SkyrimSE"), "Blank.bsa") });
        QCOMPARE(combined.size(), size_t(3));
        QVERIFY(!Loot::doAssetsOverlap(oblivion, skyrim));  // the same file name in different folders
        QVERIFY(Loot::doAssetsOverlap(skyrim, combined));
        // a broken archive is skipped
        QTemporaryDir dir;
        writeFile(FS::PathCombine(dir.path(), "Broken.bsa"), "BSA\0garbage");
        QVERIFY(Loot::assetsInArchives({ FS::PathCombine(dir.path(), "Broken.bsa"), FS::PathCombine(fallout4, "Blank - Main.ba2") }) == main);

        // which archives a plugin loads, per game
        const QByteArray plugin = pluginBytes(0, {}, QString(), {});
        const auto ba2 = FS::PathCombine(fallout4, "Blank - Main.ba2");
        const auto bsa = FS::PathCombine(testingPlugins("SkyrimSE"), "Blank.bsa");
        const auto oblivionPlugin = FS::read(FS::PathCombine(testingPlugins("Oblivion"), "Blank.esm"));
        QVERIFY(oblivionPlugin);
        auto assetCount = [&](GameType game, const QString& pluginName, const QStringList& archives) {
            QTemporaryDir data;
            // Oblivion's record headers are 20 bytes, the others' 24
            writeFile(FS::PathCombine(data.path(), pluginName), game == GameType::Oblivion ? *oblivionPlugin : plugin);
            for (const auto& archive : archives) {
                QFile::copy(archive.contains(".ba2") ? ba2 : bsa, FS::PathCombine(data.path(), archive));
            }
            const auto loaded = Loot::loadPlugins(game, data.path(), { pluginName });
            return loaded ? loaded->front().assetCount : -1;
        };
        QCOMPARE(assetCount(GameType::SkyrimSE, "Mod.esp", { "Mod.bsa" }), 1);
        QCOMPARE(assetCount(GameType::SkyrimSE, "Mod.esp", { "Mod - Textures.bsa" }), 1);
        QCOMPARE(assetCount(GameType::SkyrimSE, "Mod.esp", { "Mod - Meshes.bsa" }), 0);  // only those two names
        QCOMPARE(assetCount(GameType::Skyrim, "Mod.esp", { "Mod - Textures.bsa" }), 0);
        QCOMPARE(assetCount(GameType::Fallout4, "Mod.esp", { "Mod - Main.ba2" }), 1);
        QCOMPARE(assetCount(GameType::Fallout4, "Mod.esp", { "MOD Anything.ba2" }), 1);  // any name starting with the plugin's
        QCOMPARE(assetCount(GameType::Fallout4, "Mod.esp", { "Other.ba2" }), 0);
        QCOMPARE(assetCount(GameType::FalloutNV, "Mod.esp", { "Mod - Sounds.bsa" }), 1);
        QCOMPARE(assetCount(GameType::Oblivion, "Mod.esm", { "Mod.bsa" }), 0);  // Oblivion: plugins (.esp) only
        QCOMPARE(assetCount(GameType::Oblivion, "Mod.esp", { "Mod Extra.bsa" }), 1);
        QCOMPARE(assetCount(GameType::Starfield, "Mod.esm", { "Mod - Main.ba2" }), 1);
        QCOMPARE(assetCount(GameType::Starfield, "Mod.esm", { "Mod - Sounds.ba2" }), 0);
    }

    ////// Sorting //////

    void test_libloot_sorting_cases()
    {
        // libloot's sort_plugins tests (src/sorting/plugins.rs), on its test fixture groups
        auto a = testPlugin("A.esp");
        auto b = testPlugin("B.esp");
        QCOMPARE(sortTest({ { &b, 1 }, { &a, 0 } }).value_or(QStringList()), (QStringList{ "A.esp", "B.esp" }));
        QCOMPARE(sortTest({ { &a, 0 }, { &b, 1 } }).value_or(QStringList()), (QStringList{ "A.esp", "B.esp" }));
        // group metadata
        QCOMPARE(sortTest({ { &a, 1 }, { &b, 0 } }, [](Loot::SortingPlugin& p) { p.group = p.plugin->name == "A.esp" ? "A" : "B"; })
                     .value_or(QStringList()),
                 (QStringList{ "A.esp", "B.esp" }));
        // load after and requirement metadata
        const auto afterB = [](Loot::SortingPlugin& p) {
            if (p.plugin->name == "A.esp") {
                p.after = { "B.esp" };
            }
        };
        QCOMPARE(sortTest({ { &a, 0 }, { &b, 1 } }, afterB).value_or(QStringList()), (QStringList{ "B.esp", "A.esp" }));
        const auto needsB = [](Loot::SortingPlugin& p) {
            if (p.plugin->name == "A.esp") {
                p.requirements = { "B.esp" };
            }
        };
        QCOMPARE(sortTest({ { &a, 0 }, { &b, 1 } }, needsB).value_or(QStringList()), (QStringList{ "B.esp", "A.esp" }));
        // early loaders
        QCOMPARE(sortTest({ { &a, 1 }, { &b, 0 } }, {}, { "A.esp" }).value_or(QStringList()), (QStringList{ "A.esp", "B.esp" }));
        // a group that does not exist
        QVERIFY(!sortTest({ { &a, 0 } }, [](Loot::SortingPlugin& p) { p.group = "missing"; }));

        // cycles, named in the error
        auto cycleA = testPlugin("A.esp", false, { "B.esp" });
        auto cycleB = testPlugin("B.esp", false, { "A.esp" });
        const auto cycle = sortTest({ { &cycleA, 0 }, { &cycleB, 1 } });
        QVERIFY(!cycle);
        QVERIFY2(cycle.error().contains("A.esp --[Master]--> B.esp --[Master]--> A.esp"), qPrintable(cycle.error()));
        auto masterA = testPlugin("A.esp", true, { "B.esp" });
        const auto contradiction = sortTest({ { &masterA, 0 }, { &b, 1 } });
        QVERIFY(!contradiction);
        QVERIFY2(contradiction.error().contains("B.esp --[Master]--> A.esp --[Master Flag]--> B.esp"), qPrintable(contradiction.error()));
        auto masterOnly = testPlugin("A.esp", true);
        const auto loadAfterContradiction = sortTest({ { &masterOnly, 0 }, { &b, 1 } }, afterB);
        QVERIFY(!loadAfterContradiction);
        QVERIFY2(loadAfterContradiction.error().contains("B.esp --[Masterlist Load After]--> A.esp --[Master Flag]-->"),
                 qPrintable(loadAfterContradiction.error()));
        const auto requirementContradiction = sortTest({ { &masterOnly, 0 }, { &b, 1 } }, needsB);
        QVERIFY(!requirementContradiction);
        QVERIFY(requirementContradiction.error().contains("B.esp --[Masterlist Requirement]--> A.esp --[Master Flag]-->"));
        const auto earlyContradiction = sortTest({ { &masterOnly, 0 }, { &b, 1 } }, {}, { "B.esp" });
        QVERIFY(!earlyContradiction);
        QVERIFY(earlyContradiction.error().contains("B.esp --[Hardcoded]--> A.esp --[Master Flag]-->"));

        // Starfield blueprint masters load last; a master edge to one is fine, a load after one is not
        auto blueprint = testPlugin("A.esp", true);
        blueprint.blueprint = true;
        auto dependsOnBlueprint = testPlugin("B.esp", true, { "A.esp" });
        QCOMPARE(sortTest({ { &blueprint, 0 }, { &dependsOnBlueprint, 1 } }).value_or(QStringList()), (QStringList{ "B.esp", "A.esp" }));
        auto nonMasterOnBlueprint = testPlugin("B.esp", false, { "A.esp" });
        QCOMPARE(sortTest({ { &blueprint, 0 }, { &nonMasterOnBlueprint, 1 } }).value_or(QStringList()), (QStringList{ "B.esp", "A.esp" }));
        auto blueprintB = testPlugin("B.esp", true);
        blueprintB.blueprint = true;
        const auto loadAfterBlueprint = sortTest({ { &masterOnly, 0 }, { &blueprintB, 1 } }, afterB);
        QVERIFY(!loadAfterBlueprint);
        QVERIFY(loadAfterBlueprint.error().contains("B.esp --[Masterlist Load After]--> A.esp --[Blueprint Master]-->"));
    }

    void test_overlap_and_tie_breaks()
    {
        // records: the plugin overriding more records loads first, whatever the current order says
        auto many = testPlugin("Many.esp");
        auto few = testPlugin("Few.esp");
        for (quint32 i = 0; i < 5; i++) {
            many.records.push_back({ 0x800 + i, 1, true });
        }
        many.overrideCount = 5;
        few.records.push_back({ 0x800, 1, true });
        few.overrideCount = 1;
        QCOMPARE(sortTest({ { &few, 0 }, { &many, 1 } }).value_or(QStringList()), (QStringList{ "Many.esp", "Few.esp" }));
        // equal counts: the current order decides
        few.records.push_back({ 0x801, 1, true });
        few.overrideCount = 5;
        QCOMPARE(sortTest({ { &few, 0 }, { &many, 1 } }).value_or(QStringList()), (QStringList{ "Few.esp", "Many.esp" }));
        // assets: the plugin loading more assets loads first when records do not decide
        auto bigArchive = testPlugin("Big.esp");
        auto smallArchive = testPlugin("Small.esp");
        bigArchive.assets["textures"] = { "a.dds", "b.dds" };
        bigArchive.assetCount = 2;
        smallArchive.assets["textures"] = { "a.dds" };
        smallArchive.assetCount = 1;
        QCOMPARE(sortTest({ { &smallArchive, 0 }, { &bigArchive, 1 } }).value_or(QStringList()), (QStringList{ "Big.esp", "Small.esp" }));
        smallArchive.assets["textures"] = { "c.dds" };  // no shared file: no edge
        QCOMPARE(sortTest({ { &smallArchive, 0 }, { &bigArchive, 1 } }).value_or(QStringList()), (QStringList{ "Small.esp", "Big.esp" }));

        // libloot's tie-break case: a path B -> C -> D -> A that disagrees with the current order A..J goes first
        std::vector<Loot::Plugin> plugins;
        for (const char c : QByteArray("ABCDEFGHIJ")) {
            plugins.push_back(testPlugin(QString(QChar(c)) + ".esp"));
        }
        std::vector<std::pair<const Loot::Plugin*, int>> input;
        for (int i = 0; i < 10; i++) {
            input.emplace_back(&plugins[i], i);
        }
        const auto sorted = sortTest(input, [](Loot::SortingPlugin& p) {
            const QHash<QString, QString> after{ { "C.esp", "B.esp" }, { "D.esp", "C.esp" }, { "A.esp", "D.esp" } };
            if (after.contains(p.plugin->name)) {
                p.after = { after.value(p.plugin->name) };
            }
        });
        QCOMPARE(sorted.value_or(QStringList()), QString("B C D A E F G H I J").split(' ').replaceInStrings(QRegularExpression("$"), ".esp"));
    }

    /// libloot's end-to-end sort test (cpp/src/tests/api/interface/game_interface_test.h, sortPluginsShouldSucceedIfPassedValidArguments)
    void test_libloot_end_to_end_orders_data()
    {
        QTest::addColumn<int>("game");
        QTest::addColumn<QString>("source");
        QTest::newRow("Oblivion") << int(GameType::Oblivion) << "Oblivion";
        QTest::newRow("Skyrim") << int(GameType::Skyrim) << "Skyrim";
        QTest::newRow("Skyrim SE") << int(GameType::SkyrimSE) << "SkyrimSE";
        QTest::newRow("Fallout 3") << int(GameType::Fallout3) << "Skyrim";
        QTest::newRow("Fallout NV") << int(GameType::FalloutNV) << "Skyrim";
        QTest::newRow("Fallout 4") << int(GameType::Fallout4) << "SkyrimSE";
        QTest::newRow("Starfield") << int(GameType::Starfield) << "Starfield";
    }

    void test_libloot_end_to_end_orders()
    {
        QFETCH(int, game);
        QFETCH(QString, source);
        const auto type = static_cast<GameType>(game);
        QStringList initial;
        QStringList expected;
        if (type == GameType::Starfield) {
            initial = { "Blank.full.esm", "Blank - Override.full.esm", "Blank.esp", "Blank - Override.esp" };
            expected = { "Blank.full.esm", "Blank - Override.full.esm", "Blank - Override.esp", "Blank.esp" };
        } else {
            initial = { "Blank.esm",
                        "Blank - Different.esm",
                        "Blank - Master Dependent.esm",
                        "Blank - Different Master Dependent.esm",
                        "Blank.esp",
                        "Blank - Different.esp",
                        "Blank - Master Dependent.esp",
                        "Blank - Different Master Dependent.esp",
                        "Blank - Plugin Dependent.esp",
                        "Blank - Different Plugin Dependent.esp" };
            expected = { "Blank.esm",
                         "Blank - Master Dependent.esm",
                         "Blank - Different.esm",
                         "Blank - Different Master Dependent.esm",
                         "Blank - Master Dependent.esp",
                         "Blank - Different Master Dependent.esp",
                         "Blank.esp",
                         "Blank - Plugin Dependent.esp",
                         "Blank - Different.esp",
                         "Blank - Different Plugin Dependent.esp" };
        }
        if (type == GameType::Fallout4 || type == GameType::SkyrimSE) {
            initial << "Blank.esl";
            expected.insert(4, "Blank.esl");
        }
        QTemporaryDir dir;
        for (const auto& name : initial) {
            copyInto(FS::PathCombine(testingPlugins(source), name), dir.path());
        }
        QList<AutoSort::PluginState> current;
        for (const auto& name : initial) {
            current << AutoSort::PluginState{ name, false };
        }
        const auto noMetadata = Loot::Masterlist::load("plugins: []\n", "");
        QVERIFY(noMetadata);
        const auto early = AutoSort::earlyLoadingPlugins(type, dir.path(), {});
        // with no metadata the order stays as it is, every time (sortingShouldNotMakeUnnecessaryChangesToAnExistingLoadOrder)
        QStringList stable = initial;
        stable.removeAll("Blank.esl");
        QList<AutoSort::PluginState> stableState;
        for (const auto& name : stable) {
            stableState << AutoSort::PluginState{ name, false };
        }
        for (int run = 0; run < 10; run++) {
            QCOMPARE(AutoSort::sortPlugins(type, dir.path(), early, stableState, *noMetadata).value_or(QStringList()), stable);
        }
        const auto masterlist = Loot::Masterlist::load(libloot_test_masterlist, "");
        QVERIFY2(masterlist, qPrintable(masterlist ? QString() : masterlist.error()));
        const auto sorted = AutoSort::sortPlugins(type, dir.path(), early, current, *masterlist);
        QVERIFY2(sorted, qPrintable(sorted ? QString() : sorted.error()));
        QCOMPARE(*sorted, expected);
    }

    void test_early_loading_plugins()
    {
        QTemporaryDir dir;
        writeFile(FS::PathCombine(dir.path(), "Skyrim.ccc"), "ccQDRSSE001-SurvivalMode.esl\r\n\r\nccBGSSSE001-Fish.esm\nupdate.esm\n");
        QCOMPARE(AutoSort::earlyLoadingPlugins(GameType::SkyrimSE, dir.path(), {}),
                 (QStringList{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm", "ccQDRSSE001-SurvivalMode.esl",
                               "ccBGSSSE001-Fish.esm" }));
        QCOMPARE(AutoSort::earlyLoadingPlugins(GameType::Skyrim, dir.path(), {}), QStringList{ "Skyrim.esm" });
        QVERIFY(AutoSort::earlyLoadingPlugins(GameType::Oblivion, dir.path(), {}).isEmpty());
        // Starfield: the My Games list wins, even when empty
        writeFile(FS::PathCombine(dir.path(), "Starfield.ccc"), "SFBGS001.esm\n");
        QVERIFY(AutoSort::earlyLoadingPlugins(GameType::Starfield, dir.path(), {}).contains("SFBGS001.esm"));
        QTemporaryDir myGames;
        writeFile(FS::PathCombine(myGames.path(), "Starfield.ccc"), "");
        const auto starfield = AutoSort::earlyLoadingPlugins(GameType::Starfield, dir.path(), myGames.path());
        QCOMPARE(starfield.size(), 12);
        QVERIFY(!starfield.contains("SFBGS001.esm"));

        // the plugins the game always loads (BetterNMM's game definitions) include every fixed early loader
        QTemporaryDir empty;
        for (const auto& [domain, game] : { std::pair{ "skyrimspecialedition", GameType::SkyrimSE }, std::pair{ "skyrim", GameType::Skyrim },
                                            std::pair{ "fallout4", GameType::Fallout4 }, std::pair{ "starfield", GameType::Starfield } }) {
            const auto implicit = GameRegistry::byDomain(domain).implicitPlugins;
            for (const auto& name : AutoSort::earlyLoadingPlugins(game, empty.path(), {})) {
                QVERIFY2(implicit.contains(name), qPrintable(QString("%1: %2").arg(domain, name)));
            }
        }
    }

    void test_game_mapping()
    {
        QCOMPARE(AutoSort::gameType("skyrimspecialedition"), std::optional(GameType::SkyrimSE));
        QCOMPARE(AutoSort::gameType("newvegas"), std::optional(GameType::FalloutNV));
        QVERIFY(!AutoSort::gameType("morrowind"));
        QVERIFY(!AutoSort::gameType("cyberpunk2077"));
        QCOMPARE(AutoSort::masterlistRepository(GameType::FalloutNV), "falloutnv");
        QCOMPARE(AutoSort::masterlistUrl("skyrimse").toString(), "https://raw.githubusercontent.com/loot/skyrimse/v0.29/masterlist.yaml");
    }

    ////// Mods //////

    void test_sort_mods()
    {
        const QStringList current{ "patch", "base", "other", "textures", "late", "plugin-a", "plugin-b", "free" };
        AutoSort::ModRules rules;
        rules.requirements["patch"] = { "base" };
        rules.pluginRank = { { "plugin-a", 5 }, { "plugin-b", 2 }, { "late", 9 } };
        // winner first, as ModList::overwrites gives them: the later mod wins now
        const QList<std::pair<QString, QString>> conflicts{ { "base", "patch" }, { "plugin-b", "plugin-a" }, { "textures", "other" } };
        const auto sorted = AutoSort::sortMods(current, conflicts, rules);
        QCOMPARE(sorted.size(), current.size());
        auto before = [&sorted](const QString& a, const QString& b) { return sorted.indexOf(a) < sorted.indexOf(b); };
        QVERIFY(before("base", "patch"));          // the mod that needs the other wins
        QVERIFY(before("plugin-b", "plugin-a"));   // then the one whose plugin loads later
        QVERIFY(before("other", "textures"));      // otherwise the current winner stays the winner
        QCOMPARE(AutoSort::movedCount(current, sorted), 2);
        // nothing to change: the same order
        QCOMPARE(AutoSort::sortMods(sorted, conflicts, rules), sorted);
        // needs through another mod, and a requirement beats the plugin order
        AutoSort::ModRules chain;
        chain.requirements = { { "addon", { "framework" } }, { "framework", { "core" } } };
        chain.pluginRank = { { "core", 10 }, { "addon", 1 } };
        QCOMPARE(AutoSort::sortMods({ "addon", "core", "framework" }, { { "addon", "core" } }, chain), (QStringList{ "core", "addon", "framework" }));
        // rules that contradict each other: the weaker one is dropped instead of making a cycle
        AutoSort::ModRules circular;
        circular.requirements = { { "x", { "y" } } };
        circular.pluginRank = { { "y", 3 }, { "z", 2 }, { "x", 1 } };
        const auto noCycle = AutoSort::sortMods({ "x", "y", "z" }, { { "y", "x" }, { "z", "y" }, { "x", "z" } }, circular);
        QCOMPARE(noCycle.size(), 3);
        QVERIFY(noCycle.indexOf("y") < noCycle.indexOf("x"));

        // sorting the result again changes nothing, also when rules disagree and the pairs come in another order
        AutoSort::ModRules mixed;
        mixed.requirements = { { "a", { "b" } } };
        mixed.pluginRank = { { "b", 9 }, { "c", 5 }, { "a", 1 }, { "d", 7 } };
        QList<std::pair<QString, QString>> pairs{ { "b", "a" }, { "c", "b" }, { "c", "a" }, { "d", "c" }, { "d", "a" }, { "e", "d" } };
        const QStringList start{ "a", "b", "c", "d", "e" };
        const auto once = AutoSort::sortMods(start, pairs, mixed);
        QHash<QString, int> at;
        for (int i = 0; i < once.size(); i++) {
            at.insert(once[i], i);
        }
        QList<std::pair<QString, QString>> repaired;  // winner first again, for the new order, reversed
        for (const auto& [x, y] : pairs) {
            repaired.prepend(at.value(x) > at.value(y) ? std::pair{ x, y } : std::pair{ y, x });
        }
        QCOMPARE(AutoSort::sortMods(once, repaired, mixed), once);
    }

    void test_sharing_pairs_do_not_depend_on_order()
    {
        QTemporaryDir dir;
        ModList mods(FS::PathCombine(dir.path(), "mods"), FS::PathCombine(dir.path(), "mods.json"));
        for (const auto& id : { "one", "two", "three", "four" }) {
            writeFile(FS::PathCombine(dir.path(), QString("mods/%1/Data/shared.txt").arg(id)), id);
            ModEntry entry;
            entry.id = id;
            entry.name = id;
            entry.enabled = QString(id) != "four";  // disabled mods do not count
            mods.upsert(entry);
        }
        auto enabledIds = [&mods] {
            QStringList ids;
            for (const auto& mod : mods.enabledMods()) {
                ids << mod.id;
            }
            return ids;
        };
        auto pairs = ModList::sharingPairs(mods.modsDir(), enabledIds());
        QCOMPARE(pairs.size(), 3);  // one-two, one-three, two-three: all of them, not just a chain
        QVERIFY(pairs.contains(std::pair<QString, QString>{ "three", "one" }));
        mods.setOrder({ "three", "two", "one" });
        pairs = ModList::sharingPairs(mods.modsDir(), enabledIds());
        QCOMPARE(pairs.size(), 3);
        QVERIFY(pairs.contains(std::pair<QString, QString>{ "one", "three" }));  // the winner first
    }

    void test_list_set_order()
    {
        QTemporaryDir dir;
        const auto data = FS::PathCombine(dir.path(), "Data");
        writeFile(FS::PathCombine(data, "Skyrim.esm"), pluginBytes(1, {}, QString(), {}));
        writeFile(FS::PathCombine(data, "A.esp"), pluginBytes(0, {}, QString(), {}));
        writeFile(FS::PathCombine(data, "B.esp"), pluginBytes(0, {}, QString(), {}));
        writeFile(FS::PathCombine(data, "M.esm"), pluginBytes(1, {}, QString(), {}));
        auto game = GameRegistry::byDomain("skyrimspecialedition");
        PluginList plugins(game, { .dataDir = data,
                                   .gameDir = dir.path(),
                                   .orderFile = FS::PathCombine(dir.path(), "loadorder.txt"),
                                   .gameListDir = FS::PathCombine(dir.path(), "local") });
        QVERIFY(plugins.refresh());
        plugins.setOrder({ "B.esp", "a.ESP", "M.esm", "Skyrim.esm" });
        QStringList names;
        for (const auto& p : plugins.plugins()) {
            names << p.name;
        }
        QCOMPARE(names, (QStringList{ "Skyrim.esm", "M.esm", "B.esp", "A.esp" }));  // the game's own and masters still lead
        QVERIFY(FS::read(FS::PathCombine(dir.path(), "loadorder.txt"))->contains("B.esp\r\nA.esp"));

        ModList mods(FS::PathCombine(dir.path(), "mods"), FS::PathCombine(dir.path(), "mods.json"));
        for (const auto& id : { "one", "two", "three" }) {
            ModEntry entry;
            entry.id = id;
            entry.name = id;
            mods.upsert(entry);
        }
        QSignalSpy deployment(&mods, &ModList::deploymentChanged);
        mods.setOrder({ "one", "two", "three" });
        QCOMPARE(deployment.count(), 0);  // unchanged: nothing to deploy
        mods.setOrder({ "three", "one" });
        QCOMPARE(deployment.count(), 1);
        QStringList ids;
        for (const auto& m : mods.mods()) {
            ids << m.id;
        }
        QCOMPARE(ids, (QStringList{ "three", "one", "two" }));
    }
};

QTEST_GUILESS_MAIN(AutoSortTest)

#include "AutoSort_test.moc"
