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
#include <QImage>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

#include <FileSystem.h>
#include <game/Deployer.h>
#include <game/ToolOutput.h>
#include <game/GameFiles.h>
#include <game/GameInstanceCreationTask.h>
#include <game/PatternMatcher.h>

// moc can not read raw string literals, so the fixtures live where it never looks
#ifndef Q_MOC_RUN
#include "SteamFixtures.h"
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

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
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

bool runTask(Task& task)
{
    QSignalSpy finished(&task, &Task::finished);
    task.start();
    if (!task.isFinished() && !finished.wait(30000)) {
        return false;
    }
    return task.wasSuccessful();
}

}  // namespace

class GameFilesTest : public QObject {
    Q_OBJECT

   private slots:
    void test_patterns()
    {
        const PatternMatcher local(GameFiles::splitPatterns(" *.exe; *.DLL ;Data/SKSE/* ;"));
        QVERIFY(local.matches("SkyrimSE.exe"));
        QVERIFY(local.matches("bin/x64/game.dll"));
        QVERIFY(local.matches("Data/SKSE/Plugins/thing.ini"));
        QVERIFY(local.matches("data/skse/plugins/thing.ini"));
        QVERIFY(!local.matches("Data/Skyrim.esm"));
        QVERIFY(!local.matches("Data/SKSE.txt"));
        QVERIFY(!PatternMatcher({}).matches("anything"));
    }

    void test_place_link_and_copy()
    {
        QTemporaryDir dir;
        const auto src = FS::PathCombine(dir.path(), "src/a.bsa");
        writeFile(src, "archive");

        const auto linked = FS::PathCombine(dir.path(), "game/a.bsa");
        auto mode = GameFiles::placeFile(src, linked, GameFiles::PlaceMode::Link);
        QVERIFY(mode.has_value());
        QCOMPARE(*mode, GameFiles::PlaceMode::Link);
        QVERIFY(GameFiles::isSameFile(src, linked));

        const auto copied = FS::PathCombine(dir.path(), "game/b.bsa");
        mode = GameFiles::placeFile(src, copied, GameFiles::PlaceMode::Copy);
        QVERIFY(mode.has_value());
        QCOMPARE(*mode, GameFiles::PlaceMode::Copy);
        QVERIFY(!GameFiles::isSameFile(src, copied));
        QCOMPARE(readFile(copied), QByteArray("archive"));
        QVERIFY(GameFiles::isSameVolume(src, copied));
    }

    void test_remove_readonly_link_keeps_other_link()
    {
        QTemporaryDir dir;
        const auto src = FS::PathCombine(dir.path(), "steam/Skyrim.esm");
        writeFile(src, "master");
        QFile(src).setPermissions(QFile::ReadOwner | QFile::ReadUser);
        const auto link = FS::PathCombine(dir.path(), "instance/Skyrim.esm");
        QVERIFY(GameFiles::placeFile(src, link, GameFiles::PlaceMode::Link).has_value());

        QVERIFY(GameFiles::removeFile(link).has_value());
        QVERIFY(!QFileInfo::exists(link));
        // the Steam side keeps its file and its read-only attribute
        QCOMPARE(readFile(src), QByteArray("master"));
        QVERIFY(!QFileInfo(src).isWritable());
        QVERIFY(GameFiles::removeFile(link).has_value());  // gone already: fine
        QFile(src).setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    }

    void test_removeTree_never_follows_junctions()
    {
#ifdef Q_OS_WIN
        QTemporaryDir dir;
        const auto precious = FS::PathCombine(dir.path(), "precious/save.ess");
        writeFile(precious, "save");
        const auto tree = FS::PathCombine(dir.path(), "instance");
        writeFile(FS::PathCombine(tree, "file.txt"), "x");
        const auto junction = QDir::toNativeSeparators(FS::PathCombine(tree, "junction"));
        const auto target = QDir::toNativeSeparators(FS::PathCombine(dir.path(), "precious"));
        QCOMPARE(QProcess::execute("cmd", { "/c", "mklink", "/J", junction, target }), 0);
        QVERIFY(QFileInfo::exists(FS::PathCombine(tree, "junction/save.ess")));

        QVERIFY(GameFiles::removeTree(tree).has_value());
        QVERIFY(!QFileInfo::exists(tree));
        QCOMPARE(readFile(precious), QByteArray("save"));
#endif
    }

    void test_removeTreeWhole_deletes_nothing_while_in_use()
    {
        QTemporaryDir dir;
        const auto instance = FS::PathCombine(dir.path(), "instances/Skyrim");
        writeFile(FS::PathCombine(instance, "instance.cfg"), "cfg");
        writeFile(FS::PathCombine(instance, "game/Data/Skyrim.esm"), "base");
        writeFile(FS::PathCombine(instance, "profile/Saves/save1.ess"), "save");
        writeFile(FS::PathCombine(instance, "profile/tool.log"), "log");
        {
            // a tool writing its log, deep in the folder: a plain delete would stop there with the rest gone
            QFile open(FS::PathCombine(instance, "profile/tool.log"));
            QVERIFY(open.open(QIODevice::ReadOnly));
            const auto removed = GameFiles::removeTreeWhole(instance);
            QVERIFY(!removed.has_value());
            QCOMPARE(readFile(FS::PathCombine(instance, "profile/Saves/save1.ess")), QByteArray("save"));
            QCOMPARE(readFile(FS::PathCombine(instance, "game/Data/Skyrim.esm")), QByteArray("base"));
            QCOMPARE(readFile(FS::PathCombine(instance, "instance.cfg")), QByteArray("cfg"));
            QVERIFY2(removed.error().contains("Nothing was deleted"), qPrintable(removed.error()));
            // deploying over a file in use says so in words, with the path as the user knows it
            const auto inUse = GameFiles::removeFile(FS::PathCombine(instance, "profile/tool.log"));
            QVERIFY(!inUse.has_value());
            QVERIFY2(inUse.error().contains("a program has it open"), qPrintable(inUse.error()));
            QVERIFY2(inUse.error().contains(QDir::toNativeSeparators(FS::PathCombine(instance, "profile/tool.log"))), qPrintable(inUse.error()));
            QVERIFY(!QFileInfo::exists(FS::PathCombine(dir.path(), "instances/.deleting")));
        }
        QVERIFY(GameFiles::removeTreeWhole(instance).has_value());
        QVERIFY(!QFileInfo::exists(instance));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(dir.path(), "instances/.deleting")));
        QVERIFY(QFileInfo::exists(FS::PathCombine(dir.path(), "instances")));

        // a mod whose file the running game has open through its own hard link in the game folder: the mod's folder
        // still goes, so a new version can take its place, and the game keeps reading the old file
        const auto mod = FS::PathCombine(dir.path(), "mods/Big Textures");
        writeFile(FS::PathCombine(mod, "Data/Textures.bsa"), "old textures");
        const auto deployed = FS::PathCombine(dir.path(), "game/Data/Textures.bsa");
        QVERIFY(GameFiles::placeFile(FS::PathCombine(mod, "Data/Textures.bsa"), deployed, GameFiles::PlaceMode::Link).has_value());
