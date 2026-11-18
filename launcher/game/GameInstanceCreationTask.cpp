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

#include "GameInstanceCreationTask.h"

#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QImage>
#include <QStandardPaths>
#include <QStorageInfo>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "game/GameFiles.h"
#include "game/Steam.h"
#include "icons/IconList.h"
#include "settings/INISettingsObject.h"

GameInstanceCreationTask::GameInstanceCreationTask(GameDefinition game, QString sourceDir, int steamAppId, GameInstance::Kind kind)
    : m_game(std::move(game)), m_sourceDir(std::move(sourceDir)), m_steamAppId(steamAppId), m_kind(kind)
{}

void GameInstanceCreationTask::executeTask()
{
    setStatus(tr("Creating instance of %1...").arg(m_game.name));
    if (!QFileInfo(m_sourceDir).isDir()) {
        emitFailed(tr("The game folder %1 does not exist.").arg(QDir::toNativeSeparators(m_sourceDir)));
        return;
    }
    if (m_kind == GameInstance::Kind::Main) {
        if (auto* existing = GameInstance::mainInstanceFor(m_sourceDir, m_steamAppId)) {
            emitFailed(tr("\"%1\" already manages the Steam installation of %2; there can only be one.").arg(existing->name(), m_game.name));
            return;
        }
    }

    auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg"));
    settings->registerSetting("InstanceType", "");
    settings->set("InstanceType", "Game");
    GameInstance instance(m_globalSettings, std::move(settings), m_stagingPath);
    instance.settings()->set("InstanceKind", m_kind == GameInstance::Kind::Main ? "Main" : "Isolated");
    instance.settings()->set("SourceGameDir", QDir::cleanPath(QFileInfo(m_sourceDir).absoluteFilePath()));
    instance.settings()->set("SteamAppId", m_steamAppId);
    GameInstance::applyDefinition(instance.settings(), m_game);
    instance.setName(name());
    instance.setIconKey(m_instIcon);
    FS::ensureFolderPathExists(instance.modsRoot());

    if (m_kind == GameInstance::Kind::Main) {
        emitSucceeded();
        return;
    }

    if (auto prepared = instance.prepareProfile(); !prepared) {
        emitFailed(prepared.error());
        return;
    }

    // seed the profile from the user's real one; only reads there, and never the saves
    if (!m_game.myGamesFolder.isEmpty()) {
        const auto realMyGames = FS::PathCombine(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), "My Games", m_game.myGamesFolder);
        for (const auto& ini : m_game.iniFiles) {
            const auto from = FS::PathCombine(realMyGames, ini);
            if (QFileInfo::exists(from)) {
                if (auto copied = GameFiles::placeFile(from, FS::PathCombine(instance.myGamesDir(), ini), GameFiles::PlaceMode::Copy); !copied) {
                    emitFailed(copied.error());
                    return;
                }
                m_messages << tr("Copied %1 from your Steam profile.").arg(ini);
            }
        }
    }
    if (!m_game.appDataFolder.isEmpty()) {
        const auto realList = FS::PathCombine(QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")), m_game.appDataFolder);
        for (const auto& file : { "plugins.txt", "loadorder.txt" }) {
            const auto from = FS::PathCombine(realList, file);
            if (QFileInfo::exists(from)) {
                if (auto copied = GameFiles::placeFile(from, FS::PathCombine(instance.pluginListDir(), file), GameFiles::PlaceMode::Copy); !copied) {
                    emitFailed(copied.error());
                    return;
                }
            }
        }
    }

    auto sync = instance.createBaseSyncTask();
    auto* syncPtr = sync.get();
    setAbortable(true);
    m_sync.reset(sync.release());
    connect(syncPtr, &Task::progress, this, &Task::setProgress);
    connect(syncPtr, &Task::status, this, &Task::setStatus);
    connect(syncPtr, &Task::failed, this, [this](const QString& reason) { emitFailed(tr("Linking the game files failed: %1").arg(reason)); });
    connect(syncPtr, &Task::aborted, this, [this] { emitAborted(); });
    connect(syncPtr, &Task::succeeded, this, [this, syncPtr] {
        m_messages << syncPtr->messages();
        for (const auto& warning : syncPtr->warnings()) {
            logWarning(warning);
        }
        emitSucceeded();
    });
    m_sync->start();
}

bool GameInstanceCreationTask::abort()
{
    if (m_sync) {
        return m_sync->abort();
    }
    return true;
}

namespace GameIcons {

bool cornerOnly(const QImage& image)
{
    constexpr int shellIcon = 48;
    if (image.width() <= shellIcon || image.height() <= shellIcon || !image.hasAlphaChannel()) {
        return false;
    }
    const auto argb = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < argb.height(); y++) {
        const auto* line = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = y < shellIcon ? shellIcon : 0; x < argb.width(); x++) {
            if (qAlpha(line[x]) != 0) {
                return false;
            }
        }
    }
    return true;
}

QString iconForGame(const GameDefinition& game, const QString& gameDir)
{
    const auto key = game.domain.isEmpty() ? QString("default") : game.domain;
    auto* icons = APPLICATION->icons();
    const auto target = FS::PathCombine(icons->getDirectory(), key + ".png");
    // earlier versions saved the broken corner-only image for some games; those get extracted again
    if (icons->icon(key) && !cornerOnly(QImage(target))) {
        return key;
    }
    const auto exe = FS::PathCombine(gameDir, game.executable);
    if (!QFileInfo::exists(exe)) {
        return icons->icon(key) ? key : QString("default");
    }
    const auto fileIcon = QFileIconProvider().icon(QFileInfo(exe));
    auto pixmap = fileIcon.pixmap(256, 256);
    if (cornerOnly(pixmap.toImage())) {
        pixmap = fileIcon.pixmap(48, 48);
    }
    if (pixmap.isNull()) {
        return "default";
    }
    if (!FS::ensureFilePathExists(target) || !pixmap.save(target, "PNG")) {
        return "default";
    }
    icons->directoryChanged(icons->getDirectory());
    icons->fileChanged(target);
    return key;
}

}  // namespace GameIcons

namespace InstancePlacement {

QString instanceDirFor(const QString& gameDir)
{
    const auto primary = QDir(APPLICATION->instances()->primaryDir()).absolutePath();
    if (GameFiles::isSameVolume(primary, gameDir)) {
        return APPLICATION->instances()->primaryDir();
    }
    // next to the game's Steam library, which is always writable, otherwise at the root of the game's drive
    QString base;
    const auto cleanGame = QDir::cleanPath(QFileInfo(gameDir).absoluteFilePath());
    for (const auto& library : Steam::libraryFolders(Steam::steamRoot())) {
        if (cleanGame.startsWith(library + "/", Qt::CaseInsensitive) && library.length() > base.length()) {
            base = library;
        }
    }
    if (base.isEmpty()) {
        base = QStorageInfo(cleanGame).rootPath();
    }
    return QDir::cleanPath(FS::PathCombine(base, "BetterNMM Instances"));
}

}  // namespace InstancePlacement
