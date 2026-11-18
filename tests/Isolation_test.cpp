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
#include <game/GameFiles.h>
#include <game/GameInstance.h>
#include <game/ModList.h>
#include <LaunchController.h>
#include <game/PluginList.h>
#include <launch/LaunchTask.h>
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
