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

#include <QElapsedTimer>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <FileSystem.h>
#include <game/Deployer.h>
#include <game/GameFiles.h>
#include <game/GameInstance.h>
#include <game/ModList.h>
#include <LaunchController.h>
#include <game/PluginList.h>
#include <game/Steam.h>
#include <launch/LaunchTask.h>
#include <launch/steps/PrepareGame.h>
#include <settings/INISettingsObject.h>

#include <windows.h>

namespace {

// no raw string literals in this file: moc skips the slots that follow one
const QString k_testKey = "Software\\BetterNMM-tests\\User Shell Folders";
const QString k_realKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders";
const QString k_savedGamesId = "{4C5C32FF-BB9D-43b0-B5B4-2D72E54EAAA4}";
const QString k_localLowId = "{A520A1A4-1780-4FF6-BD18-167343C5AF16}";

QSettings registry(const QString& key)
{
    return QSettings("HKEY_CURRENT_USER\\" + key, QSettings::NativeFormat);
}

/// Where the tests' Steam reports itself; launches read it instead of the real Steam's key
const QString k_steamKey = "Software\\BetterNMM-tests-steam";

/// Puts the tests' Steam in a state: running and logged in as this very process, or not running, started by @p exe
void setSteam(bool running, const QString& exe = {})
{
    auto steam = registry(k_steamKey);
    steam.setValue("SteamExe", exe);
    steam.setValue("ActiveProcess/pid", running ? static_cast<uint>(GetCurrentProcessId()) : 0u);
    steam.setValue("ActiveProcess/ActiveUser", running ? 12345u : 0u);
    steam.sync();
}

/// Ends the fake Steam a launch started, so it does not linger after the test
void endSteam()
{
    const auto pid = registry(k_steamKey).value("ActiveProcess/pid").toUInt();
    if (pid != 0 && pid != GetCurrentProcessId()) {
        if (HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid)) {
            TerminateProcess(process, 0);
            CloseHandle(process);
        }
    }
}

/// The value as stored, and whether it is REG_EXPAND_SZ (Windows only expands %USERPROFILE% in that type)
std::pair<QString, bool> expandableValue(const QString& key, const QString& name)
{
    wchar_t buffer[512] = {};
    DWORD size = sizeof(buffer);
    const auto status = RegGetValueW(HKEY_CURRENT_USER, key.toStdWString().c_str(), name.toStdWString().c_str(),
                                     RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, buffer, &size);
    return { registry(key).value(name).toString(), status == ERROR_SUCCESS };
}

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

QMap<QString, QString> readReport(const QString& path)
{
    QMap<QString, QString> out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return out;
    }
    for (const auto& line : QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts)) {
        const auto at = line.indexOf('=');
        out.insert(line.left(at).remove(QChar(0xFEFF)), line.mid(at + 1).trimmed());
    }
    return out;
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

}  // namespace