#ifdef Q_OS_WIN
        {
            // the way a game reads its archives: no sharing for deletes
            HANDLE game = CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(deployed).utf16()), GENERIC_READ,
                                      FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            QVERIFY(game != INVALID_HANDLE_VALUE);
            const auto removed = GameFiles::removeTreeWhole(mod);
            CloseHandle(game);
            QVERIFY2(removed.has_value(), qPrintable(removed ? QString() : removed.error()));
            QVERIFY(!QFileInfo::exists(mod));
            QCOMPARE(readFile(deployed), QByteArray("old textures"));
        }

        // a file that can not be deleted does not stop the move: it waits in .deleting, and a later call deletes it
        const auto stubborn = FS::PathCombine(dir.path(), "mods/Stubborn");
        writeFile(FS::PathCombine(stubborn, "kept.txt"), "x");
        const auto native = [](const QString& path) { return QDir::toNativeSeparators(path); };
        QCOMPARE(QProcess::execute("icacls", { native(FS::PathCombine(stubborn, "kept.txt")), "/deny", "*S-1-1-0:(DE)" }), 0);
        QCOMPARE(QProcess::execute("icacls", { native(stubborn), "/deny", "*S-1-1-0:(DC)" }), 0);
        QVERIFY(GameFiles::removeTreeWhole(stubborn).has_value());
        QVERIFY(!QFileInfo::exists(stubborn));
        QStringList parked;
        for (const auto& entry : QDirListing(FS::PathCombine(dir.path(), "mods/.deleting"), QDirListing::IteratorFlag::Recursive)) {
            parked << entry.absoluteFilePath();
        }
        QCOMPARE(parked.size(), 2);
        for (const auto& path : parked) {
            QCOMPARE(QProcess::execute("icacls", { native(path), "/remove:d", "*S-1-1-0" }), 0);
        }
        writeFile(FS::PathCombine(mod, "Data/Textures.bsa"), "new textures");
        QVERIFY(GameFiles::removeTreeWhole(mod).has_value());
        QVERIFY(!QFileInfo::exists(FS::PathCombine(dir.path(), "mods/.deleting")));
