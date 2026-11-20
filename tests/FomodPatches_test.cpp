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
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <FileSystem.h>
#include <archive/ArchiveWriter.h>
#include <game/FomodPatches.h>
#include <ui/SelfUpdate.h>

// moc can not read raw string literals, so the fixtures live where it never looks
#ifndef Q_MOC_RUN
#include "FomodFixtures.h"
#endif

namespace {

/// The archive's files, under a wrapper folder and with other case than the installer names them
const QList<QPair<QString, QByteArray>> g_files = {
    { "Lights/Core/Lights.esp", "core plugin" },
    { "Lights/Main/Standard.esp", "standard main plugin, the large one" },
    { "Lights/Main/Lite.esp", "lite main" },
    { "Lights/Patches/Vanilla.esp", "vanilla weather patch" },
    { "Lights/PATCHES/weather.ESP", "weather patch" },
    { "Lights/Patches/Citizens/Lights - Citizens.esp", "citizens patch" },
    { "Lights/Patches/Citizens/textures/lamp.dds", "lamp texture" },
    { "Lights/Patches/Snow.esp", "snow patch" },
    { "Lights/Patches/Water.esp", "water patch" },
    { "Lights/readme.txt", "read me" },
};

void makeArchive(const QString& path, const QByteArray& config, const QList<QPair<QString, QByteArray>>& files)
{
    MMCZip::ArchiveWriter writer(path);
    QVERIFY(writer.open().has_value());
    QVERIFY(writer.addFile("Lights/fomod/ModuleConfig.xml", config).has_value());
    for (const auto& [name, content] : files) {
        QVERIFY(writer.addFile(name, content).has_value());
    }
    QVERIFY(writer.close().has_value());
}

void writeFile(const QString& path, const QByteArray& content)
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

QByteArray content(const QString& archivePath)
{
    for (const auto& [name, data] : g_files) {
        if (name == archivePath) {
            return data;
        }
    }
    return {};
}

FomodPatches::FileStates plugins(const QStringList& active)
{
    return [active](const QString& file) {
        return active.contains(file, Qt::CaseInsensitive) ? Fomod::FileState::Active : Fomod::FileState::Missing;
    };
}

const QStringList g_others = { "Weather.esp", "Citizens.esp", "Water.esp", "Snow.esp" };

QStringList titles(const QList<FomodPatches::Patch>& patches)
{
    QStringList out;
    for (const auto& patch : patches) {
        out << (patch.title.isEmpty() ? "auto:" + patch.files.join(",") : patch.title);
    }
    out.sort();
    return out;
}

}  // namespace

class FomodPatchesTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    FomodPatches::Mod m_mod;

    /// The mod as its installer put it there: Standard, vanilla weather and the readme picked
    void installDefault()
    {
        QDir(m_mod.folder).removeRecursively();
        writeFile(FS::PathCombine(m_mod.folder, "Lights.esp"), content("Lights/Core/Lights.esp"));
        writeFile(FS::PathCombine(m_mod.folder, "Lights - Main.esp"), content("Lights/Main/Standard.esp"));
        writeFile(FS::PathCombine(m_mod.folder, "Lights - Vanilla Weather.esp"), content("Lights/Patches/Vanilla.esp"));
        writeFile(FS::PathCombine(m_mod.folder, "docs/readme.txt"), content("Lights/readme.txt"));
    }

   private slots:
    void init()
    {
        m_mod = FomodPatches::Mod{ .id = "lights", .name = "Lights", .archive = FS::PathCombine(m_dir.path(), "Lights.zip"),
                                   .folder = FS::PathCombine(m_dir.path(), "mods/lights/Data") };
        if (!QFileInfo::exists(m_mod.archive)) {
            makeArchive(m_mod.archive, g_lights, g_files);
        }
        installDefault();
    }

    void test_patches_for_mods_you_have()
    {
        const auto check = FomodPatches::find({ m_mod }, plugins(g_others), {});
        QVERIFY(check.errors.isEmpty());
        // the weather patch is only recommended with the Standard version: the folder's main plugin has Standard's size
        QCOMPARE(titles(check.patches), QStringList({ "Citizens patch", "Water patch", "Weather patch", "auto:Lights - Snow.esp" }));
        for (const auto& patch : check.patches) {
            if (patch.title == "Weather patch") {
                QCOMPARE(patch.replaces, QStringList{ "Vanilla weather" });
                QCOMPARE(patch.files, QStringList{ "Lights - Weather.esp" });
            }
            if (patch.title == "Citizens patch") {
                QVERIFY(patch.files.isEmpty());  // only a flag; its files follow from it
            }
        }
        // without the other mods nothing is a patch
        QVERIFY(FomodPatches::find({ m_mod }, plugins({}), {}).patches.isEmpty());
    }

    void test_lite_version_does_not_want_the_weather_patch()
    {
        writeFile(FS::PathCombine(m_mod.folder, "Lights - Main.esp"), content("Lights/Main/Lite.esp"));
        QVERIFY(!titles(FomodPatches::find({ m_mod }, plugins(g_others), {}).patches).contains("Weather patch"));
    }

    void test_recorded_choices_win_over_files()
    {
        m_mod.choices = { { "Main", "Version", "Lite" }, { "Patches", "Weather", "Vanilla weather" } };
        QVERIFY(!titles(FomodPatches::find({ m_mod }, plugins(g_others), {}).patches).contains("Weather patch"));
    }

    void test_flag_only_option_found_through_its_files()
    {
        writeFile(FS::PathCombine(m_mod.folder, "Lights - Citizens.esp"), content("Lights/Patches/Citizens/Lights - Citizens.esp"));
        writeFile(FS::PathCombine(m_mod.folder, "textures/lamp.dds"), content("Lights/Patches/Citizens/textures/lamp.dds"));
        QVERIFY(!titles(FomodPatches::find({ m_mod }, plugins(g_others), {}).patches).contains("Citizens patch"));
    }

    void test_missing_archive_is_reported()
    {
        auto gone = m_mod;
        gone.archive = FS::PathCombine(m_dir.path(), "gone.zip");
        const auto check = FomodPatches::find({ gone }, plugins(g_others), {});
        QCOMPARE(check.unchecked, QStringList{ "Lights" });
        QVERIFY(check.patches.isEmpty());
    }

    void test_archive_read_once_then_cached()
    {
        QTemporaryDir dir;
        auto copy = m_mod;
        copy.archive = FS::PathCombine(dir.path(), "Lights.zip");
        QVERIFY(QFile::copy(m_mod.archive, copy.archive));
        const auto cache = FS::PathCombine(dir.path(), "cache");
        const auto states = plugins(g_others);
        const auto first = titles(FomodPatches::find({ copy }, states, cache).patches);
        QCOMPARE(first.size(), 4);

        // the same file (size and time) is not read again: garbage in its place goes unnoticed
        QFile file(copy.archive);
        QVERIFY(file.open(QIODevice::ReadWrite));
        const auto time = file.fileTime(QFileDevice::FileModificationTime);
        const auto size = file.size();
        file.write(QByteArray(size, 'x'));
        QVERIFY(file.setFileTime(time, QFileDevice::FileModificationTime));
        file.close();
        QCOMPARE(titles(FomodPatches::find({ copy }, states, cache).patches), first);
        // without the cache it is read, and found broken
        QCOMPARE(FomodPatches::find({ copy }, states, {}).errors.size(), 1);
        // a changed archive (another time) is read again
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(time.addSecs(60), QFileDevice::FileModificationTime));
        file.close();
        QCOMPARE(FomodPatches::find({ copy }, states, cache).errors.size(), 1);
    }

    void test_install_adds_patches_and_replaces_options()
    {
        const auto states = plugins(g_others);
        const auto found = FomodPatches::find({ m_mod }, states, {}).patches;
        const auto installed = FomodPatches::install(m_mod, found, states, {});
        QVERIFY2(installed.has_value(), qPrintable(installed ? QString() : installed.error()));

        QCOMPARE(readFile(FS::PathCombine(m_mod.folder, "Lights - Weather.esp")), content("Lights/PATCHES/weather.ESP"));
        QCOMPARE(readFile(FS::PathCombine(m_mod.folder, "Lights - Water.esp")), content("Lights/Patches/Water.esp"));
        QCOMPARE(readFile(FS::PathCombine(m_mod.folder, "Lights - Snow.esp")), content("Lights/Patches/Snow.esp"));
        // the flag-only option brought its conditional folder
        QCOMPARE(readFile(FS::PathCombine(m_mod.folder, "Lights - Citizens.esp")), content("Lights/Patches/Citizens/Lights - Citizens.esp"));
        QCOMPARE(readFile(FS::PathCombine(m_mod.folder, "textures/lamp.dds")), content("Lights/Patches/Citizens/textures/lamp.dds"));
        // the weather patch took the vanilla option's place in its pick-one group
        QVERIFY(!QFileInfo::exists(FS::PathCombine(m_mod.folder, "Lights - Vanilla Weather.esp")));
        QCOMPARE(readFile(FS::PathCombine(m_mod.folder, "Lights - Main.esp")), content("Lights/Main/Standard.esp"));

        auto added = installed->plugins;
        added.sort();
        QCOMPARE(added, QStringList({ "lights - citizens.esp", "lights - snow.esp", "lights - water.esp", "lights - weather.esp" }));
        QVERIFY(installed->choices.contains(QStringList({ "Patches", "Weather", "Weather patch" })));
        QVERIFY(!installed->choices.contains(QStringList({ "Patches", "Weather", "Vanilla weather" })));
        QVERIFY(installed->choices.contains(QStringList({ "Main", "Version", "Standard" })));

        // no unpacked leftovers, and nothing is missing anymore, with the recorded choices or without
        for (const auto& entry : QDirListing(m_mod.folder, QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly)) {
            QVERIFY2(!entry.fileName().contains(".part-"), qPrintable(entry.fileName()));
        }
        auto recorded = m_mod;
        recorded.choices = installed->choices;
        QVERIFY(FomodPatches::find({ recorded }, states, {}).patches.isEmpty());
        QVERIFY(FomodPatches::find({ m_mod }, states, {}).patches.isEmpty());
    }

    void test_install_keeps_a_replaced_file_you_changed()
    {
        writeFile(FS::PathCombine(m_mod.folder, "Lights - Vanilla Weather.esp"), "edited by hand, longer than before");
        m_mod.choices = { { "Main", "Version", "Standard" }, { "Patches", "Weather", "Vanilla weather" }, { "Patches", "Extras", "Readme" } };
        const auto states = plugins(g_others);
        QList<FomodPatches::Patch> weather;
        for (const auto& patch : FomodPatches::find({ m_mod }, states, {}).patches) {
            if (patch.title == "Weather patch") {
                weather << patch;
            }
        }
        QCOMPARE(weather.size(), 1);
        QVERIFY(FomodPatches::install(m_mod, weather, states, {}).has_value());
        QCOMPARE(readFile(FS::PathCombine(m_mod.folder, "Lights - Vanilla Weather.esp")), QByteArray("edited by hand, longer than before"));
        QVERIFY(QFileInfo::exists(FS::PathCombine(m_mod.folder, "Lights - Weather.esp")));
    }

    void test_install_rejects_paths_outside_the_mod()
    {
        QTemporaryDir dir;
        auto evil = m_mod;
        evil.archive = FS::PathCombine(dir.path(), "Escape.zip");
        evil.folder = FS::PathCombine(dir.path(), "mods/escape/Data");
        QVERIFY(QDir().mkpath(evil.folder));
        makeArchive(evil.archive, g_escape, { { "Lights/evil.esp", "evil" } });
        const auto states = plugins(g_others);
        const auto found = FomodPatches::find({ evil }, states, {}).patches;
        QCOMPARE(found.size(), 1);
        QVERIFY(!FomodPatches::install(evil, found, states, {}).has_value());
        QVERIFY(!QFileInfo::exists(FS::PathCombine(dir.path(), "mods/evil.esp")));
    }

    void test_choice_names_skip_hidden_steps()
    {
        auto module = Fomod::parse(g_lights);
        QVERIFY(module.has_value());
        const Fomod::Context base{ plugins({}), {} };
        // the Water step is hidden without Water.esp, so a selection left there is not recorded
        const auto names = FomodPatches::choiceNames(*module, { { 0, 0, 0 }, { 2, 0, 0 } }, base);
        QCOMPARE(names, QList<QStringList>{ QStringList({ "Main", "Version", "Standard" }) });
    }

    void test_release_parsing()
    {
        const auto release = SelfUpdate::parseRelease(g_release);
        QVERIFY(release.has_value());
        QCOMPARE(release->version, QVersionNumber(1, 2, 3));
        QCOMPARE(release->installer, QUrl("https://x/setup.exe"));
        QCOMPARE(release->size, qint64(166676508));
        QCOMPARE(release->notes, QString("notes"));
        QVERIFY(!SelfUpdate::parseRelease(g_nightly).has_value());
        QVERIFY(!SelfUpdate::parseRelease("not json").has_value());
    }
};

QTEST_GUILESS_MAIN(FomodPatchesTest)

#include "FomodPatches_test.moc"
