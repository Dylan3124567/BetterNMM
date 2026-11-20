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

#include <QTest>

#include <algorithm>

#include <game/HealthCheck.h>

using HealthCheck::Kind;

namespace {

const QString g_game = "skyrimspecialedition";
constexpr int g_gameId = 1704;
constexpr int g_skse = 30379;

ModEntry mod(const QString& id, int nexusId, bool enabled)
{
    return ModEntry{ .id = id, .name = id + " mod", .enabled = enabled, .nexusGame = g_game, .nexusModId = nexusId };
}

Nexus::Requirement needs(int modId, const QString& name, const QString& notes = {}, const QString& url = {}, int gameId = 0)
{
    return Nexus::Requirement{ .modId = modId, .gameId = gameId, .name = name, .notes = notes, .url = url };
}

}  // namespace

class HealthCheckTest : public QObject {
    Q_OBJECT

   private slots:
    void test_plugin_masters()
    {
        const QList<PluginEntry> plugins{
            { .name = "Skyrim.esm", .master = true, .implicit = true },
            { .name = "A.esp", .active = true, .masters = { "Skyrim.esm", "Missing.esm" } },
            { .name = "B.esp", .active = true, .masters = { "off.ESP" } },
            { .name = "Off.esp" },
            { .name = "C.esp", .active = true, .masters = { "Skyrim.esm", "Late.esp" } },
            { .name = "Late.esp", .active = true, .masters = { "Skyrim.esm" } },
            { .name = "D.esp", .masters = { "Missing2.esm" } },  // inactive: its masters do not matter
            { .name = "E.esp", .active = true, .masters = { "Late.esp", "Skyrim.esm" } },
        };
        const auto problems = HealthCheck::pluginProblems(plugins);
        QCOMPARE(problems.size(), 3);
        QCOMPARE(problems[0].kind, Kind::MissingMaster);
        QCOMPARE(problems[0].plugin, QString("A.esp"));
        QCOMPARE(problems[0].master, QString("Missing.esm"));
        QCOMPARE(problems[1].kind, Kind::InactiveMaster);
        QCOMPARE(problems[1].plugin, QString("B.esp"));
        QCOMPARE(problems[1].master, QString("Off.esp"));  // named as the load order has it
        QCOMPARE(problems[2].kind, Kind::MasterLoadsLater);
        QCOMPARE(problems[2].plugin, QString("C.esp"));
        QCOMPARE(problems[2].master, QString("Late.esp"));
        for (const auto& problem : problems) {
            QVERIFY(HealthCheck::describe(problem).contains(problem.plugin));
            QVERIFY(HealthCheck::remedy(problem).contains(problem.master));
        }

        // a missing master that a disabled mod has: enabling that mod is the fix
        const auto withDisabled = HealthCheck::pluginProblems(plugins, { { "missing.esm", { "M", "Masters mod" } } });
        QCOMPARE(withDisabled.size(), 3);
        QCOMPARE(withDisabled[0].kind, Kind::MasterInDisabledMod);
        QCOMPARE(withDisabled[0].modId, QString("M"));
        QVERIFY(HealthCheck::describe(withDisabled[0]).contains("Masters mod"));
        QCOMPARE(HealthCheck::remedy(withDisabled[0]), QString("Enable Masters mod"));
        // only a missing one: an inactive master is there already
        QCOMPARE(HealthCheck::pluginProblems(plugins, { { "off.esp", { "O", "Off mod" } } })[1].kind, Kind::InactiveMaster);
    }

