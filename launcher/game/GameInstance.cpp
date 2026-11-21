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

#include "GameInstance.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "game/Deployer.h"
#include "game/GameFiles.h"
#include "game/ModList.h"
#include "game/PluginList.h"
#include "launch/LaunchTask.h"
#include "launch/steps/CaptureToolOutput.h"
#include "launch/steps/LaunchCommand.h"
#include "launch/steps/LaunchGameProcess.h"
#include "launch/steps/PrepareGame.h"
#include "launch/steps/QuitAfterGameStop.h"
#include "launch/steps/TextPrint.h"
#include "settings/Setting.h"

#ifdef Q_OS_WIN
#include <windows.h>

#include <shlobj.h>
#endif

namespace {

const QString k_shellFoldersKey = R"(Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders)";

/// A user folder by its User Shell Folders entry and its default place in the profile
struct ProfileFolder {
    const char* registryName;
    const char* defaultPath;
};

constexpr ProfileFolder k_documents{ "Personal", "Documents" };
constexpr ProfileFolder k_localAppData{ "Local AppData", "AppData/Local" };
constexpr ProfileFolder k_roamingAppData{ "AppData", "AppData/Roaming" };
constexpr ProfileFolder k_savedGames{ "{4C5C32FF-BB9D-43b0-B5B4-2D72E54EAAA4}", "Saved Games" };
constexpr ProfileFolder k_localLow{ "{A520A1A4-1780-4FF6-BD18-167343C5AF16}", "AppData/LocalLow" };
// every isolated profile gets these, as SHGetKnownFolderPath fails for folders that do not exist
constexpr ProfileFolder k_profileFolders[] = { { "Desktop", "Desktop" },
                                               k_documents,
                                               { "{374DE290-123F-4565-9164-39C4925E467B}", "Downloads" },
                                               { "My Music", "Music" },
                                               { "My Pictures", "Pictures" },
                                               { "My Video", "Videos" },
                                               k_savedGames,
                                               k_localAppData,
                                               k_localLow,
                                               k_roamingAppData };

/// The raw (unexpanded) User Shell Folders entry of a folder, empty when it has none
QString shellFolderValue(const QString& key, const char* registryName)
{
    return QSettings("HKEY_CURRENT_USER\\" + key, QSettings::NativeFormat).value(registryName).toString();
}

QString profileRelative(const ProfileFolder& folder)
{
    return GameInstance::profileSubfolder(shellFolderValue(k_shellFoldersKey, folder.registryName), folder.defaultPath);
}

}  // namespace

QString GameInstance::profileSubfolder(const QString& shellFolderEntry, const QString& defaultPath)
{
    static const QString prefix = "%USERPROFILE%";
    if (shellFolderEntry.startsWith(prefix, Qt::CaseInsensitive)) {
        const auto rest = QDir::cleanPath(QDir::fromNativeSeparators(shellFolderEntry.mid(prefix.size())));
        if (rest.size() > 1 && rest.startsWith('/') && !rest.startsWith("/..")) {
            return rest.mid(1);
        }
    }
    return defaultPath;
}

GameInstance* GameInstance::mainInstanceFor(const QString& gameDir, int steamAppId)
{
    const auto folder = QDir::cleanPath(QFileInfo(gameDir).absoluteFilePath());
    for (int i = 0; i < APPLICATION->instances()->count(); i++) {
        auto* other = APPLICATION->instances()->at(i);
        if (!other->isMain()) {
            continue;
        }
        const bool sameFolder = QDir::cleanPath(other->sourceGameDir()).compare(folder, Qt::CaseInsensitive) == 0;
        if (sameFolder || (steamAppId > 0 && other->steamAppId() == steamAppId)) {
            return other;
        }
    }
    return nullptr;
}

QString GameInstance::documentsSubfolder()
{
    return profileRelative(k_documents);
}

QString GameInstance::localAppDataSubfolder()
{
    return profileRelative(k_localAppData);
}

QString GameInstance::roamingAppDataSubfolder()
{
    return profileRelative(k_roamingAppData);
}

QString GameInstance::savedGamesSubfolder()
{
    return profileRelative(k_savedGames);
}

