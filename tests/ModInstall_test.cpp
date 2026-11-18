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
#include <QStringConverter>
#include <QTemporaryDir>
#include <QTest>

#include <FileSystem.h>
#include <archive/ArchiveReader.h>
#include <archive/ArchiveWriter.h>
#include <game/Fomod.h>
#include <game/GameDefinition.h>
#include <game/ModInstaller.h>

// moc can not read raw string literals, so the fixtures live where it never looks
#ifndef Q_MOC_RUN
#include "FomodFixtures.h"
#endif

namespace {

void writeFile(const QString& path, const QByteArray& content = "x")
{
    QVERIFY(FS::ensureFilePathExists(path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
}

QStringList listTree(const QString& root)
{
    QStringList out;
    for (const auto& entry : QDirListing(root, QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly)) {
        out << QDir(root).relativeFilePath(entry.absoluteFilePath());
    }
    out.sort();
    return out;
}

GameDefinition skyrim()
{
    GameDefinition game;
    game.domain = "skyrimspecialedition";
    game.name = "Skyrim Special Edition";
    game.executable = "SkyrimSE.exe";
    game.dataPath = "Data";
    game.pluginFormat = PluginFormat::Asterisk;
    return game;
}


}  // namespace

class ModInstallTest : public QObject {
    Q_OBJECT

   private slots:
    void test_fomod_parse_and_evaluate()
    {
        // installers ship UTF-16 configurations with a byte order mark
        auto encoder = QStringEncoder(QStringConverter::Utf16LE, QStringConverter::Flag::WriteBom);
        const QByteArray utf16 = encoder(QString::fromUtf8(g_config));
        auto module = Fomod::parse(utf16);
        QVERIFY2(module.has_value(), qPrintable(module ? QString() : module.error()));
        QCOMPARE(module->name, QString("Example Overhaul"));
        QCOMPARE(module->steps.size(), 2);
        const auto& groups = module->steps[0].groups;
        QCOMPARE(groups[1].plugins[0].name, QString("Aa readme"));  // "Ascending" sorts by name
        QCOMPARE(groups[0].plugins[0].files[0].source, QString("10 2K/textures"));
        QCOMPARE(groups[1].plugins[1].files[0].destination, QString("20 Patch/Patch.esp"));  // no destination: same as source

        Fomod::Context context;
        context.fileState = [](const QString& file) {
            return file == "Dawnguard.esm" ? Fomod::FileState::Active : Fomod::FileState::Missing;
        };
        QCOMPARE(Fomod::pluginType(groups[1].plugins[1], context), Fomod::PluginType::Recommended);
        Fomod::Context noDawnguard;
        QCOMPARE(Fomod::pluginType(groups[1].plugins[1], noDawnguard), Fomod::PluginType::NotUsable);

        // defaults: the recommended 2K and the recommended patch
        auto selections = Fomod::defaultSelections(*module, 0, context);
        QCOMPARE(selections.size(), 2);
        QVERIFY(!Fomod::stepVisible(module->steps[1], Fomod::Context{ context.fileState, Fomod::flagsFor(*module, selections, context) }));
        auto files = Fomod::filesToInstall(*module, selections, context);
        QStringList sources;
        for (const auto& f : files) {
            sources << f.source;
        }
        QCOMPARE(sources, QStringList({ "00 Core", "10 2K/textures", "20 Patch/Patch.esp" }));

        // choosing 4K shows the second step and the conditional file, which goes last for its priority
        QList<Fomod::Selection> fourK{ { 0, 0, 1 }, { 1, 0, 0 } };
        QVERIFY(Fomod::stepVisible(module->steps[1], Fomod::Context{ context.fileState, Fomod::flagsFor(*module, fourK, context) }));
        files = Fomod::filesToInstall(*module, fourK, context);
        sources.clear();
        for (const auto& f : files) {
            sources << f.source;
        }
        QCOMPARE(sources, QStringList({ "00 Core", "11 4K/textures", "30 Parallax/p.dds", "40 Conditional/hd.ini" }));

        QVERIFY(!Fomod::parse("<notconfig/>").has_value());
        QVERIFY(!Fomod::parse("<config><broken").has_value());
    }

    void test_simple_data_mod_with_wrapper_folder()
    {
        QTemporaryDir dir;
        const auto game = FS::PathCombine(dir.path(), "game");
        writeFile(FS::PathCombine(game, "Data/Skyrim.esm"));
        const auto extracted = FS::PathCombine(dir.path(), "extracted");
        writeFile(FS::PathCombine(extracted, "Cool Mod v1.2/Cool.esp"));
        writeFile(FS::PathCombine(extracted, "Cool Mod v1.2/meshes/a.nif"));

        const auto analysis = ModInstaller::analyze(extracted, skyrim(), game);
        QCOMPARE(analysis.layout, ModInstaller::Layout::DataFolder);
        QVERIFY(analysis.fomodConfig.isEmpty());
        const auto staging = FS::PathCombine(dir.path(), "staging");
        QVERIFY(ModInstaller::stage(analysis, skyrim(), staging).has_value());
        QCOMPARE(listTree(staging), QStringList({ "Data/Cool.esp", "Data/meshes/a.nif" }));
    }

    void test_wrapper_folder_beside_documents()
    {
        QTemporaryDir dir;
        const auto game = FS::PathCombine(dir.path(), "game");
        writeFile(FS::PathCombine(game, "Data/Skyrim.esm"));
        const auto extracted = FS::PathCombine(dir.path(), "extracted");
        writeFile(FS::PathCombine(extracted, "C Mod/C Mod.esp"));
        writeFile(FS::PathCombine(extracted, "C Mod/meshes/c.nif"));
        writeFile(FS::PathCombine(extracted, "Readme.txt"));
        writeFile(FS::PathCombine(extracted, "Screenshot.PNG"));

        const auto analysis = ModInstaller::analyze(extracted, skyrim(), game);
        QCOMPARE(analysis.layout, ModInstaller::Layout::DataFolder);
        const auto staging = FS::PathCombine(dir.path(), "staging");
        QVERIFY(ModInstaller::stage(analysis, skyrim(), staging).has_value());
        QCOMPARE(listTree(staging), QStringList({ "Data/C Mod.esp", "Data/meshes/c.nif" }));

        // a plugin beside the folder is part of the mod, so the folder is not a wrapper then
        const auto withPlugin = FS::PathCombine(dir.path(), "withPlugin");
        writeFile(FS::PathCombine(withPlugin, "Assets/a.nif"));
        writeFile(FS::PathCombine(withPlugin, "Main.esp"));
        writeFile(FS::PathCombine(withPlugin, "Readme.txt"));
        QCOMPARE(ModInstaller::analyze(withPlugin, skyrim(), game).contentRoot, withPlugin);
    }

    void test_script_extender_goes_to_root_without_sources()
    {
        QTemporaryDir dir;
        const auto game = FS::PathCombine(dir.path(), "game");
        writeFile(FS::PathCombine(game, "SkyrimSE.exe"));
        const auto extracted = FS::PathCombine(dir.path(), "extracted");
        const auto root = FS::PathCombine(extracted, "skse64_2_02_06");
        writeFile(FS::PathCombine(root, "skse64_loader.exe"));
        writeFile(FS::PathCombine(root, "skse64_1_6_1170.dll"));
        writeFile(FS::PathCombine(root, "src/skse64/main.cpp"));
        writeFile(FS::PathCombine(root, "data/Scripts/Actor.pex"));

        const auto analysis = ModInstaller::analyze(extracted, skyrim(), game);
        QCOMPARE(analysis.layout, ModInstaller::Layout::GameRoot);
        QCOMPARE(analysis.skipped, QStringList({ "src" }));
        const auto staging = FS::PathCombine(dir.path(), "staging");
        QVERIFY(ModInstaller::stage(analysis, skyrim(), staging).has_value());
        // the archive's lower-case "data" lands in the game's own "Data"
        QCOMPARE(listTree(staging), QStringList({ "Data/Scripts/Actor.pex", "skse64_1_6_1170.dll", "skse64_loader.exe" }));
    }

    void test_generic_game_keeps_real_folders()
    {
        QTemporaryDir dir;
        GameDefinition game;
        game.domain = "cyberpunk2077";
        game.executable = "bin/x64/Cyberpunk2077.exe";
        const auto gameDir = FS::PathCombine(dir.path(), "game");
        writeFile(FS::PathCombine(gameDir, "archive/pc/content/basegame.archive"));
        const auto extracted = FS::PathCombine(dir.path(), "extracted");
        writeFile(FS::PathCombine(extracted, "archive/pc/mod/cool.archive"));
        const auto analysis = ModInstaller::analyze(extracted, game, gameDir);
        // "archive" exists in the game, so it is not mistaken for a wrapper folder
        QCOMPARE(analysis.contentRoot, extracted);
        const auto staging = FS::PathCombine(dir.path(), "staging");
        QVERIFY(ModInstaller::stage(analysis, game, staging).has_value());
        QCOMPARE(listTree(staging), QStringList({ "archive/pc/mod/cool.archive" }));
    }

    void test_fomod_staging_resolves_case_and_rejects_escapes()
    {
        QTemporaryDir dir;
        const auto extracted = FS::PathCombine(dir.path(), "extracted");
        writeFile(FS::PathCombine(extracted, "Mod/fomod/ModuleConfig.xml"), g_config);
        writeFile(FS::PathCombine(extracted, "Mod/fomod/info.xml"), "<fomod><Name>Info Name</Name><Version>3.1</Version></fomod>");
        writeFile(FS::PathCombine(extracted, "Mod/00 core/Core.esp"));
        writeFile(FS::PathCombine(extracted, "Mod/10 2K/Textures/sky.dds"));
        const auto analysis = ModInstaller::analyze(extracted, skyrim(), FS::PathCombine(dir.path(), "game"));
        QVERIFY(!analysis.fomodConfig.isEmpty());
        QCOMPARE(analysis.name, QString("Info Name"));
        QCOMPARE(analysis.version, QString("3.1"));

        QList<Fomod::FileItem> files{ { .source = "00 Core", .destination = "", .isFolder = true },
                                      { .source = "10 2K/textures", .destination = "textures", .isFolder = true } };
        const auto staging = FS::PathCombine(dir.path(), "staging");
        QVERIFY(ModInstaller::stageFomod(files, analysis, skyrim(), staging).has_value());
        QCOMPARE(listTree(staging), QStringList({ "Data/Core.esp", "Data/textures/sky.dds" }));

        QList<Fomod::FileItem> hostile{ { .source = "00 Core/Core.esp", .destination = "../../evil.esp" } };
        QVERIFY(!ModInstaller::stageFomod(hostile, analysis, skyrim(), FS::PathCombine(dir.path(), "staging2")).has_value());
        QVERIFY(!QFileInfo::exists(FS::PathCombine(dir.path(), "evil.esp")));
        QList<Fomod::FileItem> missing{ { .source = "nope.esp", .destination = "nope.esp" } };
        QVERIFY(!ModInstaller::stageFomod(missing, analysis, skyrim(), FS::PathCombine(dir.path(), "staging3")).has_value());
    }

    void test_damaged_archives_are_errors_not_short_archives()
    {
        QTemporaryDir dir;
        const auto whole = FS::PathCombine(dir.path(), "whole.zip");
        {
            MMCZip::ArchiveWriter writer(whole);
            QVERIFY(writer.open().has_value());
            for (const auto* name : { "Data/One.esp", "Data/textures/two.dds", "Data/meshes/three.nif" }) {
                QByteArray content;
                for (int i = 0; i < 20000; i++) {
                    content += QByteArray::number(i * 7919 % 100003);  // does not compress to nothing
                }
                QVERIFY(writer.addFile(name, content).has_value());
            }
            QVERIFY(writer.close().has_value());
        }
        MMCZip::ArchiveReader intact(whole);
        QVERIFY(intact.collectFiles().has_value());
        QCOMPARE(intact.getFiles().size(), 3);

        // a download cut off in the middle of the second file
        QFile in(whole);
        QVERIFY(in.open(QIODevice::ReadOnly));
        const auto bytes = in.readAll();
        in.close();
        const auto cut = FS::PathCombine(dir.path(), "cut.zip");
        QFile out(cut);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(bytes.left(bytes.size() / 2));
        out.close();
        MMCZip::ArchiveReader truncated(cut);
        QVERIFY(!truncated.collectFiles().has_value());

        const auto garbage = FS::PathCombine(dir.path(), "garbage.zip");
        QFile junk(garbage);
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write(QByteArray("PK\x03\x04", 4) + QByteArray(300, 'Z'));
        junk.close();
        MMCZip::ArchiveReader broken(garbage);
        const auto brokenResult = broken.collectFiles();
        QVERIFY(!brokenResult.has_value());
        QVERIFY2(brokenResult.error().contains("garbage.zip is damaged or incomplete"), qPrintable(brokenResult.error()));
        QVERIFY(!broken.readFile("Data/One.esp").has_value());
    }
};

QTEST_GUILESS_MAIN(ModInstallTest)

#include "ModInstall_test.moc"