#endif
    }

    void test_file_version()
    {
        const auto notepad = FS::PathCombine(QDir::fromNativeSeparators(qEnvironmentVariable("SystemRoot")), "System32/notepad.exe");
        const auto version = GameFiles::fileVersion(notepad);
        QVERIFY2(QRegularExpression("^\\d+\\.\\d+\\.\\d+\\.\\d+$").match(version).hasMatch(), qPrintable(version));
        QTemporaryDir dir;
        writeFile(FS::PathCombine(dir.path(), "plain.txt"), "no version here");
        QVERIFY(GameFiles::fileVersion(FS::PathCombine(dir.path(), "plain.txt")).isEmpty());
        QVERIFY(GameFiles::fileVersion(FS::PathCombine(dir.path(), "missing.exe")).isEmpty());
    }

    void test_paths_beyond_260_characters()
    {
        // mod staging folders get deep quickly; this test executable is not long path aware, like many setups
        QTemporaryDir dir;
        auto deep = dir.path();
        while (deep.size() < 300) {
            deep = FS::PathCombine(deep, "a deeply nested mod folder");
        }
        const auto src = FS::PathCombine(deep, "source.bsa");
        writeFile(src, "data");
        const auto linked = FS::PathCombine(deep, "linked/textures.bsa");
        const auto copied = FS::PathCombine(deep, "copied/textures.bsa");
        QCOMPARE(GameFiles::placeFile(src, linked, GameFiles::PlaceMode::Link).value_or(GameFiles::PlaceMode::Copy), GameFiles::PlaceMode::Link);
        QVERIFY(GameFiles::isSameFile(src, linked));
        QCOMPARE(GameFiles::placeFile(src, copied, GameFiles::PlaceMode::Copy).value_or(GameFiles::PlaceMode::Link), GameFiles::PlaceMode::Copy);
        QCOMPARE(readFile(copied), QByteArray("data"));
        QVERIFY(GameFiles::removeFile(linked).has_value());
        QVERIFY(!QFileInfo::exists(linked));
        QVERIFY(QFileInfo::exists(src));
        QVERIFY(GameFiles::removeTree(FS::PathCombine(dir.path(), "a deeply nested mod folder")).has_value());
        QVERIFY(!QFileInfo::exists(src));
    }

    void test_cancelled_copy_leaves_nothing()
    {
        QTemporaryDir dir;
        const auto src = FS::PathCombine(dir.path(), "big.bsa");
        writeFile(src, QByteArray(4 * 1024 * 1024, 'x'));
        const std::atomic_bool cancel = true;
        const auto dst = FS::PathCombine(dir.path(), "copy/big.bsa");
        const auto placed = GameFiles::placeFile(src, dst, GameFiles::PlaceMode::Copy, &cancel);
        QVERIFY(!placed.has_value());
        QCOMPARE(placed.error(), QString("Cancelled"));
        QVERIFY(!QFileInfo::exists(dst));
        // without a cancel request the same copy goes through, and an existing file is never overwritten
        QVERIFY(GameFiles::placeFile(src, dst, GameFiles::PlaceMode::Copy).has_value());
        QCOMPARE(QFileInfo(dst).size(), qint64(4 * 1024 * 1024));
        QVERIFY(!GameFiles::placeFile(src, dst, GameFiles::PlaceMode::Copy).has_value());
    }

    void test_base_sync_failure_and_abort_keep_what_was_placed()
    {
        QTemporaryDir dir;
        const auto source = FS::PathCombine(dir.path(), "steam");
        const auto instance = FS::PathCombine(dir.path(), "instance");
        for (int i = 0; i < 20; i++) {
            writeFile(FS::PathCombine(source, QString("Data/file%1.bsa").arg(i)), "bsa");
        }
        // sorts after the .bsa files, so the sync fails after placing them
        writeFile(FS::PathCombine(source, "Data/zLocked.ini"), "ini");

        GameFiles::BaseSyncTask::Params params;
        params.sourceDir = source;
        params.gameDir = FS::PathCombine(instance, "game");
        params.manifestPath = FS::PathCombine(instance, "base.json");
        params.overwrittenDir = FS::PathCombine(instance, "overwritten");
        params.localPatterns = { "*.ini" };

        {
            // the copied file can not be read, so the sync fails part way
            const auto locked = QDir::toNativeSeparators(FS::PathCombine(source, "Data/zLocked.ini")).toStdWString();
            HANDLE handle = CreateFileW(locked.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            QVERIFY(handle != INVALID_HANDLE_VALUE);
            GameFiles::BaseSyncTask failing(params);
            const bool ok = runTask(failing);
            CloseHandle(handle);
            QVERIFY(!ok);
        }
        // every file placed before the failure is recorded as the game's own
        const auto manifest = GameFiles::BaseManifest::load(params.manifestPath);
        QVERIFY(manifest.has_value());
        for (const auto& entry : QDirListing(params.gameDir, QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly)) {
            QVERIFY2(manifest->files.contains(QDir(params.gameDir).relativeFilePath(entry.absoluteFilePath())), qPrintable(entry.fileName()));
        }

        GameFiles::BaseSyncTask aborted(params);
        QSignalSpy abortedSpy(&aborted, &Task::aborted);
        QVERIFY(aborted.abort());
        QVERIFY(!runTask(aborted));
        QCOMPARE(abortedSpy.count(), 1);

        GameFiles::BaseSyncTask retry(params);
        QVERIFY2(runTask(retry), qPrintable(retry.failReason()));
        for (const auto& message : retry.messages()) {
            QVERIFY2(!message.startsWith("Kept"), qPrintable(message));
        }
        for (int i = 0; i < 20; i++) {
            const auto rel = QString("Data/file%1.bsa").arg(i);
            QVERIFY(GameFiles::isSameFile(FS::PathCombine(source, rel), FS::PathCombine(params.gameDir, rel)));
        }
        QCOMPARE(readFile(FS::PathCombine(params.gameDir, "Data/zLocked.ini")), QByteArray("ini"));
    }

    void test_base_sync_links_copies_updates_and_keeps_changes()
    {
        QTemporaryDir dir;
        const auto source = FS::PathCombine(dir.path(), "steam");
        const auto instance = FS::PathCombine(dir.path(), "instance");
        writeFile(FS::PathCombine(source, "Game.exe"), "exe v1");
        writeFile(FS::PathCombine(source, "Data/Main.bsa"), "bsa v1");
        writeFile(FS::PathCombine(source, "Data/Old.bsa"), "old");
        writeFile(FS::PathCombine(source, "crash.dmp"), "dump");
        writeFile(FS::PathCombine(source, "Data/SKSE/Plugins/mod.dll"), "deployed by vortex");

        GameFiles::BaseSyncTask::Params params;
        params.sourceDir = source;
        params.gameDir = FS::PathCombine(instance, "game");
        params.manifestPath = FS::PathCombine(instance, "base.json");
        params.overwrittenDir = FS::PathCombine(instance, "overwritten");
        params.localPatterns = { "*.exe" };
        params.excludePatterns = { "*.dmp" };
        params.knownModFiles = { "data/skse/plugins/mod.dll" };

        GameFiles::BaseSyncTask first(params);
        QVERIFY(runTask(first));
        const auto game = params.gameDir;
        QVERIFY(GameFiles::isSameFile(FS::PathCombine(source, "Data/Main.bsa"), FS::PathCombine(game, "Data/Main.bsa")));
        QVERIFY(!GameFiles::isSameFile(FS::PathCombine(source, "Game.exe"), FS::PathCombine(game, "Game.exe")));
        QCOMPARE(readFile(FS::PathCombine(game, "Game.exe")), QByteArray("exe v1"));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "crash.dmp")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "Data/SKSE/Plugins/mod.dll")));
        QCOMPARE(first.linkedCount(), 2);
        QCOMPARE(first.copiedCount(), 1);
        // not a Steam installation: every file is used, and the user is told so
        QCOMPARE(first.warnings().size(), 1);
        QVERIFY(first.messages().contains(first.warnings().first()));

        // Steam updates: it replaces files instead of writing into them
        QVERIFY(QFile::remove(FS::PathCombine(source, "Data/Main.bsa")));
        writeFile(FS::PathCombine(source, "Data/Main.bsa"), "bsa v2");
        QVERIFY(QFile::remove(FS::PathCombine(source, "Data/Old.bsa")));
        writeFile(FS::PathCombine(source, "Data/New.bsa"), "new");
        // the user patched the instance's executable
        QVERIFY(QFile::remove(FS::PathCombine(game, "Game.exe")));
        writeFile(FS::PathCombine(game, "Game.exe"), "patched exe");

        GameFiles::BaseSyncTask second(params);
        QVERIFY(runTask(second));
        QCOMPARE(readFile(FS::PathCombine(game, "Data/Main.bsa")), QByteArray("bsa v2"));
        QVERIFY(GameFiles::isSameFile(FS::PathCombine(source, "Data/Main.bsa"), FS::PathCombine(game, "Data/Main.bsa")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "Data/Old.bsa")));
        QVERIFY(QFileInfo::exists(FS::PathCombine(game, "Data/New.bsa")));
        QCOMPARE(readFile(FS::PathCombine(game, "Game.exe")), QByteArray("patched exe"));
        // the Steam installation was only read
        QCOMPARE(readFile(FS::PathCombine(source, "Game.exe")), QByteArray("exe v1"));
        QVERIFY(QFileInfo::exists(FS::PathCombine(source, "crash.dmp")));
    }

    void test_linked_local_files_become_private_copies()
    {
        QTemporaryDir dir;
        const auto source = FS::PathCombine(dir.path(), "steam");
        writeFile(FS::PathCombine(source, "FalloutNV.exe"), "exe v1");
        writeFile(FS::PathCombine(source, "Data/FalloutNV.esm"), "esm");
        writeFile(FS::PathCombine(source, "Data/Fallout - Meshes.bsa"), "bsa");
        GameFiles::BaseSyncTask::Params params;
        params.sourceDir = source;
        params.gameDir = FS::PathCombine(dir.path(), "instance/game");
        params.manifestPath = FS::PathCombine(dir.path(), "instance/base.json");
        params.overwrittenDir = FS::PathCombine(dir.path(), "instance/overwritten");
        // an instance made before local copies were set: everything is a link to the Steam installation
        GameFiles::BaseSyncTask sync(params);
        QVERIFY(runTask(sync));
        QVERIFY(GameFiles::isSameFile(FS::PathCombine(source, "FalloutNV.exe"), FS::PathCombine(params.gameDir, "FalloutNV.exe")));
        // a mod replaced the master: the instance's own link to it waits in the overwritten folder
        QVERIFY(GameFiles::moveFile(FS::PathCombine(params.gameDir, "Data/FalloutNV.esm"), FS::PathCombine(params.overwrittenDir, "Data/FalloutNV.esm"))
                    .has_value());
        writeFile(FS::PathCombine(params.gameDir, "Data/FalloutNV.esm"), "mod's esm");

        const QStringList patterns{ "*.exe", "*.esm" };
        auto copied = GameFiles::unshareLocalFiles(params.manifestPath, params.gameDir, params.overwrittenDir, { "data/falloutnv.esm" }, patterns);
        QVERIFY2(copied.has_value(), qPrintable(copied ? QString() : copied.error()));
        QCOMPARE(*copied, 2);
        const auto exe = FS::PathCombine(params.gameDir, "FalloutNV.exe");
        QVERIFY(!GameFiles::isSameFile(FS::PathCombine(source, "FalloutNV.exe"), exe));
        QCOMPARE(readFile(exe), QByteArray("exe v1"));
        QVERIFY(!GameFiles::isSameFile(FS::PathCombine(source, "Data/FalloutNV.esm"), FS::PathCombine(params.overwrittenDir, "Data/FalloutNV.esm")));
        QCOMPARE(readFile(FS::PathCombine(params.gameDir, "Data/FalloutNV.esm")), QByteArray("mod's esm"));
        // archives stay links: they take the space
        QVERIFY(GameFiles::isSameFile(FS::PathCombine(source, "Data/Fallout - Meshes.bsa"), FS::PathCombine(params.gameDir, "Data/Fallout - Meshes.bsa")));
        QVERIFY(!QFileInfo::exists(exe + ".betternmm-copy"));

        // patching the copy in place (the 4GB patcher) leaves the Steam installation alone
        writeFile(exe, "exe patched");
        QCOMPARE(readFile(FS::PathCombine(source, "FalloutNV.exe")), QByteArray("exe v1"));
        // known as copies now: nothing to do the next time, and an update keeps the patched file as the instance's own
        copied = GameFiles::unshareLocalFiles(params.manifestPath, params.gameDir, params.overwrittenDir, { "data/falloutnv.esm" }, patterns);
        QVERIFY(copied.has_value());
        QCOMPARE(*copied, 0);
        params.localPatterns = patterns;
        params.deployedFiles = { "data/falloutnv.esm" };
        GameFiles::BaseSyncTask update(params);
        QVERIFY(runTask(update));
        QCOMPARE(readFile(exe), QByteArray("exe patched"));
        QCOMPARE(readFile(FS::PathCombine(source, "FalloutNV.exe")), QByteArray("exe v1"));
    }

    void test_base_sync_checks_free_space_before_copying()
    {
        QTemporaryDir dir;
        const auto source = FS::PathCombine(dir.path(), "steam");
        writeFile(FS::PathCombine(source, "Game.exe"), QByteArray(4096, 'x'));
        writeFile(FS::PathCombine(source, "Data/Main.bsa"), "bsa");
        GameFiles::BaseSyncTask::Params params;
        params.sourceDir = source;
        params.gameDir = FS::PathCombine(dir.path(), "instance/game");
        params.manifestPath = FS::PathCombine(dir.path(), "instance/base.json");
        params.overwrittenDir = FS::PathCombine(dir.path(), "instance/overwritten");
        params.localPatterns = { "*.exe" };
        // a drive that would end up fuller than allowed: nothing is copied, nothing linked
        params.reserveBytes = std::numeric_limits<qint64>::max() / 2;
        GameFiles::BaseSyncTask full(params);
        QSignalSpy failed(&full, &Task::failed);
        QVERIFY(!runTask(full));
        QVERIFY2(failed.first().first().toString().contains("Nothing was changed"), qPrintable(failed.first().first().toString()));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(params.gameDir, "Game.exe")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(params.gameDir, "Data/Main.bsa")));
        QVERIFY(!QFileInfo::exists(params.manifestPath));

        // only links: no space needed, however full the drive
        params.localPatterns = {};
        GameFiles::BaseSyncTask linksOnly(params);
        QVERIFY(runTask(linksOnly));
        QCOMPARE(linksOnly.copiedCount(), 0);

        // the copies already in place need no room again
        params.localPatterns = { "*.exe" };
        params.reserveBytes = 0;
        QVERIFY(GameFiles::removeFile(FS::PathCombine(params.gameDir, "Game.exe")).has_value());
        GameFiles::BaseSyncTask copied(params);
        QVERIFY(runTask(copied));
        params.reserveBytes = std::numeric_limits<qint64>::max() / 2;
        GameFiles::BaseSyncTask update(params);
        QVERIFY(runTask(update));
    }

    void test_base_sync_never_empties_the_instance_when_the_game_is_gone()
    {
        QTemporaryDir dir;
        const auto source = FS::PathCombine(dir.path(), "steam");
        const auto instance = FS::PathCombine(dir.path(), "instance");
        writeFile(FS::PathCombine(source, "Game.exe"), "exe");
        writeFile(FS::PathCombine(source, "Data/Main.bsa"), "bsa");

        GameFiles::BaseSyncTask::Params params;
        params.sourceDir = source;
        params.gameDir = FS::PathCombine(instance, "game");
        params.manifestPath = FS::PathCombine(instance, "base.json");
        params.overwrittenDir = FS::PathCombine(instance, "overwritten");
        GameFiles::BaseSyncTask first(params);
        QVERIFY(runTask(first));
        const auto manifest = readFile(params.manifestPath);

        // the drive is unplugged, or the game was moved or uninstalled: the instance's links may be its last copy
        QVERIFY(QDir(source).rename(source, source + " (unplugged)"));
        GameFiles::BaseSyncTask second(params);
        QSignalSpy failed(&second, &Task::failed);
        QVERIFY(!runTask(second));
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed.first().first().toString().contains("is not there"));
        QCOMPARE(readFile(FS::PathCombine(params.gameDir, "Game.exe")), QByteArray("exe"));
        QCOMPARE(readFile(FS::PathCombine(params.gameDir, "Data/Main.bsa")), QByteArray("bsa"));
        QCOMPARE(readFile(params.manifestPath), manifest);

        // uninstalled, with only leftovers in the folder: the same
        QVERIFY(QDir(source + " (unplugged)").removeRecursively());
        writeFile(FS::PathCombine(source, "crash.dmp"), "left behind");
        params.excludePatterns = { "*.dmp" };
        GameFiles::BaseSyncTask third(params);
        QSignalSpy thirdFailed(&third, &Task::failed);
        QVERIFY(!runTask(third));
        QVERIFY(thirdFailed.first().first().toString().contains("has no game files"));
        QCOMPARE(readFile(FS::PathCombine(params.gameDir, "Game.exe")), QByteArray("exe"));
        QCOMPARE(readFile(params.manifestPath), manifest);

        // creating an instance from such a folder fails too, instead of making an empty game folder
        params.gameDir = FS::PathCombine(dir.path(), "other/game");
        params.manifestPath = FS::PathCombine(dir.path(), "other/base.json");
        GameFiles::BaseSyncTask create(params);
        QVERIFY(!runTask(create));
        QVERIFY(!QFileInfo::exists(params.manifestPath));
    }

    void test_base_sync_keeps_files_steam_lists_but_the_folder_lacks()
    {
        // a Steam folder with one app whose cached file list names Game.exe, Data/Main.bsa and Data/Gone.bsa
        QTemporaryDir dir;
        const auto steam = FS::PathCombine(dir.path(), "Steam");
        const auto source = FS::PathCombine(steam, "steamapps/common/Fake Game");
        writeFile(FS::PathCombine(steam, "steamapps/appmanifest_100.acf"),
                  "\"AppState\" { \"appid\" \"100\" \"name\" \"Fake Game\" \"installdir\" \"Fake Game\" "
                  "\"InstalledDepots\" { \"101\" { \"manifest\" \"555\" } } }");
        const QByteArray payload = fileMapping("Game.exe", 3, 0) + fileMapping("Data\\Main.bsa", 3, 0) + fileMapping("Data\\Gone.bsa", 4, 0) +
                                   fileMapping("Data", 0, 64);
        const QByteArray metadata = field(1, 0) + varint(101) + field(4, 0) + varint(0);
        writeFile(FS::PathCombine(steam, "depotcache/101_555.manifest"),
                  section(0x71F617D0, payload) + section(0x1F4812BE, metadata) + section(0x1B81B817, QByteArray(32, 's')) +
                      QByteArray("\xab\x15\xc4\x32", 4));
        writeFile(FS::PathCombine(source, "Game.exe"), "exe");
        writeFile(FS::PathCombine(source, "Data/Main.bsa"), "bsa");
        writeFile(FS::PathCombine(source, "Data/Gone.bsa"), "gone");
        writeFile(FS::PathCombine(source, "Data/Mod.esp"), "another manager's mod");

        GameFiles::BaseSyncTask::Params params;
        params.sourceDir = source;
        params.steamRoot = steam;
        params.gameDir = FS::PathCombine(dir.path(), "instance/game");
        params.manifestPath = FS::PathCombine(dir.path(), "instance/base.json");
        params.overwrittenDir = FS::PathCombine(dir.path(), "instance/overwritten");
        GameFiles::BaseSyncTask first(params);
        QVERIFY(runTask(first));
        QVERIFY(QFileInfo::exists(FS::PathCombine(params.gameDir, "Data/Gone.bsa")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(params.gameDir, "Data/Mod.esp")));  // only what Steam lists

        // the file disappears from the Steam folder while Steam still lists it (verify the game files brings it back)
        QVERIFY(QFile::remove(FS::PathCombine(source, "Data/Gone.bsa")));
        GameFiles::BaseSyncTask second(params);
        QVERIFY(runTask(second));
        QCOMPARE(readFile(FS::PathCombine(params.gameDir, "Data/Gone.bsa")), QByteArray("gone"));
        QVERIFY(second.warnings().join(' ').contains("missing from the Steam folder"));

        // a file Steam no longer lists (a game update removed it) does leave the instance
        QVERIFY(QFile::remove(FS::PathCombine(source, "Data/Main.bsa")));
        const QByteArray updated = fileMapping("Game.exe", 3, 0) + fileMapping("Data\\Gone.bsa", 4, 0) + fileMapping("Data", 0, 64);
        writeFile(FS::PathCombine(steam, "depotcache/101_555.manifest"),
                  section(0x71F617D0, updated) + section(0x1F4812BE, metadata) + section(0x1B81B817, QByteArray(32, 's')) +
                      QByteArray("\xab\x15\xc4\x32", 4));
        GameFiles::BaseSyncTask third(params);
        QVERIFY(runTask(third));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(params.gameDir, "Data/Main.bsa")));
        QCOMPARE(readFile(FS::PathCombine(params.gameDir, "Data/Gone.bsa")), QByteArray("gone"));
    }

    void test_deploy_adopts_its_own_unrecorded_file()
    {
        QTemporaryDir dir;
        const auto game = FS::PathCombine(dir.path(), "game");
        const auto mods = FS::PathCombine(dir.path(), "mods");
        writeFile(FS::PathCombine(game, "Data/Skyrim.esm"), "base");
        writeFile(FS::PathCombine(mods, "C/Data/C Mod.esp"), "plugin C");
        // what an interrupted deploy leaves: the mod's file linked into the game, but not in the manifest
        QVERIFY(GameFiles::placeFile(FS::PathCombine(mods, "C/Data/C Mod.esp"), FS::PathCombine(game, "Data/C Mod.esp"),
                                     GameFiles::PlaceMode::Link)
                    .has_value());
        auto params = [&](QList<ModEntry> enabled) {
            return DeployTask::Params{ .gameDir = game,
                                       .modsDir = mods,
                                       .overwrittenDir = FS::PathCombine(dir.path(), "overwritten"),
                                       .manifestPath = FS::PathCombine(dir.path(), "deployment.json"),
                                       .enabledMods = std::move(enabled) };
        };
        DeployTask deploy(params({ ModEntry{ .id = "C", .name = "C", .enabled = true, .stamp = 1 } }));
        QVERIFY(runTask(deploy));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(dir.path(), "overwritten/Data/C Mod.esp")));
        auto manifest = DeploymentManifest::load(FS::PathCombine(dir.path(), "deployment.json"));
        QVERIFY(manifest.has_value());
        QCOMPARE(manifest->files.value("data/c mod.esp").mod, QString("C"));
        QVERIFY(!manifest->files.value("data/c mod.esp").displacedOriginal);

        // removing the mod takes its file away instead of restoring a copy as if it belonged to the game
        DeployTask removed(params({}));
        QVERIFY(runTask(removed));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "Data/C Mod.esp")));
        QCOMPARE(readFile(FS::PathCombine(game, "Data/Skyrim.esm")), QByteArray("base"));

        // what overlapping deploys left in instances already: the mod's own file set aside as the game's original
        DeployTask again(params({ ModEntry{ .id = "C", .name = "C", .enabled = true, .stamp = 1 } }));
        QVERIFY(runTask(again));
        const auto stray = FS::PathCombine(dir.path(), "overwritten/Data/C Mod.esp");
        QVERIFY(GameFiles::placeFile(FS::PathCombine(mods, "C/Data/C Mod.esp"), stray, GameFiles::PlaceMode::Link).has_value());
        auto broken = DeploymentManifest::load(FS::PathCombine(dir.path(), "deployment.json"));
        QVERIFY(broken.has_value());
        broken->files["data/c mod.esp"].displacedOriginal = true;
        QVERIFY(broken->save(FS::PathCombine(dir.path(), "deployment.json")).has_value());
        // and a real original of another file stays put
        writeFile(FS::PathCombine(mods, "C/Data/Skyrim.esm"), "patched base");
        DeployTask over(params({ ModEntry{ .id = "C", .name = "C", .enabled = true, .stamp = 1 } }));
        QVERIFY(runTask(over));
        QCOMPARE(readFile(FS::PathCombine(dir.path(), "overwritten/Data/Skyrim.esm")), QByteArray("base"));

        DeployTask healed(params({ ModEntry{ .id = "C", .name = "C", .enabled = true, .stamp = 1 } }));
        QVERIFY(runTask(healed));
        QVERIFY(!QFileInfo::exists(stray));
        DeployTask gone(params({}));
        QVERIFY(runTask(gone));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "Data/C Mod.esp")));
        QCOMPARE(readFile(FS::PathCombine(game, "Data/Skyrim.esm")), QByteArray("base"));
    }

    void test_deploy_overrides_restores_and_purges()
    {
        QTemporaryDir dir;
        const auto game = FS::PathCombine(dir.path(), "game");
        const auto mods = FS::PathCombine(dir.path(), "mods");
        writeFile(FS::PathCombine(game, "Data/textures/sky.dds"), "vanilla sky");
        writeFile(FS::PathCombine(mods, "A/Data/textures/sky.dds"), "sky from A");
        writeFile(FS::PathCombine(mods, "A/Data/A.esp"), "plugin A");
        writeFile(FS::PathCombine(mods, "B/Data/textures/sky.dds"), "sky from B");
        writeFile(FS::PathCombine(mods, "B/skse64_loader.exe"), "loader");

        auto params = [&](QList<ModEntry> enabled) {
            return DeployTask::Params{ .gameDir = game,
                                       .modsDir = mods,
                                       .overwrittenDir = FS::PathCombine(dir.path(), "overwritten"),
                                       .manifestPath = FS::PathCombine(dir.path(), "deployment.json"),
                                       .enabledMods = std::move(enabled) };
        };
        const ModEntry a{ .id = "A", .name = "A", .enabled = true, .stamp = 1 };
        const ModEntry b{ .id = "B", .name = "B", .enabled = true, .stamp = 1 };

        DeployTask both(params({ a, b }));
        QVERIFY(runTask(both));
        QCOMPARE(readFile(FS::PathCombine(game, "Data/textures/sky.dds")), QByteArray("sky from B"));
        QVERIFY(GameFiles::isSameFile(FS::PathCombine(mods, "B/Data/textures/sky.dds"), FS::PathCombine(game, "Data/textures/sky.dds")));
        QCOMPARE(readFile(FS::PathCombine(game, "Data/A.esp")), QByteArray("plugin A"));
        QCOMPARE(readFile(FS::PathCombine(game, "skse64_loader.exe")), QByteArray("loader"));
        QCOMPARE(readFile(FS::PathCombine(dir.path(), "overwritten/Data/textures/sky.dds")), QByteArray("vanilla sky"));

        // B disabled: A's file wins, the loader goes
        DeployTask onlyA(params({ a }));
        QVERIFY(runTask(onlyA));
        QCOMPARE(readFile(FS::PathCombine(game, "Data/textures/sky.dds")), QByteArray("sky from A"));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "skse64_loader.exe")));

        // nothing enabled: the game is exactly as before
        DeployTask purge(params({}));
        QVERIFY(runTask(purge));
        QCOMPARE(readFile(FS::PathCombine(game, "Data/textures/sky.dds")), QByteArray("vanilla sky"));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "Data/A.esp")));
        auto manifest = DeploymentManifest::load(FS::PathCombine(dir.path(), "deployment.json"));
        QVERIFY(manifest.has_value());
        QVERIFY(manifest->files.isEmpty());
        QVERIFY(manifest->createdDirs.isEmpty());
        QStringList left;
        for (const auto& entry : QDirListing(game, QDirListing::IteratorFlag::Recursive)) {
            left << QDir(game).relativeFilePath(entry.absoluteFilePath());
        }
        left.sort();
        QCOMPARE(left, QStringList({ "Data", "Data/textures", "Data/textures/sky.dds" }));
        // the mods themselves are untouched
        QCOMPARE(readFile(FS::PathCombine(mods, "B/Data/textures/sky.dds")), QByteArray("sky from B"));
    }

    void test_deploy_keeps_files_changed_after_deployment()
    {
        QTemporaryDir dir;
        const auto game = FS::PathCombine(dir.path(), "game");
        const auto mods = FS::PathCombine(dir.path(), "mods");
        const auto overwritten = FS::PathCombine(dir.path(), "overwritten");
        const auto manifestPath = FS::PathCombine(dir.path(), "deployment.json");
        writeFile(FS::PathCombine(game, "Data/textures/sky.dds"), "vanilla sky");
        writeFile(FS::PathCombine(game, "bin/engine.dll"), "engine v1");
        writeFile(FS::PathCombine(mods, "A/Data/A.esp"), "plugin A");
        writeFile(FS::PathCombine(mods, "A/Data/textures/sky.dds"), "sky from A");
        writeFile(FS::PathCombine(mods, "A/bin/engine.dll"), "engine from A");
        writeFile(FS::PathCombine(mods, "A/Data/Ordered.esp"), "plugin ordered by time");
        auto params = [&](QList<ModEntry> enabled) {
            return DeployTask::Params{
                .gameDir = game, .modsDir = mods, .overwrittenDir = overwritten, .manifestPath = manifestPath, .enabledMods = std::move(enabled)
            };
        };
        const ModEntry a{ .id = "A", .name = "A", .enabled = true, .stamp = 1 };
        auto replace = [&](const QString& rel, const QByteArray& content) {
            // how xEdit saves and Steam updates: a new file takes the old one's place
            QVERIFY(GameFiles::removeFile(FS::PathCombine(game, rel)).has_value());
            writeFile(FS::PathCombine(game, rel), content);
        };

        DeployTask deploy(params({ a }));
        QVERIFY(runTask(deploy));
        auto manifest = DeploymentManifest::load(manifestPath);
        QVERIFY(manifest.has_value());
        QCOMPARE(manifest->files.value("data/a.esp").fileId, GameFiles::fileIndex(FS::PathCombine(game, "Data/A.esp")));
        QVERIFY(manifest->files.value("data/a.esp").fileId != 0);

        // what earlier versions deployed has no identities; the next deploy records them for the files still the mod's
        for (auto& file : manifest->files) {
            file.fileId = 0;
        }
        QVERIFY(manifest->save(manifestPath).has_value());
        DeployTask upgraded(params({ a }));
        QVERIFY(runTask(upgraded));
        manifest = DeploymentManifest::load(manifestPath);
        QVERIFY(manifest.has_value());
        for (const auto& file : std::as_const(manifest->files)) {
            QCOMPARE(file.fileId, GameFiles::fileIndex(FS::PathCombine(game, file.path)));
        }

        replace("Data/A.esp", "plugin A cleaned by xEdit");
        replace("bin/engine.dll", "engine v2 from a Steam update");
        // an edit through the link lands in the mod's own file, so taking the link away loses nothing
        writeFile(FS::PathCombine(game, "Data/textures/sky.dds"), "sky edited in place");
        // Oblivion and Fallout 3 order plugins by file time, which the load order sets: still the mod's file
        {
            QFile ordered(FS::PathCombine(game, "Data/Ordered.esp"));
            QVERIFY(ordered.open(QIODevice::ReadWrite));
            QVERIFY(ordered.setFileTime(QDateTime(QDate(2006, 3, 20), QTime(12, 0)), QFileDevice::FileModificationTime));
        }

        DeployTask purge(params({}));
        QVERIFY(runTask(purge));
        QCOMPARE(purge.kept(), 2);
        QCOMPARE(purge.warnings().size(), 2);
        QCOMPARE(readFile(FS::PathCombine(game, "Data/A.esp")), QByteArray("plugin A cleaned by xEdit"));
        QCOMPARE(readFile(FS::PathCombine(game, "bin/engine.dll")), QByteArray("engine v2 from a Steam update"));
        // the original is not put back over the newer file, and stays where the message says
        QCOMPARE(readFile(FS::PathCombine(overwritten, "bin/engine.dll")), QByteArray("engine v1"));
        QVERIFY(purge.warnings().join('\n').contains(QDir::toNativeSeparators(FS::PathCombine(overwritten, "bin/engine.dll"))));
        QCOMPARE(readFile(FS::PathCombine(game, "Data/textures/sky.dds")), QByteArray("vanilla sky"));
        QCOMPARE(readFile(FS::PathCombine(mods, "A/Data/textures/sky.dds")), QByteArray("sky edited in place"));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "Data/Ordered.esp")));
        manifest = DeploymentManifest::load(manifestPath);
        QVERIFY(manifest.has_value());
        QVERIFY(manifest->files.isEmpty());

        // enabled again, the mod's files go back in front and the kept files wait in the overwritten folder
        DeployTask again(params({ a }));
        QVERIFY(runTask(again));
        QCOMPARE(readFile(FS::PathCombine(game, "Data/A.esp")), QByteArray("plugin A"));
        QCOMPARE(readFile(FS::PathCombine(game, "bin/engine.dll")), QByteArray("engine from A"));
        QCOMPARE(readFile(FS::PathCombine(overwritten, "bin/engine.dll")), QByteArray("engine v2 from a Steam update"));
        DeployTask off(params({}));
        QVERIFY(runTask(off));
        QCOMPARE(off.kept(), 0);
        QCOMPARE(readFile(FS::PathCombine(game, "Data/A.esp")), QByteArray("plugin A cleaned by xEdit"));
        QCOMPARE(readFile(FS::PathCombine(game, "bin/engine.dll")), QByteArray("engine v2 from a Steam update"));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(overwritten, "bin/engine.dll")));
    }

    void test_changed_files_saved_into_mod_or_restored()
    {
        QTemporaryDir dir;
        const auto game = FS::PathCombine(dir.path(), "game");
        const auto mods = FS::PathCombine(dir.path(), "mods");
        const auto manifestPath = FS::PathCombine(dir.path(), "deployment.json");
        writeFile(FS::PathCombine(mods, "A/Data/A.esp"), "plugin A");
        writeFile(FS::PathCombine(mods, "A/Data/B.esp"), "plugin B");
        writeFile(FS::PathCombine(mods, "A/Data/C.esp"), "plugin C");
        const ModEntry a{ .id = "A", .name = "A", .enabled = true, .stamp = 1 };
        const DeployTask::Params params{
            .gameDir = game, .modsDir = mods, .overwrittenDir = FS::PathCombine(dir.path(), "overwritten"), .manifestPath = manifestPath, .enabledMods = { a }
        };
        DeployTask deploy(params);
        QVERIFY(runTask(deploy));
        auto found = DeployedChanges::find(manifestPath, game);
        QVERIFY(found.has_value() && found->isEmpty());

        // xEdit saves two of them; a third is deleted from the game folder
        for (const auto& name : { QString("A.esp"), QString("B.esp") }) {
            QVERIFY(GameFiles::removeFile(FS::PathCombine(game, "Data/" + name)).has_value());
            writeFile(FS::PathCombine(game, "Data/" + name), "cleaned " + name.toUtf8());
        }
        QVERIFY(GameFiles::removeFile(FS::PathCombine(game, "Data/C.esp")).has_value());
        found = DeployedChanges::find(manifestPath, game);
        QVERIFY(found.has_value());
        QStringList changed;
        for (const auto& file : *found) {
            changed << file.path + " " + file.mod;
        }
        changed.sort();
        QCOMPARE(changed, QStringList({ "Data/A.esp A", "Data/B.esp A" }));

        // saved into the mod: the mod has the cleaned plugin, and the game folder links to it again
        QVERIFY(DeployedChanges::saveIntoMod(manifestPath, game, mods, "Data/A.esp").has_value());
        QCOMPARE(readFile(FS::PathCombine(mods, "A/Data/A.esp")), QByteArray("cleaned A.esp"));
        QVERIFY(GameFiles::isSameFile(FS::PathCombine(mods, "A/Data/A.esp"), FS::PathCombine(game, "Data/A.esp")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(mods, "A/Data/A.esp.betternmm-new")));

        // restored: the changed file goes where it is told, the mod's file is back
        const auto discarded = FS::PathCombine(dir.path(), "discarded");
        auto discard = [&](const QString& file) { return GameFiles::moveFile(file, FS::PathCombine(discarded, QFileInfo(file).fileName())).has_value(); };
        QVERIFY(DeployedChanges::restore(manifestPath, game, mods, "Data/B.esp", discard).has_value());
        QCOMPARE(readFile(FS::PathCombine(game, "Data/B.esp")), QByteArray("plugin B"));
        QVERIFY(GameFiles::isSameFile(FS::PathCombine(mods, "A/Data/B.esp"), FS::PathCombine(game, "Data/B.esp")));
        QCOMPARE(readFile(FS::PathCombine(discarded, "B.esp")), QByteArray("cleaned B.esp"));
        // a file that is not changed is refused, and nothing happens to it
        QVERIFY(!DeployedChanges::saveIntoMod(manifestPath, game, mods, "Data/B.esp").has_value());
        QVERIFY(!DeployedChanges::restore(manifestPath, game, mods, "Data/A.esp", discard).has_value());
        QCOMPARE(readFile(FS::PathCombine(game, "Data/A.esp")), QByteArray("cleaned A.esp"));
        found = DeployedChanges::find(manifestPath, game);
        QVERIFY(found.has_value() && found->isEmpty());

        // the deleted file comes back with the next deploy; after that, disabling the mod takes all three away
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "Data/C.esp")));
        DeployTask again(params);
        QVERIFY(runTask(again));
        QCOMPARE(readFile(FS::PathCombine(game, "Data/C.esp")), QByteArray("plugin C"));
        auto off = params;
        off.enabledMods = {};
        DeployTask disabled(off);
        QVERIFY(runTask(disabled));
        QCOMPARE(disabled.kept(), 0);
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "Data/A.esp")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "Data/B.esp")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "Data/C.esp")));
        QCOMPARE(readFile(FS::PathCombine(mods, "A/Data/A.esp")), QByteArray("cleaned A.esp"));
    }

    void test_deploy_leaves_out_hidden_files()
    {
        QTemporaryDir dir;
        const auto game = FS::PathCombine(dir.path(), "game");
        const auto mods = FS::PathCombine(dir.path(), "mods");
        writeFile(FS::PathCombine(game, "Data/textures/sky.dds"), "vanilla sky");
        writeFile(FS::PathCombine(mods, "A/Data/textures/sky.dds"), "sky from A");
        writeFile(FS::PathCombine(mods, "B/Data/textures/sky.dds"), "sky from B");
        writeFile(FS::PathCombine(mods, "B/Data/B.esp"), "plugin B");
        ModList list(mods, FS::PathCombine(dir.path(), "mods.json"));
        QVERIFY(list.load().has_value());
        list.setOrder({ "A", "B" });
        list.setEnabled({ 0, 1 }, true);
        const auto deploy = [&] {
            DeployTask task(DeployTask::Params{ .gameDir = game,
                                                .modsDir = mods,
                                                .overwrittenDir = FS::PathCombine(dir.path(), "overwritten"),
                                                .manifestPath = FS::PathCombine(dir.path(), "deployment.json"),
                                                .enabledMods = list.enabledMods() });
            return runTask(task);
        };
        const auto sky = FS::PathCombine(game, "Data/textures/sky.dds");
        QVERIFY(deploy());
        QCOMPARE(readFile(sky), QByteArray("sky from B"));

        // B's copy hidden: A's wins; B's plugin leaves the game, and no hidden file goes in
        QVERIFY(list.setFilesHidden({ { "B", "Data/textures/sky.dds" }, { "B", "Data/B.esp" } }, true).has_value());
        QVERIFY(deploy());
        QCOMPARE(readFile(sky), QByteArray("sky from A"));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "Data/B.esp")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "Data/B.esp.mohidden")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "Data/textures/sky.dds.mohidden")));

        // every copy hidden: the game's own file is back
        QVERIFY(list.setFilesHidden({ { "A", "Data/textures/sky.dds" } }, true).has_value());
        QVERIFY(deploy());
        QCOMPARE(readFile(sky), QByteArray("vanilla sky"));

        // shown again: B wins again and the game's file is set aside once more
        QVERIFY(list.setFilesHidden({ { "B", "Data/textures/sky.dds" }, { "B", "Data/B.esp" } }, false).has_value());
        QVERIFY(deploy());
        QCOMPARE(readFile(sky), QByteArray("sky from B"));
        QCOMPARE(readFile(FS::PathCombine(game, "Data/B.esp")), QByteArray("plugin B"));
        QCOMPARE(readFile(FS::PathCombine(dir.path(), "overwritten/Data/textures/sky.dds")), QByteArray("vanilla sky"));
    }

    void test_tool_output_found_and_collected()
    {
        QTemporaryDir dir;
        const auto game = FS::PathCombine(dir.path(), "game");
        const auto mods = FS::PathCombine(dir.path(), "mods");
        writeFile(FS::PathCombine(game, "Data/Skyrim.esm"), "base");
        writeFile(FS::PathCombine(game, "Data/Tool/tool.exe"), "tool");
        QVERIFY(FS::ensureFolderPathExists(FS::PathCombine(game, "Data/Empty")));
        writeFile(FS::PathCombine(mods, "A/Data/textures/a.dds"), "A");
        writeFile(FS::PathCombine(mods, "A/Data/meshes/x.hkx"), "A x");
        ModList list(mods, FS::PathCombine(dir.path(), "mods.json"));
        QVERIFY(list.load().has_value());
        const auto manifestPath = FS::PathCombine(dir.path(), "deployment.json");
        const auto deploy = [&] {
            DeployTask task(DeployTask::Params{ .gameDir = game,
                                                .modsDir = mods,
                                                .overwrittenDir = FS::PathCombine(dir.path(), "overwritten"),
                                                .manifestPath = manifestPath,
                                                .enabledMods = list.enabledMods() });
            return runTask(task);
        };
        list.setEnabled({ list.indexOf("A") }, true);
        QVERIFY(deploy());
        const auto before = ToolOutput::take(game);
        QVERIFY(before.dirs.contains("data/empty"));
        QVERIFY(before.files.contains("data/skyrim.esm"));

        // what a tool does: new files (one in a new folder, one in a folder that was empty), a mod file written anew,
        // a mod file written into, a game file changed, its own cache and a log
        QTest::qWait(20);  // a later modification time than deployment's
        writeFile(FS::PathCombine(game, "Data/meshes/new/gen.hkx"), "generated");
        writeFile(FS::PathCombine(game, "Data/Empty/note.txt"), "note");
        QVERIFY(QFile::remove(FS::PathCombine(game, "Data/meshes/x.hkx")));
        writeFile(FS::PathCombine(game, "Data/meshes/x.hkx"), "tool x");
        {
            QFile inPlace(FS::PathCombine(game, "Data/textures/a.dds"));
            QVERIFY(inPlace.open(QIODevice::Append));
            inPlace.write(" edited");
        }
        writeFile(FS::PathCombine(game, "Data/Skyrim.esm"), "changed base");
        writeFile(FS::PathCombine(game, "Data/Tool/cache.bin"), "cache");
        writeFile(FS::PathCombine(game, "Data/run.log"), "log");

        auto manifest = DeploymentManifest::load(manifestPath);
        QVERIFY(manifest.has_value());
        const ToolOutput::Filter filter{ .excludePatterns = { "*.log" }, .ownDirs = { "Data/Tool" } };
        auto changes = ToolOutput::find(game, before, *manifest, filter, {});
        changes.created.sort();
        QCOMPARE(changes.created, (QStringList{ "Data/Empty/note.txt", "Data/meshes/new/gen.hkx" }));
        QCOMPARE(changes.replaced, QStringList{ "Data/meshes/x.hkx" });
        QCOMPARE(changes.changedInPlace, QStringList{ "Data/textures/a.dds" });
        QCOMPARE(changes.modOf.value("Data/textures/a.dds"), QString("A"));
        QCOMPARE(changes.changedGameFiles, QStringList{ "Data/Skyrim.esm" });

        const auto out = FS::PathCombine(mods, "Out");
        QVERIFY(ToolOutput::collect(game, before, changes, out).has_value());
        QCOMPARE(readFile(FS::PathCombine(out, "Data/meshes/new/gen.hkx")), QByteArray("generated"));
        QCOMPARE(readFile(FS::PathCombine(out, "Data/Empty/note.txt")), QByteArray("note"));
        QCOMPARE(readFile(FS::PathCombine(out, "Data/meshes/x.hkx")), QByteArray("tool x"));
        QCOMPARE(readFile(FS::PathCombine(out, "Data/textures/a.dds")), QByteArray("A edited"));
        // the tool's new folder went with its file; the folder that was there stays
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "Data/meshes/new")));
        QVERIFY(QFileInfo(FS::PathCombine(game, "Data/Empty")).isDir());
        QCOMPARE(readFile(FS::PathCombine(game, "Data/Tool/cache.bin")), QByteArray("cache"));
        QCOMPARE(readFile(FS::PathCombine(game, "Data/Skyrim.esm")), QByteArray("changed base"));
        // the mod's file the tool wrote anew is the mod's own again
        QCOMPARE(readFile(FS::PathCombine(mods, "A/Data/meshes/x.hkx")), QByteArray("A x"));

        // the output mod last: its files win
        QVERIFY(list.load().has_value());
        list.setOrder({ "A", "Out" });
        list.setEnabled({ list.indexOf("A"), list.indexOf("Out") }, true);
        QVERIFY(deploy());
        QVERIFY(GameFiles::isSameFile(FS::PathCombine(out, "Data/meshes/x.hkx"), FS::PathCombine(game, "Data/meshes/x.hkx")));
        QVERIFY(GameFiles::isSameFile(FS::PathCombine(out, "Data/textures/a.dds"), FS::PathCombine(game, "Data/textures/a.dds")));
        QVERIFY(GameFiles::isSameFile(FS::PathCombine(out, "Data/meshes/new/gen.hkx"), FS::PathCombine(game, "Data/meshes/new/gen.hkx")));

        // the tool runs again and writes into its own earlier output: that is its output already
        const auto again = ToolOutput::take(game);
        QTest::qWait(20);
        {
            QFile inPlace(FS::PathCombine(game, "Data/meshes/new/gen.hkx"));
            QVERIFY(inPlace.open(QIODevice::Append));
            inPlace.write(" again");
        }
        manifest = DeploymentManifest::load(manifestPath);
        QVERIFY(manifest.has_value());
        QVERIFY(ToolOutput::find(game, again, *manifest, filter, "Out").isEmpty());
        QCOMPARE(readFile(FS::PathCombine(out, "Data/meshes/new/gen.hkx")), QByteArray("generated again"));

        // disabled, the output leaves the game and the mods' own files are back
        list.setEnabled({ list.indexOf("Out") }, false);
        QVERIFY(deploy());
        QCOMPARE(readFile(FS::PathCombine(game, "Data/meshes/x.hkx")), QByteArray("A x"));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "Data/meshes/new")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(game, "Data/Empty/note.txt")));
        QVERIFY(QFileInfo(FS::PathCombine(game, "Data/Empty")).isDir());
    }

    void test_deploy_redeploys_reinstalled_mod()
    {
        QTemporaryDir dir;
        const auto game = FS::PathCombine(dir.path(), "game");
        const auto mods = FS::PathCombine(dir.path(), "mods");
        writeFile(FS::PathCombine(mods, "A/Data/a.txt"), "v1");
        DeployTask::Params params{ .gameDir = game,
                                   .modsDir = mods,
                                   .overwrittenDir = FS::PathCombine(dir.path(), "overwritten"),
                                   .manifestPath = FS::PathCombine(dir.path(), "deployment.json"),
                                   .enabledMods = { ModEntry{ .id = "A", .enabled = true, .stamp = 1 } } };
        DeployTask first(params);
        QVERIFY(runTask(first));
        QCOMPARE(readFile(FS::PathCombine(game, "Data/a.txt")), QByteArray("v1"));

        // reinstalling replaces the staged file; the new stamp makes deployment pick it up
        QVERIFY(GameFiles::removeTree(FS::PathCombine(mods, "A")).has_value());
        writeFile(FS::PathCombine(mods, "A/Data/a.txt"), "v2");
        params.enabledMods = { ModEntry{ .id = "A", .enabled = true, .stamp = 2 } };
        DeployTask second(params);
        QVERIFY(runTask(second));
        QCOMPARE(readFile(FS::PathCombine(game, "Data/a.txt")), QByteArray("v2"));
    }

    void test_game_icon_corner_only()
    {
        // what Windows returns at 256 px for Morrowind.exe: its 48 px icon in the corner, the rest transparent
        QImage corner(256, 256, QImage::Format_ARGB32);
        corner.fill(Qt::transparent);
        for (int y = 0; y < 48; y++) {
            for (int x = 0; x < 48; x++) {
                corner.setPixel(x, y, qRgba(200, 120, 40, 255));
            }
        }
        QVERIFY(GameIcons::cornerOnly(corner));

        auto full = corner;
        full.setPixel(48, 0, qRgba(0, 0, 0, 1));  // one faint pixel beyond the corner makes it a real 256 px icon
        QVERIFY(!GameIcons::cornerOnly(full));
        auto below = corner;
        below.setPixel(0, 255, qRgba(0, 0, 0, 255));
        QVERIFY(!GameIcons::cornerOnly(below));

        QVERIFY(!GameIcons::cornerOnly(corner.copy(0, 0, 48, 48)));                          // already the small icon
        QVERIFY(!GameIcons::cornerOnly(corner.convertToFormat(QImage::Format_RGB32)));       // no transparency
        QVERIFY(!GameIcons::cornerOnly(QImage()));                                           // missing file
    }
};

QTEST_GUILESS_MAIN(GameFilesTest)

#include "GameFiles_test.moc"