GameInstance::GameInstance(SettingsObject* globalSettings, std::unique_ptr<SettingsObject> settings, const QString& rootDir)
    : BaseInstance(globalSettings, std::move(settings), rootDir)
{
    m_settings->registerSetting("InstanceKind", "Isolated");
    m_settings->registerSetting("SourceGameDir", "");
    m_settings->registerSetting("SteamAppId", 0);

    // the game definition is copied into the instance so games BetterNMM does not know work the same way
    m_settings->registerSetting("GameDomain", "");
    m_settings->registerSetting("GameName", "");
    m_settings->registerSetting("GameExecutable", "");
    m_settings->registerSetting("ScriptExtender", "");
    m_settings->registerSetting("MyGamesFolder", "");
    m_settings->registerSetting("AppDataFolder", "");
    m_settings->registerSetting("IniFiles", "");
    m_settings->registerSetting("DataPath", "");
    m_settings->registerSetting("PluginFormat", "none");
    m_settings->registerSetting("ImplicitPlugins", "");
    m_settings->registerSetting("CccFile", "");

    // empty: automatic; LaunchChoiceVersion 1 marks it as a choice the user made (see chosenExecutableName)
    m_settings->registerSetting("LaunchExecutable", "");
    m_settings->registerSetting("LaunchChoiceVersion", 0);
    m_settings->registerSetting("GameArguments", "");
    m_settings->registerSetting("CustomExecutables", "[]");

    m_settings->registerSetting("LocalFilePatterns", "");
    m_settings->registerSetting("ExcludeFilePatterns", defaultExcludePatterns());

    auto envSetting = m_settings->registerSetting("OverrideEnv", false);
    m_settings->registerOverride(globalSettings->getSetting("Env"), envSetting);
    m_settings->registerPassthrough(globalSettings->getSetting("QuitAfterGameStop"), nullptr);
    m_settings->registerPassthrough(globalSettings->getSetting("CloseAfterLaunch"), nullptr);
}

GameInstance::~GameInstance() = default;

void GameInstance::applyDefinition(SettingsObject* settings, const GameDefinition& game)
{
    settings->set("GameDomain", game.domain);
    settings->set("GameName", game.name);
    settings->set("GameExecutable", game.executable);
    settings->set("ScriptExtender", game.scriptExtender);
    settings->set("MyGamesFolder", game.myGamesFolder);
    settings->set("AppDataFolder", game.appDataFolder);
    settings->set("IniFiles", game.iniFiles.join(';'));
    settings->set("DataPath", game.dataPath);
    settings->set("PluginFormat", GameDefinition::pluginFormatToString(game.pluginFormat));
    settings->set("ImplicitPlugins", game.implicitPlugins.join(';'));
    settings->set("CccFile", game.cccFile);
    settings->set("LocalFilePatterns", defaultLocalPatterns(game));
}

QString GameInstance::defaultLocalPatterns(const GameDefinition& game)
{
    // anything a patcher or the game may rewrite in place stays a private copy
    QString patterns = "*.exe;*.dll;*.asi;*.ini;*.cfg;*.toml;*.json;*.xml;*.txt";
    if (game.pluginFormat == PluginFormat::Timestamp) {
        // these games order plugins by file time, which hard links would share with the Steam installation
        patterns += ";*.esm;*.esp";
    }
    return patterns;
}

QStringList GameInstance::localPatterns() const
{
    const auto set = m_settings->get("LocalFilePatterns").toString();
    return GameFiles::splitPatterns(set.trimmed().isEmpty() ? defaultLocalPatterns(definition()) : set);
}

QString GameInstance::defaultExcludePatterns()
{
    return "*.dmp;*.log;steam_appid.txt";
}

void GameInstance::saveNow()
{
    if (m_modList) {
        if (auto saved = m_modList->save(); !saved) {
            qWarning() << "Could not save the mod list of" << name() << ":" << saved.error();
        }
    }
}

void GameInstance::loadSpecificSettings()
{
    setSpecificSettingsLoaded(true);
}

bool GameInstance::canLaunch() const
{
    return BaseInstance::canLaunch() && QFileInfo(gameRoot()).isDir();
}

void GameInstance::populateLaunchMenu(QMenu* menu)
{
    const auto all = executables();
    const auto chosen = chosenExecutableName(all);
    const auto current = chosen.isEmpty() ? automaticExecutableName(all) : chosen;
    for (int i = 0; i < all.size(); i++) {
        const auto& exe = all[i];
        auto* action = menu->addAction(exe.name == current ? tr("Launch %1 (default)").arg(exe.name) : tr("Launch %1").arg(exe.name));
        // the co-op launcher and script extender (and the default, which may be a tool standing in for the co-op launcher)
        // can be in a mod that the launch deploys first
        action->setEnabled((i > 0 && exe.builtIn) || exe.name == current || QFileInfo::exists(absoluteExecutablePath(exe)));
        connect(action, &QAction::triggered, this, [this, name = exe.name] { APPLICATION->launch(this, name); });
    }
}

