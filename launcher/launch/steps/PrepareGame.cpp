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

#include "PrepareGame.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include "FileSystem.h"
#include "game/Deployer.h"
#include "game/GameFiles.h"
#include "game/PluginList.h"
#include "game/Steam.h"
#include "launch/LaunchTask.h"

#include <QProcess>

namespace {
int g_steamTimeout = 120000;
}

PrepareGame::PrepareGame(LaunchTask* parent) : LaunchStep(parent)
{
    m_steamWait.setInterval(500);
    connect(&m_steamWait, &QTimer::timeout, this, &PrepareGame::checkSteam);
}

void PrepareGame::setSteamTimeout(int ms)
{
    g_steamTimeout = ms;
}

PrepareGame::~PrepareGame() = default;

void PrepareGame::executeTask()
{
    auto* instance = m_parent->instance();
    if (!QFileInfo(instance->gameRoot()).isDir()) {
        emitFailed(tr("The game folder %1 does not exist.").arg(QDir::toNativeSeparators(instance->gameRoot())));
        return;
    }
    if (auto prepared = instance->prepareProfile(); !prepared) {
        emit logLine(prepared.error(), MessageLevel::Fatal);
        emitFailed(prepared.error());
        return;
    }
    if (!instance->isMain()) {
        QSet<QString> deployed;
        if (auto manifest = DeploymentManifest::load(instance->deploymentManifestPath())) {
            deployed = manifest->keys();
        }
        auto unshared = GameFiles::unshareLocalFiles(instance->baseManifestPath(), instance->gameRoot(), instance->overwrittenDir(), deployed,
                                                     instance->localPatterns());
        if (!unshared) {
            // the game still runs; what it would rewrite in place is shared with the Steam installation until next time
            emit logLine(tr("Could not make the instance's own copies of its executables and settings files: %1").arg(unshared.error()),
                         MessageLevel::Warning);
        } else if (*unshared > 0) {
            emit logLine(tr("%n file(s) were still shared with the Steam installation (executables, DLLs, settings files); they are "
                            "this instance's own copies now, so changes to them stay here.",
                            nullptr, *unshared),
                         MessageLevel::Launcher);
        }
    }
    if (const auto shared = GameInstance::sharedProfileFolders(); !instance->isMain() && !shared.isEmpty()) {
        emit logLine(tr("Not isolated: %1. Windows gives every program the same %1, so a game that keeps its saves there shares "
                        "them with your normal profile. Documents and AppData\\Local and \\Roaming are this instance's own. "
                        "Settings > General > Saves can give each instance its own %1 too.")
                         .arg(shared.join(", ")),
                     MessageLevel::Warning);
    }

    if (!instance->isMain() && instance->steamAppId() > 0) {
        // Chromium holds this file while Steam's browser runs with this profile: Steam was started by this instance's game
        if (const auto lock = FS::PathCombine(instance->profileRoot(), "AppData/Local/Steam/htmlcache/lockfile");
            QFileInfo::exists(lock) && !QFile(lock).open(QIODevice::ReadOnly)) {
            emit logLine(tr("Steam is running with this instance's profile: a launch while Steam was closed let the game start it. "
                            "Whatever Steam starts now (any game, Skyrim from Steam too) uses this instance's Documents and "
                            "AppData. Exit Steam (Steam > Exit) and start it again."),
                         MessageLevel::Warning);
        }
    }

    if (!instance->isMain() && instance->steamAppId() > 0) {
        // makes the Steam API accept a game started outside Steam's own folder
        const auto appIdFile = FS::PathCombine(instance->gameRoot(), "steam_appid.txt");
        if (!QFileInfo::exists(appIdFile)) {
            if (auto written = FS::write(appIdFile, QByteArray::number(instance->steamAppId())); !written) {
                emit logLine(tr("Could not write steam_appid.txt: %1").arg(written.error()), MessageLevel::Warning);
            }
        }
    }

    if (!instance->isMain() && instance->steamAppId() > 0 && !Steam::clientState().loggedIn) {
        startSteamFirst();
        return;
    }
    deploy();
}