class IsolationTest : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase()
    {
        // launches of isolated Steam games find this "Steam" running instead of asking the real one
        Steam::setClientKey(k_steamKey);
        setSteam(true);
    }

    void cleanupTestCase()
    {
        endSteam();
        registry("Software").remove("BetterNMM-tests-steam");
    }

    void test_profile_redirection_supported()
    {
        // this machine keeps Documents and AppData under the user profile, which isolation relies on
        auto supported = GameInstance::profileRedirectionSupported();
        QVERIFY2(supported.has_value(), qPrintable(supported ? QString() : supported.error()));
    }

    void test_profile_subfolder()
    {
        QCOMPARE(GameInstance::profileSubfolder("%USERPROFILE%\\Documents", "Documents"), QString("Documents"));
        // OneDrive backup keeps Documents inside the profile, one level down
        QCOMPARE(GameInstance::profileSubfolder("%USERPROFILE%\\OneDrive\\Documents", "Documents"), QString("OneDrive/Documents"));
        QCOMPARE(GameInstance::profileSubfolder("%userprofile%\\AppData\\Local\\", "AppData/Local"), QString("AppData/Local"));
        // anything that is not a folder inside the profile falls back to the default place
        QCOMPARE(GameInstance::profileSubfolder("D:\\Documents", "Documents"), QString("Documents"));
        QCOMPARE(GameInstance::profileSubfolder("", "Saved Games"), QString("Saved Games"));
        QCOMPARE(GameInstance::profileSubfolder("%USERPROFILE%", "Documents"), QString("Documents"));
        QCOMPARE(GameInstance::profileSubfolder("%USERPROFILE%\\..\\Public\\Documents", "Documents"), QString("Documents"));
    }

    void test_saved_games_setting()
    {
        // works on a throwaway key; the real User Shell Folders key is only read, before and after
        const auto realBefore = std::pair{ registry(k_realKey).value(k_savedGamesId), registry(k_realKey).value(k_localLowId) };
        registry("Software").remove("BetterNMM-tests");
        auto cleanup = qScopeGuard([] { registry("Software").remove("BetterNMM-tests"); });

        QCOMPARE(GameInstance::sharedProfileFolders(k_testKey), QStringList({ "Saved Games", "AppData\\LocalLow" }));
        auto isolated = GameInstance::setProfileFoldersIsolated(true, k_testKey);
        if (!isolated && isolated.error().contains("usual place")) {
            QSKIP("this machine keeps Saved Games or LocalLow outside their default places");
        }
        QVERIFY2(isolated.has_value(), qPrintable(isolated ? QString() : isolated.error()));
        QCOMPARE(GameInstance::sharedProfileFolders(k_testKey), QStringList());
        QCOMPARE(expandableValue(k_testKey, k_savedGamesId), std::pair(QString("%USERPROFILE%\\Saved Games"), true));
        QCOMPARE(expandableValue(k_testKey, k_localLowId), std::pair(QString("%USERPROFILE%\\AppData\\LocalLow"), true));
        QVERIFY(GameInstance::setProfileFoldersIsolated(true, k_testKey).has_value());
        QCOMPARE(expandableValue(k_testKey, k_savedGamesId).first, QString("%USERPROFILE%\\Saved Games"));

        // a folder the user moved is never touched, either way
        const QString moved = "D:\\Elsewhere\\Saved Games";
        registry(k_testKey).setValue(k_savedGamesId, moved);
        QVERIFY(GameInstance::setProfileFoldersIsolated(false, k_testKey).has_value());
        QCOMPARE(registry(k_testKey).value(k_savedGamesId).toString(), moved);
        QVERIFY(!registry(k_testKey).contains(k_localLowId));
        QCOMPARE(GameInstance::sharedProfileFolders(k_testKey), QStringList({ "Saved Games", "AppData\\LocalLow" }));
        QVERIFY(GameInstance::setProfileFoldersIsolated(true, k_testKey).has_value());
        QCOMPARE(registry(k_testKey).value(k_savedGamesId).toString(), moved);
        QCOMPARE(GameInstance::sharedProfileFolders(k_testKey), QStringList({ "Saved Games" }));

        QCOMPARE(registry(k_realKey).value(k_savedGamesId), realBefore.first);
        QCOMPARE(registry(k_realKey).value(k_localLowId), realBefore.second);
    }

    void test_game_crash_is_reported()
    {
        QTemporaryDir dir;
        auto global = globalSettings(dir.path());
        const auto steam = FS::PathCombine(dir.path(), "steam");
        QVERIFY(FS::ensureFolderPathExists(steam));
        QVERIFY(QFile::copy(KNOWN_FOLDERS_EXE, FS::PathCombine(steam, "Game.exe")));

        // the crash of the program started directly, and of the game a loader started and left behind
        for (const auto& mode : { QString("--crash"), QString("--spawn-crash") }) {
            const auto root = FS::PathCombine(dir.path(), "instances", mode.mid(2));
            auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(root, "instance.cfg"));
            settings->registerSetting("InstanceType", "Game");
            GameInstance instance(global.get(), std::move(settings), root);
            GameDefinition game;
            game.domain = "testgame";
            game.name = "Test Game";
            game.executable = "Game.exe";
            GameInstance::applyDefinition(instance.settings(), game);
            instance.settings()->set("InstanceKind", "Main");
            instance.settings()->set("SourceGameDir", steam);
            instance.settings()->set("GameArguments", QString("%1 \"$INST_DIR/unused.txt\"").arg(mode));

            auto* launch = instance.createLaunchTask(QString());
            QVERIFY(launch);
            connect(launch, &LaunchTask::readyForLaunch, launch, &LaunchTask::proceed);
            QSignalSpy finished(launch, &Task::finished);
            launch->start();
            QVERIFY(finished.wait(60000));
            QVERIFY2(!launch->wasSuccessful(), qPrintable(mode));
            const auto crash = launch->crash();
            QVERIFY2(crash.has_value(), qPrintable(mode + ": " + launch->failReason()));
            QCOMPARE(crash->program, QString("Game.exe"));
            QCOMPARE(crash->code, quint32(0xC0000005));
            QVERIFY(crash->started.isValid());
            QVERIFY(launch->failReason().contains("access violation"));
            QVERIFY(instance.hasCrashed());
        }
    }

    void test_empty_local_patterns_mean_the_defaults()
    {
        QTemporaryDir dir;
        auto global = globalSettings(dir.path());
        const auto root = FS::PathCombine(dir.path(), "instances", "old");
        auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(root, "instance.cfg"));
        settings->registerSetting("InstanceType", "Game");
        GameInstance instance(global.get(), std::move(settings), root);
        GameInstance::applyDefinition(instance.settings(), GameRegistry::byDomain("newvegas"));
        QVERIFY(instance.localPatterns().contains("*.exe"));
        // made before the field existed, or cleared: never "no copies at all", which shares exe and plugin times with Steam
        instance.settings()->set("LocalFilePatterns", QString());
        QCOMPARE(instance.localPatterns(), GameFiles::splitPatterns(GameInstance::defaultLocalPatterns(instance.definition())));
        QVERIFY(instance.localPatterns().contains("*.esp"));  // New Vegas orders plugins by file time
        instance.settings()->set("LocalFilePatterns", QString("*.exe"));
        QCOMPARE(instance.localPatterns(), QStringList({ "*.exe" }));
    }

    void test_script_extender_falls_back_to_the_game()
    {
        QTemporaryDir dir;
        auto global = globalSettings(dir.path());
        const auto root = FS::PathCombine(dir.path(), "instances", "old");
        auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(root, "instance.cfg"));
        settings->registerSetting("InstanceType", "Game");
        GameInstance instance(global.get(), std::move(settings), root);
        GameInstance::applyDefinition(instance.settings(), GameRegistry::byDomain("skyrimspecialedition"));
        // an instance whose field is empty (made by an older version, or cleared) still knows its game's loader
        instance.settings()->set("ScriptExtender", QString());
        QCOMPARE(instance.definition().scriptExtender, QString("skse64_loader.exe"));
        instance.settings()->set("ScriptExtender", QString("custom_loader.exe"));
        QCOMPARE(instance.definition().scriptExtender, QString("custom_loader.exe"));
    }

    void test_launch_checks_the_load_order()
    {
        QTemporaryDir dir;
        auto global = globalSettings(dir.path());
        // a TES4 header listing one master (MAST after HEDR), the way PluginList reads it
        QByteArray fields("HEDR");
        fields += QByteArray("\x0c\x00", 2) + QByteArray(12, '\0') + "MAST" + QByteArray("\x09\x00", 2) + "Gone.esm" + '\0';
        QByteArray orphan("TES4");
        for (int i = 0; i < 4; i++) {
            orphan.append(static_cast<char>((fields.size() >> (8 * i)) & 0xFF));
        }
        orphan += QByteArray(16, '\0') + fields;
        const auto root = FS::PathCombine(dir.path(), "instances", "checked");
        auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(root, "instance.cfg"));
        settings->registerSetting("InstanceType", "Game");
        GameInstance instance(global.get(), std::move(settings), root);
        GameDefinition game;
        game.domain = "testgame";
        game.name = "Test Game";
        game.executable = "Game.exe";
        game.dataPath = "Data";
        game.pluginFormat = PluginFormat::Asterisk;
        game.appDataFolder = "Test Game";
        GameInstance::applyDefinition(instance.settings(), game);
        // an isolated instance: its plugins.txt goes into its own profile, inside the test's folder
        QVERIFY(!instance.isMain());
        QVERIFY(instance.pluginListDir().startsWith(root));
        writeFile(FS::PathCombine(instance.gameRoot(), "Game.exe"), "exe");
        writeFile(FS::PathCombine(instance.dataDir(), "Orphan.esp"), orphan);
        instance.setCustomExecutables({ GameExecutable{ .name = "xEdit", .path = "Game.exe" } });
        QVERIFY(LaunchController::loadOrderProblems(&instance, QString()).isEmpty());  // the plugin is not active

        auto* plugins = instance.pluginList();
        QVERIFY(plugins->refresh().has_value());
        QCOMPARE(plugins->plugins().size(), 1);
        plugins->setActive({ 0 }, true);
        QVERIFY(plugins->save().has_value());
        const auto problems = LaunchController::loadOrderProblems(&instance, QString());
        QCOMPARE(problems, QStringList({ "Orphan.esp: Missing master: Gone.esm" }));
        QCOMPARE(LaunchController::loadOrderProblems(&instance, "Test Game"), problems);
        // tools are how such a load order gets fixed: no question for them
        QVERIFY(LaunchController::loadOrderProblems(&instance, "xEdit").isEmpty());
    }

    void test_killed_game_is_not_a_crash()
    {
        QTemporaryDir dir;
        auto global = globalSettings(dir.path());
        const auto steam = FS::PathCombine(dir.path(), "steam");
        QVERIFY(FS::ensureFolderPathExists(steam));
        QVERIFY(QFile::copy(KNOWN_FOLDERS_EXE, FS::PathCombine(steam, "Game.exe")));
        const auto root = FS::PathCombine(dir.path(), "instances", "killed");
        auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(root, "instance.cfg"));
        settings->registerSetting("InstanceType", "Game");
        GameInstance instance(global.get(), std::move(settings), root);
        GameDefinition game;
        game.domain = "testgame";
        game.name = "Test Game";
        game.executable = "Game.exe";
        GameInstance::applyDefinition(instance.settings(), game);
        instance.settings()->set("InstanceKind", "Main");
        instance.settings()->set("SourceGameDir", steam);
        // the helper stays open for a while before it writes and exits
        instance.settings()->set("GameArguments", QString("--print \"$INST_DIR/unused.txt\""));

        auto* launch = instance.createLaunchTask(QString());
        QVERIFY(launch);
        connect(launch, &LaunchTask::readyForLaunch, launch, &LaunchTask::proceed);
        QSignalSpy finished(launch, &Task::finished);
        QSignalSpy failed(launch, &Task::failed);
        launch->start();
        QTRY_VERIFY_WITH_TIMEOUT(launch->canAbort() && instance.isRunning(), 10000);
        QTest::qWait(300);
        QVERIFY(launch->abort());
        QVERIFY(finished.wait(30000) || finished.count() == 1);
        QCOMPARE(launch->getState(), Task::State::AbortedByUser);
        QCOMPARE(failed.count(), 0);
        QVERIFY(!instance.hasCrashed());
        QVERIFY(!instance.isRunning());
    }

    void test_tool_output_goes_into_its_mod()
    {
        QTemporaryDir dir;
        auto global = globalSettings(dir.path());
        const auto steam = FS::PathCombine(dir.path(), "steam");
        QVERIFY(FS::ensureFolderPathExists(FS::PathCombine(steam, "Data/Writer")));
        QVERIFY(QFile::copy(KNOWN_FOLDERS_EXE, FS::PathCombine(steam, "Game.exe")));
        QVERIFY(FS::ensureFolderPathExists(FS::PathCombine(steam, "Tools/Writer")));
        QVERIFY(QFile::copy(KNOWN_FOLDERS_EXE, FS::PathCombine(steam, "Tools/Writer/writer.exe")));
        writeFile(FS::PathCombine(steam, "Data/Base.esm"), "TES4");

        const auto root = FS::PathCombine(dir.path(), "instances", "tools");
        auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(root, "instance.cfg"));
        settings->registerSetting("InstanceType", "Game");
        GameInstance instance(global.get(), std::move(settings), root);
        GameDefinition game;
        game.domain = "testgame";
        game.name = "Test Game";
        game.executable = "Game.exe";
        game.dataPath = "Data";
        GameInstance::applyDefinition(instance.settings(), game);
        instance.settings()->set("InstanceKind", "Main");
        instance.settings()->set("SourceGameDir", steam);
        QCOMPARE(QDir(instance.gameRoot()), QDir(steam));

        Task::State ended = Task::State::Inactive;
        const auto run = [&](const QString& arguments, const std::function<void(LaunchTask*)>& whileRunning) {
            instance.setCustomExecutables({ GameExecutable{
                .name = "Writer", .path = "Tools/Writer/writer.exe", .arguments = arguments, .outputMod = "Writer Output" } });
            auto* launch = instance.createLaunchTask("Writer");
            connect(launch, &LaunchTask::readyForLaunch, launch, &LaunchTask::proceed);
            QSignalSpy finished(launch, &Task::finished);
            launch->start();
            if (whileRunning) {
                // the tool's own step is the only one that can be stopped: the game folder has been noted by then
                QTRY_VERIFY_WITH_TIMEOUT(launch->canAbort(), 10000);
                whileRunning(launch);
            }
            QVERIFY(finished.wait(60000) || finished.count() == 1);
            QVERIFY(!instance.isRunning());
            ended = launch->getState();
        };
        const auto outputMod = [&instance]() -> ModEntry {
            for (const auto& mod : instance.modList()->mods()) {
                if (mod.name == "Writer Output") {
                    return mod;
                }
            }
            return {};
        };

        // the report the tool writes into the game folder ends up in its output mod, linked back into the game
        run("--print \"$INST_GAME_DIR/Data/Writer/report.txt\"", {});
        QCOMPARE(ended, Task::State::Succeeded);
        auto output = outputMod();
        QVERIFY(!output.id.isEmpty());
        QVERIFY(output.enabled);
        const auto modReport = FS::PathCombine(instance.modList()->modDir(output.id), "Data/Writer/report.txt");
        const auto gameReport = FS::PathCombine(steam, "Data/Writer/report.txt");
        QVERIFY(readReport(modReport).contains("documents"));
        QVERIFY(GameFiles::isSameFile(modReport, gameReport));
        QVERIFY(QFileInfo(FS::PathCombine(steam, "Data/Writer")).isDir());
        QCOMPARE(QFileInfo(FS::PathCombine(steam, "Data/Base.esm")).size(), 4);

        // a tool that crashes still has its files moved: here it crashes half a second after it starts, and "it"
        // writes a file, a log and a file of its own folder before that
        run("--crash unused", [&](LaunchTask*) {
            writeFile(FS::PathCombine(steam, "Data/late.txt"), "late");
            writeFile(FS::PathCombine(steam, "Data/tool.log"), "log");
            writeFile(FS::PathCombine(steam, "Tools/Writer/cache.txt"), "cache");
        });
        QCOMPARE(ended, Task::State::Failed);
        output = outputMod();
        const auto modLate = FS::PathCombine(instance.modList()->modDir(output.id), "Data/late.txt");
        QCOMPARE(readFile(modLate), QByteArray("late"));
        QVERIFY(GameFiles::isSameFile(modLate, FS::PathCombine(steam, "Data/late.txt")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(instance.modList()->modDir(output.id), "Data/tool.log")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(instance.modList()->modDir(output.id), "Tools/Writer/cache.txt")));
        QCOMPARE(readFile(FS::PathCombine(steam, "Tools/Writer/cache.txt")), QByteArray("cache"));
        QCOMPARE(readFile(FS::PathCombine(steam, "Data/tool.log")), QByteArray("log"));

        // and so does one that is stopped
        instance.modList()->setEnabled({ instance.modList()->indexOf(output.id) }, false);
        run("--print \"$INST_GAME_DIR/Missing/x.txt\"", [&](LaunchTask* launch) {
            writeFile(FS::PathCombine(steam, "Data/stopped.txt"), "stopped");
            QVERIFY(launch->abort());
        });
        QCOMPARE(ended, Task::State::AbortedByUser);
        output = outputMod();
        QVERIFY(output.enabled);  // its files are used, so it is on again
        const auto modStopped = FS::PathCombine(instance.modList()->modDir(output.id), "Data/stopped.txt");
        QCOMPARE(readFile(modStopped), QByteArray("stopped"));
        QVERIFY(GameFiles::isSameFile(modStopped, FS::PathCombine(steam, "Data/stopped.txt")));
        QVERIFY(GameFiles::isSameFile(modLate, FS::PathCombine(steam, "Data/late.txt")));

        // disabled, its files leave the game folder
        instance.modList()->setEnabled({ instance.modList()->indexOf(output.id) }, false);
        auto purge = instance.createDeployTask();
        QSignalSpy purged(purge.get(), &Task::finished);
        purge->start();
        QVERIFY(purged.wait(30000) || purged.count() == 1);
        QVERIFY(!QFileInfo::exists(FS::PathCombine(steam, "Data/late.txt")));
        QVERIFY(!QFileInfo::exists(gameReport));
        QVERIFY(QFileInfo(FS::PathCombine(steam, "Data/Writer")).isDir());
    }

    void test_steam_started_before_an_isolated_game()
    {
        QTemporaryDir dir;
        auto global = globalSettings(dir.path());
        const auto steam = FS::PathCombine(dir.path(), "steam");
        QVERIFY(FS::ensureFolderPathExists(steam));
        QVERIFY(QFile::copy(KNOWN_FOLDERS_EXE, FS::PathCombine(steam, "Game.exe")));
        writeFile(FS::PathCombine(steam, "Data/Base.esm"), "TES4");
        const auto root = FS::PathCombine(dir.path(), "instances/steamfirst");
        auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(root, "instance.cfg"));
        settings->registerSetting("InstanceType", "Game");
        GameInstance instance(global.get(), std::move(settings), root);
        GameDefinition game;
        game.domain = "testgame";
        game.name = "Test Game";
        game.executable = "Game.exe";
        game.dataPath = "Data";
        GameInstance::applyDefinition(instance.settings(), game);
        instance.settings()->set("InstanceKind", "Isolated");
        instance.settings()->set("SourceGameDir", steam);
        instance.settings()->set("SteamAppId", 12345);
        auto sync = instance.createBaseSyncTask();
        QSignalSpy synced(sync.get(), &Task::finished);
        sync->start();
        QVERIFY(synced.wait(30000));
        QVERIFY2(sync->wasSuccessful(), qPrintable(sync->failReason()));

        const auto report = FS::PathCombine(root, "report.txt");
        instance.settings()->set("GameArguments", QString("--print \"%1\"").arg(report));
        const auto launch = [&](QString* log) {
            auto* task = instance.createLaunchTask(QString());
            connect(task, &LaunchTask::readyForLaunch, task, &LaunchTask::proceed);
            QSignalSpy finished(task, &Task::finished);
            task->start();
            // a launch that stops at once has finished before there is anything to wait for
            const bool ended = finished.count() == 1 || finished.wait(60000);
            *log = task->getLogModel()->toPlainText();
            return ended && task->wasSuccessful();
        };
        auto cleanup = qScopeGuard([] {
            endSteam();
            PrepareGame::setSteamTimeout(120000);
            setSteam(true);
        });

        // Steam closed: BetterNMM starts it first and waits until it is logged in, then the game runs in the instance
        setSteam(false, FAKE_STEAM_EXE);
        QString log;
        QVERIFY2(launch(&log), qPrintable(log));
        QVERIFY2(log.contains("Starting Steam first"), qPrintable(log));
        QVERIFY(log.contains("Steam is running."));
        QVERIFY(readReport(report).contains("documents"));
        QVERIFY(Steam::clientState().loggedIn);
        endSteam();

        // Steam nowhere to be found: the launch stops before the game can start Steam itself
        QFile::remove(report);
        setSteam(false, FS::PathCombine(dir.path(), "no-steam.exe"));
        QVERIFY(!launch(&log));
        QVERIFY2(log.contains("can not find it"), qPrintable(log));
        QVERIFY(!QFileInfo::exists(report));

        // Steam that starts but never logs in: the launch gives up after the wait, the game does not run
        const auto nologin = FS::PathCombine(dir.path(), "steam-nologin.exe");
        QVERIFY(QFile::copy(FAKE_STEAM_EXE, nologin));
        setSteam(false, nologin);
        PrepareGame::setSteamTimeout(3000);
        QVERIFY(!launch(&log));
        QVERIFY2(log.contains("did not start and log in"), qPrintable(log));
        QVERIFY(!QFileInfo::exists(report));

        // Steam left running with this instance's profile (its browser holds the lock file): the launch says so
        setSteam(true);
        const auto lock = FS::PathCombine(instance.profileRoot(), "AppData/Local/Steam/htmlcache/lockfile");
        QVERIFY(FS::ensureFilePathExists(lock));
        HANDLE held = CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(lock).utf16()), GENERIC_READ | GENERIC_WRITE, 0,
                                  nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(held != INVALID_HANDLE_VALUE);
        const bool launched = launch(&log);
        CloseHandle(held);
        QVERIFY2(launched, qPrintable(log));
        QVERIFY2(log.contains("Steam is running with this instance's profile"), qPrintable(log));
        // once Steam has let go of it, nothing is said
        QVERIFY(launch(&log));
        QVERIFY(!log.contains("Steam is running with this instance's profile"));
    }

    void test_isolated_instance_launch()
    {
        QTemporaryDir dir;
        auto global = globalSettings(dir.path());

        // a "Steam installation" whose game reports where Windows tells it the user's folders are
        const auto steam = FS::PathCombine(dir.path(), "steam");
        QVERIFY(FS::ensureFolderPathExists(steam));
        QVERIFY(QFile::copy(KNOWN_FOLDERS_EXE, FS::PathCombine(steam, "Game.exe")));
        writeFile(FS::PathCombine(steam, "Data/Base.esm"), "TES4");

        const auto root = FS::PathCombine(dir.path(), "instances/test");
        auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(root, "instance.cfg"));
        settings->registerSetting("InstanceType", "Game");
        GameInstance instance(global.get(), std::move(settings), root);
        GameDefinition game;
        game.domain = "testgame";
        game.name = "Test Game";
        game.executable = "Game.exe";
        game.myGamesFolder = "BetterNMM Test Game";
        game.appDataFolder = "BetterNMM Test Game";
        game.dataPath = "Data";
        game.pluginFormat = PluginFormat::Asterisk;
        game.implicitPlugins = { "Base.esm" };
        GameInstance::applyDefinition(instance.settings(), game);
        instance.settings()->set("InstanceKind", "Isolated");
        instance.settings()->set("SourceGameDir", steam);
        instance.settings()->set("SteamAppId", 12345);
        const auto report = FS::PathCombine(root, "report.txt");
        // like a script extender loader: start the game and exit
        instance.settings()->set("GameArguments", QString("--spawn \"$INST_DIR/report.txt\""));

        auto sync = instance.createBaseSyncTask();
        QSignalSpy synced(sync.get(), &Task::finished);
        sync->start();
        QVERIFY(synced.wait(30000));
        QVERIFY2(sync->wasSuccessful(), qPrintable(sync->failReason()));
        QVERIFY(GameFiles::isSameFile(FS::PathCombine(steam, "Data/Base.esm"), FS::PathCombine(instance.gameRoot(), "Data/Base.esm")));

        writeFile(FS::PathCombine(instance.modsRoot(), "ModA/Data/ModA.esp"), "TES4");
        QVERIFY(instance.modList()->load().has_value());
        instance.modList()->setEnabled({ instance.modList()->indexOf("ModA") }, true);

        auto* launch = instance.createLaunchTask(QString());
        QVERIFY(launch);
        connect(launch, &LaunchTask::readyForLaunch, launch, &LaunchTask::proceed);
        QSignalSpy finished(launch, &Task::finished);
        QElapsedTimer timer;
        timer.start();
        launch->start();
        QVERIFY(finished.wait(60000));
        QStringList log;
        for (int i = 0; i < launch->getLogModel()->rowCount(); i++) {
            log << launch->getLogModel()->data(launch->getLogModel()->index(i), Qt::DisplayRole).toString();
        }
        QVERIFY2(launch->wasSuccessful(), qPrintable(launch->failReason() + "\n" + log.join('\n')));
        // the loader exits at once; the launch only ends when the game it started has exited
        QVERIFY2(timer.elapsed() >= 1400, qPrintable(QString::number(timer.elapsed())));

        const auto folders = readReport(report);
        const auto profile = QDir::toNativeSeparators(QDir(instance.profileRoot()).absolutePath());
        QCOMPARE(folders.value("documents"), profile + "\\Documents");
        QCOMPARE(folders.value("personal"), profile + "\\Documents");
        QCOMPARE(folders.value("localappdata"), profile + "\\AppData\\Local");
        QCOMPARE(folders.value("roamingappdata"), profile + "\\AppData\\Roaming");
        if (GameInstance::sharedProfileFolders().contains("Saved Games")) {
            // Windows resolves Saved Games without %USERPROFILE% here; the launch log says so
            QVERIFY(!folders.value("savedgames").startsWith(profile));
            QVERIFY(log.join('\n').contains("Not isolated: Saved Games"));
        } else {
            QCOMPARE(folders.value("savedgames"), profile + "\\Saved Games");
        }
        QCOMPARE(folders.value("steamappid"), QString("12345"));

        // mods deployed into the instance only, the load order written into its own AppData
        QVERIFY(GameFiles::isSameFile(FS::PathCombine(instance.modsRoot(), "ModA/Data/ModA.esp"),
                                      FS::PathCombine(instance.gameRoot(), "Data/ModA.esp")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(steam, "Data/ModA.esp")));
        QFile plugins(FS::PathCombine(instance.pluginListDir(), "plugins.txt"));
        QVERIFY(plugins.open(QIODevice::ReadOnly));
        QVERIFY(plugins.readAll().contains("ModA.esp"));
        QFile appId(FS::PathCombine(instance.gameRoot(), "steam_appid.txt"));
        QVERIFY(appId.open(QIODevice::ReadOnly));
        QCOMPARE(appId.readAll(), QByteArray("12345"));
        QVERIFY(QFileInfo(instance.myGamesDir()).isDir());

        // nothing reached the real user profile or the Steam installation
        const auto realDocuments = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        QVERIFY(!QFileInfo::exists(FS::PathCombine(realDocuments, "My Games", "BetterNMM Test Game")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")), "BetterNMM Test Game")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(steam, "steam_appid.txt")));
    }
};

QTEST_GUILESS_MAIN(IsolationTest)

#include "Isolation_test.moc"