GameInstance::Kind GameInstance::kind() const
{
    return m_settings->get("InstanceKind").toString() == "Main" ? Kind::Main : Kind::Isolated;
}

GameDefinition GameInstance::definition() const
{
    GameDefinition def;
    def.domain = m_settings->get("GameDomain").toString();
    def.name = m_settings->get("GameName").toString();
    if (const int appId = steamAppId(); appId > 0) {
        def.steamAppIds << appId;
    }
    def.executable = m_settings->get("GameExecutable").toString();
    def.scriptExtender = m_settings->get("ScriptExtender").toString();
    if (def.scriptExtender.isEmpty()) {
        // an instance made without the field (an older version, or cleared) still knows its game's loader
        def.scriptExtender = GameRegistry::byDomain(def.domain).scriptExtender;
    }
    def.myGamesFolder = m_settings->get("MyGamesFolder").toString();
    def.appDataFolder = m_settings->get("AppDataFolder").toString();
    def.iniFiles = GameFiles::splitPatterns(m_settings->get("IniFiles").toString());
    def.dataPath = m_settings->get("DataPath").toString();
    def.pluginFormat = GameDefinition::pluginFormatFromString(m_settings->get("PluginFormat").toString());
    def.implicitPlugins = GameFiles::splitPatterns(m_settings->get("ImplicitPlugins").toString());
    def.cccFile = m_settings->get("CccFile").toString();
    // the engine's limits and the co-op launcher belong to the game, not to the instance
    const auto known = GameRegistry::byDomain(def.domain);
    def.fullPluginSlots = known.fullPluginSlots;
    def.lightPluginSlots = known.lightPluginSlots;
    def.coopLauncher = known.coopLauncher;
    def.coopLauncherName = known.coopLauncherName;
    def.coopLauncherArguments = known.coopLauncherArguments;
    return def;
}

QString GameInstance::sourceGameDir() const
{
    return m_settings->get("SourceGameDir").toString();
}

int GameInstance::steamAppId() const
{
    return m_settings->get("SteamAppId").toInt();
}

QString GameInstance::gameRoot() const
{
    return isMain() ? sourceGameDir() : FS::PathCombine(instanceRoot(), "game");
}

QString GameInstance::modsRoot() const
{
    return FS::PathCombine(instanceRoot(), "mods");
}

QString GameInstance::instanceConfigFolder() const
{
    return instanceRoot();
}

QString GameInstance::profileRoot() const
{
    return isMain() ? QString() : FS::PathCombine(instanceRoot(), "profile");
}

QString GameInstance::documentsDir() const
{
    return isMain() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : FS::PathCombine(profileRoot(), documentsSubfolder());
}

QString GameInstance::myGamesDir() const
{
    const auto folder = m_settings->get("MyGamesFolder").toString();
    return folder.isEmpty() ? QString() : FS::PathCombine(documentsDir(), "My Games", folder);
}

QString GameInstance::savesDir() const
{
    const auto myGames = myGamesDir();
    return myGames.isEmpty() ? QString() : FS::PathCombine(myGames, "Saves");
}

QString GameInstance::localAppDataDir() const
{
    if (!isMain()) {
        return FS::PathCombine(profileRoot(), localAppDataSubfolder());
    }
    const auto env = qEnvironmentVariable("LOCALAPPDATA");
    return env.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) : QDir::fromNativeSeparators(env);
}

QString GameInstance::pluginListDir() const
{
    const auto folder = m_settings->get("AppDataFolder").toString();
    return folder.isEmpty() ? QString() : FS::PathCombine(localAppDataDir(), folder);
}

QString GameInstance::dataDir() const
{
    const auto data = m_settings->get("DataPath").toString();
    return data.isEmpty() ? gameRoot() : FS::PathCombine(gameRoot(), data);
}

QString GameInstance::overwrittenDir() const
{
    return FS::PathCombine(instanceRoot(), "overwritten");
}

QString GameInstance::downloadsDir() const
{
    auto domain = m_settings->get("GameDomain").toString();
    if (domain.isEmpty()) {
        domain = "other";
    }
    return FS::PathCombine(APPLICATION->settings()->get("DownloadsDir").toString(), domain);
}

QString GameInstance::deploymentManifestPath() const
{
    return FS::PathCombine(instanceRoot(), "deployment.json");
}

QString GameInstance::baseManifestPath() const
{
    return FS::PathCombine(instanceRoot(), "base.json");
}