void PrepareGame::startSteamFirst()
{
    const auto state = Steam::clientState();
    if (!state.running) {
        if (state.exe.isEmpty() || !QFileInfo::exists(state.exe)) {
            const auto error = tr("Steam is not running and BetterNMM can not find it to start it. Start Steam, log in, and launch again.");
            emit logLine(error, MessageLevel::Fatal);
            emitFailed(error);
            return;
        }
        // BetterNMM's own environment, not the instance's: Steam keeps its settings in the real profile
        if (!QProcess::startDetached(state.exe, { "-silent" }, QFileInfo(state.exe).absolutePath())) {
            const auto error = tr("Could not start Steam (%1). Start it, log in, and launch again.").arg(QDir::toNativeSeparators(state.exe));
            emit logLine(error, MessageLevel::Fatal);
            emitFailed(error);
            return;
        }
        emit logLine(tr("Starting Steam first: a game that finds Steam closed starts it itself, and Steam then runs the game from "
                        "the Steam installation instead of this instance."),
                     MessageLevel::Launcher);
    } else {
        emit logLine(tr("Waiting for Steam to log in..."), MessageLevel::Launcher);
    }
    m_steamSince.start();
    m_steamWait.start();
}

void PrepareGame::checkSteam()
{
    if (Steam::clientState().loggedIn) {
        m_steamWait.stop();
        emit logLine(tr("Steam is running."), MessageLevel::Launcher);
        deploy();
        return;
    }
    if (m_steamSince.elapsed() > g_steamTimeout) {
        m_steamWait.stop();
        const auto error = tr("Steam did not start and log in within %n second(s). Start Steam, log in, and launch again.", nullptr,
                              g_steamTimeout / 1000);
        emit logLine(error, MessageLevel::Fatal);
        emitFailed(error);
    }
}

bool PrepareGame::abort()
{
    if (!m_steamWait.isActive()) {
        return false;
    }
    m_steamWait.stop();
    emitAborted();
    return true;
}

void PrepareGame::deploy()
{
    auto* instance = m_parent->instance();
    emit logLine(tr("Deploying %n enabled mod(s)...", nullptr, static_cast<int>(instance->modList()->enabledMods().size())),
                 MessageLevel::Launcher);
    m_deploy = instance->createDeployTask();
    connect(m_deploy.get(), &Task::finished, this, &PrepareGame::afterDeploy);
    m_deploy->start();
}

void PrepareGame::afterDeploy()
{
    if (!m_deploy->wasSuccessful()) {
        const auto error = tr("Deploying mods failed: %1").arg(m_deploy->failReason());
        emit logLine(error, MessageLevel::Fatal);
        emitFailed(error);
        return;
    }
    emit logLine(tr("Mods deployed (%1 files linked, %2 removed).").arg(m_deploy->added()).arg(m_deploy->removed()), MessageLevel::Launcher);
    for (const auto& warning : m_deploy->warnings()) {
        emit logLine(warning, MessageLevel::Warning);
    }

    auto* instance = m_parent->instance();
    if (instance->definition().hasPlugins()) {
        auto* plugins = instance->pluginList();
        auto refreshed = plugins->refresh();
        auto written = refreshed ? plugins->save() : refreshed;
        if (!written) {
            const auto error = tr("Writing the load order failed: %1").arg(written.error());
            emit logLine(error, MessageLevel::Fatal);
            emitFailed(error);
            return;
        }
        int active = 0;
        for (const auto& plugin : plugins->plugins()) {
            active += plugin.active ? 1 : 0;
        }
        emit logLine(tr("Load order written: %1 of %2 plugins active.").arg(active).arg(plugins->plugins().size()), MessageLevel::Launcher);
    }
    emitSucceeded();
}