    void test_requirements()
    {
        QList<ModEntry> mods{ mod("X", 10, true), mod("Y", 11, true), mod("Z", 12, false), mod("W", 13, false) };
        mods.insert(1, ModEntry{ .id = "|separator|1", .name = "Group", .separator = true });
        const QList<Nexus::ModDetails> details{
            { .modId = 10, .game = g_game, .gameId = g_gameId, .name = "X",
              .requirements = { needs(12, "Z on Nexus"), needs(99, "SkyUI", "for the menu"), needs(11, "Y on Nexus"),
                                needs(0, "SKSE64", {}, "https://skse.silverlock.org"), needs(0, "Some Tool", {}, "https://github.com/tool"),
                                needs(50, "Other game mod", {}, {}, 1234), needs(10, "itself") } },
            { .modId = 11, .game = g_game, .gameId = g_gameId, .name = "Y",
              .requirements = { needs(99, "SkyUI", "needed"), needs(g_skse, "Skyrim Script Extender (SKSE64)") } },
            // a disabled mod's requirements do not matter
            { .modId = 13, .game = g_game, .gameId = g_gameId, .name = "W", .requirements = { needs(98, "Something") } },
        };
        const HealthCheck::ScriptExtender missingExtender{ .exists = true, .nexusModId = g_skse, .installed = false };
        auto problems = HealthCheck::requirementProblems(mods, details, missingExtender);
        QCOMPARE(problems.size(), 3);
        const auto find = [&problems](Kind kind) {
            for (const auto& problem : problems) {
                if (problem.kind == kind) {
                    return problem;
                }
            }
            return HealthCheck::Problem{};
        };
        const auto disabled = find(Kind::DisabledRequirement);
        QCOMPARE(disabled.modId, QString("Z"));
        QCOMPARE(disabled.neededBy, QStringList{ "X mod" });
        QCOMPARE(disabled.nexusModId, 12);
        const auto missing = find(Kind::MissingRequirement);
        QCOMPARE(missing.nexusModId, 99);
        QCOMPARE(missing.game, g_game);
        QCOMPARE(missing.requirement, QString("SkyUI"));
        QCOMPARE(missing.neededBy, (QStringList{ "X mod", "Y mod" }));
        QCOMPARE(missing.notes, (QStringList{ "for the menu", "needed" }));
        QVERIFY(HealthCheck::describe(missing).contains("X mod, Y mod"));
        // named on its own site by one mod and by its Nexus Mods page by the other: one problem
        const auto extender = find(Kind::MissingScriptExtender);
        QCOMPARE(extender.neededBy, (QStringList{ "X mod", "Y mod" }));

        // with the loader in the game folder the script extender is there
        problems = HealthCheck::requirementProblems(mods, details, { .exists = true, .nexusModId = g_skse, .installed = true });
        QCOMPARE(problems.size(), 2);
        // a game without one has no such problem, and a link to its site is like any other site
        problems = HealthCheck::requirementProblems(mods, details, {});
        QCOMPARE(problems.size(), 3);
        QCOMPARE(find(Kind::MissingScriptExtender).neededBy, QStringList());
        QCOMPARE(std::count_if(problems.begin(), problems.end(), [](const HealthCheck::Problem& p) { return p.kind == Kind::MissingRequirement; }), 2);

        // installed as a mod but disabled: enabling it is the fix
        mods << mod("SKSE", g_skse, false);
        problems = HealthCheck::requirementProblems(mods, details, missingExtender);
        QCOMPARE(problems.size(), 4);
        bool found = false;
        for (const auto& problem : problems) {
            if (problem.kind == Kind::DisabledRequirement && problem.modId == "SKSE") {
                QCOMPARE(problem.neededBy, QStringList{ "Y mod" });
                found = true;
            }
        }
        QVERIFY(found);
        QCOMPARE(find(Kind::MissingScriptExtender).neededBy, QStringList{ "X mod" });

        // everything there and enabled: nothing to say
        mods = { mod("X", 10, true), mod("Y", 11, true), mod("Z", 12, true), mod("S", 99, true), mod("SKSE", g_skse, true) };
        QCOMPARE(HealthCheck::requirementProblems(mods, details, missingExtender).size(), 1);  // X names SKSE64's site, the loader is not there
        QVERIFY(HealthCheck::requirementProblems(mods, details, { .exists = true, .nexusModId = g_skse, .installed = true }).isEmpty());
    }
};

QTEST_GUILESS_MAIN(HealthCheckTest)

#include "HealthCheck_test.moc"