ModList* GameInstance::modList()
{
    if (!m_modList) {
        m_modList = std::make_unique<ModList>(modsRoot(), FS::PathCombine(instanceRoot(), "mods.json"));
        FS::ensureFolderPathExists(modsRoot());
        if (auto loaded = m_modList->load(); !loaded) {
            qCritical() << "Could not load the mod list of" << name() << ":" << loaded.error();
        }
    }
    return m_modList.get();
}

PluginList* GameInstance::pluginList()
{
    if (!m_pluginList) {
        PluginList::Paths paths{ .dataDir = dataDir(),
                                 .gameDir = gameRoot(),
                                 .orderFile = FS::PathCombine(instanceRoot(), "loadorder.txt"),
                                 .gameListDir = pluginListDir() };
        m_pluginList = std::make_unique<PluginList>(definition(), paths);
        if (auto loaded = m_pluginList->refresh(); !loaded) {
            qCritical() << "Could not read the load order of" << name() << ":" << loaded.error();
        }
    }
    return m_pluginList.get();
}

std::unique_ptr<DeployTask> GameInstance::createDeployTask()
{
    return std::make_unique<DeployTask>(DeployTask::Params{ .gameDir = gameRoot(),
                                                            .modsDir = modsRoot(),
                                                            .overwrittenDir = overwrittenDir(),
                                                            .manifestPath = deploymentManifestPath(),
                                                            .enabledMods = modList()->enabledMods() });
}

std::unique_ptr<DeployTask> GameInstance::createPurgeTask()
{
    return std::make_unique<DeployTask>(DeployTask::Params{ .gameDir = gameRoot(),
                                                            .modsDir = modsRoot(),
                                                            .overwrittenDir = overwrittenDir(),
                                                            .manifestPath = deploymentManifestPath(),
                                                            .enabledMods = {} });
}

std::unique_ptr<GameFiles::BaseSyncTask> GameInstance::createBaseSyncTask()
{
    if (isMain()) {
        return nullptr;
    }
    GameFiles::BaseSyncTask::Params params;
    params.sourceDir = sourceGameDir();
    params.gameDir = gameRoot();
    params.manifestPath = baseManifestPath();
    params.overwrittenDir = overwrittenDir();
    params.localPatterns = localPatterns();
    params.excludePatterns = GameFiles::splitPatterns(m_settings->get("ExcludeFilePatterns").toString());
    if (auto manifest = DeploymentManifest::load(deploymentManifestPath())) {
        params.deployedFiles = manifest->keys();
    }
    params.knownModFiles = GameFiles::vortexDeployedFiles(sourceGameDir(), m_settings->get("DataPath").toString());
    // the Steam instance keeps the originals its mods replaced; prefer those so this instance starts vanilla
    if (auto* app = APPLICATION_DYN; app && app->instances()) {
        for (int i = 0; i < app->instances()->count(); i++) {
            auto* other = app->instances()->at(i);
            if (other != this && other->isMain() && QDir::cleanPath(other->sourceGameDir()).compare(QDir::cleanPath(sourceGameDir()), Qt::CaseInsensitive) == 0) {
                params.alternateSourceDir = other->overwrittenDir();
                if (auto manifest = DeploymentManifest::load(other->deploymentManifestPath())) {
                    params.knownModFiles.unite(manifest->keys());
                }
            }
        }
    }
    return std::make_unique<GameFiles::BaseSyncTask>(params);
}

Result<> GameInstance::profileRedirectionSupported()
{
#ifdef Q_OS_WIN
    for (const auto& [folder, label] : { std::pair{ k_documents, "Documents" }, std::pair{ k_localAppData, "AppData\\Local" },
                                         std::pair{ k_roamingAppData, "AppData\\Roaming" } }) {
        const auto value = shellFolderValue(k_shellFoldersKey, folder.registryName);
        if (!value.isEmpty() && !value.startsWith("%USERPROFILE%", Qt::CaseInsensitive)) {
            return std::unexpected(QObject::tr("Windows keeps your %1 folder at %2, outside your user profile (OneDrive or folder "
                                               "redirection does this). Isolated instances need %1 inside the user profile to give the "
                                               "game its own copy; move the folder back in its Properties > Location tab.")
                                       .arg(label, value));
        }
    }
#endif
    return {};
}

QStringList GameInstance::sharedProfileFolders(const QString& shellFoldersKey)
{
    QStringList shared;
#ifdef Q_OS_WIN
    // Windows only lets a process see its own copy of folders it resolves through %USERPROFILE%. Saved Games and
    // AppData\LocalLow have no such entry unless the user relocated them, so they stay the user's real folders.
    const auto key = shellFoldersKey.isEmpty() ? k_shellFoldersKey : shellFoldersKey;
    for (const auto& [folder, label] : { std::pair{ k_savedGames, "Saved Games" }, std::pair{ k_localLow, "AppData\\LocalLow" } }) {
        if (!shellFolderValue(key, folder.registryName).startsWith("%USERPROFILE%", Qt::CaseInsensitive)) {
            shared << label;
        }
    }
#endif
    return shared;
}

Result<> GameInstance::setProfileFoldersIsolated(bool isolated, const QString& shellFoldersKey)
{
#ifdef Q_OS_WIN
    const auto keyPath = shellFoldersKey.isEmpty() ? k_shellFoldersKey : shellFoldersKey;
    HKEY key = nullptr;
    if (const auto status = RegCreateKeyExW(HKEY_CURRENT_USER, keyPath.toStdWString().c_str(), 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE,
                                            nullptr, &key, nullptr);
        status != ERROR_SUCCESS) {
        return std::unexpected(QObject::tr("Could not open the Windows user folder settings: %1").arg(qt_error_string(static_cast<int>(status))));
    }
    const auto closeKey = qScopeGuard([key] { RegCloseKey(key); });
    const auto realProfile = QDir::fromNativeSeparators(qEnvironmentVariable("USERPROFILE"));
    QStringList elsewhere;
    for (const auto& [folder, id, label] : { std::tuple{ k_savedGames, &FOLDERID_SavedGames, "Saved Games" },
                                             std::tuple{ k_localLow, &FOLDERID_LocalAppDataLow, "AppData\\LocalLow" } }) {
        const auto name = QString::fromLatin1(folder.registryName).toStdWString();
        const auto entry = "%USERPROFILE%\\" + QDir::toNativeSeparators(folder.defaultPath);
        const bool present = RegQueryValueExW(key, name.c_str(), nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
        if (!isolated) {
            // only the entry this records goes; it names the default place, so the folder stays where it is
            if (present && shellFolderValue(keyPath, folder.registryName).compare(entry, Qt::CaseInsensitive) == 0) {
                if (const auto status = RegDeleteValueW(key, name.c_str()); status != ERROR_SUCCESS) {
                    return std::unexpected(QObject::tr("Could not change the Windows setting of %1: %2")
                                               .arg(label, qt_error_string(static_cast<int>(status))));
                }
            }
            continue;
        }
        if (present) {
            continue;  // through %USERPROFILE% it is isolated already; anywhere else the user moved it on purpose
        }
        PWSTR resolved = nullptr;
        if (SHGetKnownFolderPath(*id, KF_FLAG_DONT_VERIFY, nullptr, &resolved) != S_OK) {
            CoTaskMemFree(resolved);
            elsewhere << label;
            continue;
        }
        const auto current = QDir::fromNativeSeparators(QString::fromWCharArray(resolved));
        CoTaskMemFree(resolved);
        if (current.compare(FS::PathCombine(realProfile, folder.defaultPath), Qt::CaseInsensitive) != 0) {
            elsewhere << QString("%1 (%2)").arg(label, QDir::toNativeSeparators(current));
            continue;
        }
        const auto data = entry.toStdWString();
        if (const auto status = RegSetValueExW(key, name.c_str(), 0, REG_EXPAND_SZ, reinterpret_cast<const BYTE*>(data.c_str()),
                                               static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
            status != ERROR_SUCCESS) {
            return std::unexpected(
                QObject::tr("Could not change the Windows setting of %1: %2").arg(label, qt_error_string(static_cast<int>(status))));
        }
    }
    if (!elsewhere.isEmpty()) {
        return std::unexpected(QObject::tr("Windows does not keep %1 at the usual place in your user profile, so BetterNMM leaves it as "
                                           "it is and instances keep sharing it.")
                                   .arg(elsewhere.join(", ")));
    }
#else
    Q_UNUSED(isolated)
    Q_UNUSED(shellFoldersKey)
#endif
    return {};
}

Result<> GameInstance::prepareProfile() const
{
    if (isMain()) {
        return {};
    }
    TRY(profileRedirectionSupported());
    const auto root = profileRoot();
    for (const auto& folder : k_profileFolders) {
        const auto dir = FS::PathCombine(root, profileRelative(folder));
        if (!FS::ensureFolderPathExists(dir)) {
            return std::unexpected(QObject::tr("Could not create %1").arg(dir));
        }
    }
    if (const auto myGames = myGamesDir(); !myGames.isEmpty() && !FS::ensureFolderPathExists(myGames)) {
        return std::unexpected(QObject::tr("Could not create %1").arg(myGames));
    }
    if (const auto list = pluginListDir(); !list.isEmpty() && !FS::ensureFolderPathExists(list)) {
        return std::unexpected(QObject::tr("Could not create %1").arg(list));
    }
    return {};
}

QList<GameExecutable> GameInstance::executables() const
{
    QList<GameExecutable> out;
    const auto args = m_settings->get("GameArguments").toString();
    const auto gameName = m_settings->get("GameName").toString();
    out << GameExecutable{ .name = gameName.isEmpty() ? tr("Game") : gameName,
                           .path = m_settings->get("GameExecutable").toString(),
                           .arguments = args,
                           .builtIn = true };
    const auto def = definition();
    const auto custom = customExecutables();
    // a tool the user added for the co-op launcher stands in for it, with its own name and arguments
    if (!def.coopLauncher.isEmpty() && provides(def.coopLauncher) &&
        std::none_of(custom.begin(), custom.end(), [&](const GameExecutable& exe) { return sameProgram(exe.path, def.coopLauncher); })) {
        out << GameExecutable{ .name = tr("%1 (%2)").arg(def.coopLauncherName, QFileInfo(def.coopLauncher).fileName()),
                               .path = def.coopLauncher,
                               .arguments = def.coopLauncherArguments,
                               .builtIn = true };
    }
    if (!def.scriptExtender.isEmpty() && provides(def.scriptExtender)) {
        out << GameExecutable{ .name = tr("Script extender (%1)").arg(QFileInfo(def.scriptExtender).fileName()),
                               .path = def.scriptExtender,
                               .arguments = args,
                               .builtIn = true };
    }
    out << custom;
    return out;
}

bool GameInstance::provides(const QString& path) const
{
    if (QFileInfo::exists(FS::PathCombine(gameRoot(), path))) {
        return true;
    }
    // installed but not deployed yet: a launch deploys before the game starts. Every change to the mod list saves it, so
    // with the deployment newer the game folder has every enabled mod already, and the lookup (one file check per mod,
    // half a second for 81 mods on a cold disk) is skipped. The mod list loads on first use.
    const QFileInfo deployed(deploymentManifestPath());
    if (deployed.exists() && QFileInfo(FS::PathCombine(instanceRoot(), "mods.json")).lastModified() <= deployed.lastModified()) {
        return false;
    }
    auto* mods = const_cast<GameInstance*>(this)->modList();
    const auto enabled = mods->enabledMods();
    return std::any_of(enabled.begin(), enabled.end(),
                       [&](const ModEntry& mod) { return QFileInfo::exists(FS::PathCombine(mods->modDir(mod.id), path)); });
}

bool GameInstance::sameProgram(const QString& a, const QString& b) const
{
    const auto absolute = [this](const QString& path) { return QDir::cleanPath(QDir::isAbsolutePath(path) ? path : FS::PathCombine(gameRoot(), path)); };
    return absolute(a).compare(absolute(b), Qt::CaseInsensitive) == 0;
}

QList<GameExecutable> GameInstance::customExecutables() const
{
    QList<GameExecutable> out;
    const auto array = QJsonDocument::fromJson(m_settings->get("CustomExecutables").toString().toUtf8()).array();
    for (const auto& value : array) {
        const auto obj = value.toObject();
        GameExecutable exe{ .name = obj.value("name").toString(),
                            .path = obj.value("path").toString(),
                            .arguments = obj.value("arguments").toString(),
                            .workingDir = obj.value("workingDir").toString(),
                            .outputMod = obj.value("outputMod").toString() };
        if (!exe.name.isEmpty() && !exe.path.isEmpty()) {
            out << exe;
        }
    }
    return out;
}

void GameInstance::setCustomExecutables(const QList<GameExecutable>& list)
{
    QJsonArray array;
    for (const auto& exe : list) {
        QJsonObject obj{ { "name", exe.name }, { "path", exe.path }, { "arguments", exe.arguments }, { "workingDir", exe.workingDir } };
        if (!exe.outputMod.isEmpty()) {
            obj.insert("outputMod", exe.outputMod);
        }
        array.append(obj);
    }
    m_settings->set("CustomExecutables", QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

GameExecutable GameInstance::executable(const QString& name) const
{
    const auto all = executables();
    for (const auto& exe : all) {
        if (exe.name == name) {
            return exe;
        }
    }
    return all.first();
}

QString GameInstance::defaultExecutableName() const
{
    const auto all = executables();
    const auto chosen = chosenExecutableName(all);
    return chosen.isEmpty() ? automaticExecutableName(all) : chosen;
}

QString GameInstance::chosenExecutableName() const
{
    return chosenExecutableName(executables());
}

QString GameInstance::automaticExecutableName() const
{
    return automaticExecutableName(executables());
}

QString GameInstance::chosenExecutableName(const QList<GameExecutable>& all) const
{
    const auto chosen = m_settings->get("LaunchExecutable").toString();
    const auto exe = std::find_if(all.begin(), all.end(), [&](const GameExecutable& e) { return e.name == chosen; });
    if (chosen.isEmpty() || exe == all.end()) {
        return {};
    }
    // before LaunchChoiceVersion 1 the Settings page saved whatever Launch showed, picked or not: saved before the script
    // extender or co-op launcher was installed, the game stayed the choice and they were never used
    const auto coop = definition().coopLauncher;
    if (m_settings->get("LaunchChoiceVersion").toInt() < 1 && (exe->builtIn || (!coop.isEmpty() && sameProgram(exe->path, coop)))) {
        return {};
    }
    return chosen;
}

QString GameInstance::automaticExecutableName(const QList<GameExecutable>& all) const
{
    // the co-op launcher starts the game with its script extender; without one, the script extender. Starting the game
    // without what is installed for it is almost never wanted.
    const auto def = definition();
    for (const auto& path : { def.coopLauncher, def.scriptExtender }) {
        if (path.isEmpty()) {
            continue;
        }
        const auto exe = std::find_if(all.begin(), all.end(), [&](const GameExecutable& e) { return sameProgram(e.path, path); });
        // built-in entries are listed only when installed; a tool standing in for one is listed either way
        if (exe != all.end() && (exe->builtIn || provides(path))) {
            return exe->name;
        }
    }
    return all.first().name;
}

QString GameInstance::absoluteExecutablePath(const GameExecutable& exe) const
{
    return QDir::isAbsolutePath(exe.path) ? exe.path : FS::PathCombine(gameRoot(), exe.path);
}

LaunchTask* GameInstance::createLaunchTask(const QString& executableName)
{
    const auto exe = executable(executableName.isEmpty() ? defaultExecutableName() : executableName);
    auto process = LaunchTask::create(this);
    auto* pptr = process.get();

    process->appendStep(makeShared<TextPrint>(pptr, verboseDescription(exe.name), MessageLevel::Launcher));

    if (!getPreLoadCommand().isEmpty()) {
        auto step = makeShared<LaunchCommand>(pptr, getPreLoadCommand(), tr("Pre-Load"));
        step->setWorkingDirectory(gameRoot());
        process->appendStep(step);
    }

    process->appendStep(makeShared<PrepareGame>(pptr));

    if (!getPreLaunchCommand().isEmpty()) {
        auto step = makeShared<LaunchCommand>(pptr, getPreLaunchCommand(), tr("Pre-Launch"));
        step->setWorkingDirectory(gameRoot());
        process->appendStep(step);
    }

    // what a tool writes into the game folder goes into its output mod, even when it fails or is stopped
    std::shared_ptr<ToolRun> toolRun;
    if (!exe.builtIn && !exe.outputMod.trimmed().isEmpty()) {
        toolRun = std::make_shared<ToolRun>();
        toolRun->tool = exe.name;
        toolRun->outputMod = exe.outputMod.trimmed();
        toolRun->filter.excludePatterns = GameFiles::splitPatterns(m_settings->get("ExcludeFilePatterns").toString());
        // a tool kept in the game folder keeps its own state (caches, logs) next to it; the game folder and the data
        // folder themselves are where its output goes
        const auto own = QDir(gameRoot()).relativeFilePath(QFileInfo(absoluteExecutablePath(exe)).absolutePath());
        const auto dataPath = definition().dataPath;
        if (!own.startsWith("..") && !QDir::isAbsolutePath(own) && own != "." && QDir::cleanPath(own).compare(dataPath, Qt::CaseInsensitive) != 0) {
            toolRun->filter.ownDirs << own;
        }
        process->appendStep(makeShared<SnapshotGameFolder>(pptr, toolRun));
    }
    process->appendStep(makeShared<LaunchGameProcess>(pptr, exe));
    if (toolRun) {
        process->appendStep(makeShared<CaptureToolOutput>(pptr, toolRun));
    }

    if (!getPostExitCommand().isEmpty()) {
        auto step = makeShared<LaunchCommand>(pptr, getPostExitCommand(), tr("Post-Exit"));
        step->setWorkingDirectory(gameRoot());
        process->appendStep(step);
    }
    if (m_settings->get("QuitAfterGameStop").toBool()) {
        process->appendStep(makeShared<QuitAfterGameStop>(pptr));
    }
    m_launchProcess = std::move(process);
    emit launchTaskChanged(m_launchProcess.get());
    return m_launchProcess.get();
}

QStringList GameInstance::verboseDescription(const QString& executableName)
{
    const auto exe = executable(executableName.isEmpty() ? defaultExecutableName() : executableName);
    QStringList out;
    out << tr("Game: %1 (%2)").arg(m_settings->get("GameName").toString(), m_settings->get("GameDomain").toString());
    out << tr("Instance type: %1").arg(isMain() ? tr("Steam installation") : tr("Isolated instance"));
    out << tr("Game folder: %1").arg(QDir::toNativeSeparators(gameRoot()));
    if (!isMain()) {
        out << tr("Profile folder (Documents, AppData): %1").arg(QDir::toNativeSeparators(profileRoot()));
        out << tr("Built from: %1").arg(QDir::toNativeSeparators(sourceGameDir()));
    }
    out << tr("Executable: %1 (%2)").arg(exe.name, QDir::toNativeSeparators(absoluteExecutablePath(exe)));
    if (!exe.arguments.isEmpty()) {
        out << tr("Arguments: %1").arg(exe.arguments);
    }
    const auto mods = modList()->enabledMods();
    out << tr("Enabled mods: %1").arg(mods.size());
    for (const auto& mod : mods) {
        out << "  " + mod.name + (mod.version.isEmpty() ? QString() : " " + mod.version);
    }
    out << "";
    return out;
}

QMap<QString, QString> GameInstance::getVariables()
{
    QMap<QString, QString> out;
    out.insert("INST_NAME", name());
    out.insert("INST_ID", id());
    out.insert("INST_DIR", QDir::toNativeSeparators(QDir(instanceRoot()).absolutePath()));
    out.insert("INST_GAME_DIR", QDir::toNativeSeparators(QDir(gameRoot()).absolutePath()));
    out.insert("INST_DATA_DIR", QDir::toNativeSeparators(QDir(dataDir()).absolutePath()));
    out.insert("INST_MODS_DIR", QDir::toNativeSeparators(QDir(modsRoot()).absolutePath()));
    out.insert("INST_PROFILE_DIR", isMain() ? QString() : QDir::toNativeSeparators(QDir(profileRoot()).absolutePath()));
    out.insert("INST_MY_GAMES_DIR", QDir::toNativeSeparators(myGamesDir()));
    out.insert("INST_GAME", m_settings->get("GameDomain").toString());
    return out;
}

QProcessEnvironment GameInstance::createEnvironment()
{
    // commands and tools see the same profile as the game, so they read and write this instance's files
    auto env = QProcessEnvironment::systemEnvironment();
    if (!isMain()) {
        const auto profile = QDir::toNativeSeparators(QDir(profileRoot()).absolutePath());
        env.insert("USERPROFILE", profile);
        env.insert("APPDATA", profile + '\\' + QDir::toNativeSeparators(profileRelative(k_roamingAppData)));
        env.insert("LOCALAPPDATA", profile + '\\' + QDir::toNativeSeparators(localAppDataSubfolder()));
    }
    if (const int appId = steamAppId(); appId > 0) {
        env.insert("SteamAppId", QString::number(appId));
        env.insert("SteamGameId", QString::number(appId));
    }
    const auto variables = getVariables();
    for (auto it = variables.begin(); it != variables.end(); ++it) {
        env.insert(it.key(), it.value());
    }
    const auto custom = QJsonDocument::fromJson(m_settings->get("Env").toString().toUtf8()).object();
    for (auto it = custom.begin(); it != custom.end(); ++it) {
        env.insert(it.key(), it.value().toString());
    }
    return env;
}

QProcessEnvironment GameInstance::createLaunchEnvironment()
{
    return createEnvironment();
}

QStringList GameInstance::getLogFileSearchPaths()
{
    QStringList out;
    if (const auto myGames = myGamesDir(); !myGames.isEmpty()) {
        out << myGames;
    }
    out << gameRoot();
    return out;
}

QString GameInstance::getStatusbarDescription()
{
    QString description = tr("%1, %2").arg(m_settings->get("GameName").toString(), isMain() ? tr("Steam installation") : tr("isolated instance"));
    if (m_settings->get("ShowGameTime").toBool() && lastTimePlayed() > 0) {
        description += tr(", last played for %1").arg(QString::number(lastTimePlayed() / 60) + tr(" min"));
    }
    if (hasCrashed()) {
        description += tr(", has crashed.");
    }
    return description;
}
