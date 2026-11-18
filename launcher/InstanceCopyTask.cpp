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

#include "InstanceCopyTask.h"

#include <QDir>
#include <QDirListing>
#include <QFileInfo>
#include <QtConcurrent>

#include "FileSystem.h"
#include "game/GameFiles.h"
#include "game/GameInstance.h"
#include "settings/INISettingsObject.h"

namespace {

/// Copies (or hard links) every file below @p from into @p to, skipping relative paths that start with an entry of @p skip;
/// stops when @p cancel is set
Result<> copyTree(const QString& from, const QString& to, bool link, const std::atomic_bool& cancel, const QStringList& skip = {})
{
    if (!QFileInfo(from).isDir()) {
        return {};
    }
    const QDir root(from);
    for (const auto& entry : QDirListing(from, QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::IncludeHidden)) {
        const auto rel = root.relativeFilePath(entry.absoluteFilePath());
        if (std::any_of(skip.begin(), skip.end(), [&rel](const QString& s) { return rel.compare(s, Qt::CaseInsensitive) == 0 || rel.startsWith(s + "/", Qt::CaseInsensitive); })) {
            continue;
        }
        if (cancel) {
            return std::unexpected(QObject::tr("Cancelled"));
        }
        const auto target = FS::PathCombine(to, rel);
        if (entry.isDir()) {
            FS::ensureFolderPathExists(target);
            continue;
        }
        TRY(GameFiles::placeFile(entry.absoluteFilePath(), target, link ? GameFiles::PlaceMode::Link : GameFiles::PlaceMode::Copy, &cancel));
    }
    return {};
}

Result<> copyFile(const QString& from, const QString& to)
{
    if (!QFileInfo::exists(from)) {
        return {};
    }
    return GameFiles::placeFile(from, to, GameFiles::PlaceMode::Copy).transform([](auto) {});
}

}  // namespace

InstanceCopyTask::InstanceCopyTask(GameInstance* origInstance, const InstanceCopyPrefs& prefs) : m_origInstance(origInstance), m_prefs(prefs) {}

void InstanceCopyTask::executeTask()
{
    setStatus(tr("Copying instance %1").arg(m_origInstance->name()));
    setAbortable(true);
    connect(&m_watcher, &QFutureWatcher<Result<>>::finished, this, &InstanceCopyTask::filesCopied);
    m_watcher.setFuture(QtConcurrent::run([this] { return copyFiles(); }));
}

Result<> InstanceCopyTask::copyFiles()
{
    const auto from = m_origInstance->instanceRoot();
    TRY(copyFile(FS::PathCombine(from, "instance.cfg"), FS::PathCombine(m_stagingPath, "instance.cfg")));

    if (m_prefs.copyMods) {
        TRY(copyTree(m_origInstance->modsRoot(), FS::PathCombine(m_stagingPath, "mods"), m_prefs.linkModFiles, m_aborted));
        TRY(copyFile(FS::PathCombine(from, "mods.json"), FS::PathCombine(m_stagingPath, "mods.json")));
        TRY(copyFile(FS::PathCombine(from, "loadorder.txt"), FS::PathCombine(m_stagingPath, "loadorder.txt")));
    }

    const auto def = m_origInstance->definition();
    const auto myGames = FS::PathCombine(GameInstance::documentsSubfolder(), "My Games", def.myGamesFolder);
    const auto stagedMyGames = def.myGamesFolder.isEmpty() ? QString() : FS::PathCombine(m_stagingPath, "profile", myGames);
    if (m_origInstance->isMain()) {
        // the Steam instance's profile is the user's real one: take only the game's INIs and, when asked, a copy of the saves
        if (m_prefs.copySettings && !stagedMyGames.isEmpty()) {
            for (const auto& ini : def.iniFiles) {
                TRY(copyFile(FS::PathCombine(m_origInstance->myGamesDir(), ini), FS::PathCombine(stagedMyGames, ini)));
            }
        }
        if (m_prefs.copySaves && !stagedMyGames.isEmpty()) {
            TRY(copyTree(m_origInstance->savesDir(), FS::PathCombine(stagedMyGames, "Saves"), false, m_aborted));
        }
    } else {
        QStringList skip;
        if (!m_prefs.copySaves && !def.myGamesFolder.isEmpty()) {
            skip << myGames + "/Saves";
        }
        if (m_prefs.copySettings) {
            TRY(copyTree(m_origInstance->profileRoot(), FS::PathCombine(m_stagingPath, "profile"), false, m_aborted, skip));
        } else if (m_prefs.copySaves && !stagedMyGames.isEmpty()) {
            TRY(copyTree(m_origInstance->savesDir(), FS::PathCombine(stagedMyGames, "Saves"), false, m_aborted));
        }
    }
    if (!m_prefs.copyMods) {
        // the copied plugins.txt would name plugins of mods the new instance does not have
        if (!def.appDataFolder.isEmpty()) {
            const auto list = FS::PathCombine(m_stagingPath, "profile", GameInstance::localAppDataSubfolder(), def.appDataFolder);
            TRY(GameFiles::removeFile(FS::PathCombine(list, "plugins.txt")));
            TRY(GameFiles::removeFile(FS::PathCombine(list, "loadorder.txt")));
        }
    }
    return {};
}

void InstanceCopyTask::filesCopied()
{
    const auto result = m_watcher.result();
    if (m_aborted) {
        emitAborted();
        return;
    }
    if (!result) {
        emitFailed(tr("Copying the instance failed: %1").arg(result.error()));
        return;
    }

    auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg"));
    GameInstance staged(m_globalSettings, std::move(settings), m_stagingPath);
    staged.setName(name());
    staged.setIconKey(m_instIcon);
    staged.regenerateUuid();
    staged.settings()->set("InstanceKind", "Isolated");
    staged.settings()->set("LaunchExecutable", "");
    if (!m_prefs.keepPlaytime) {
        staged.resetTimePlayed();
    }
    if (auto prepared = staged.prepareProfile(); !prepared) {
        emitFailed(prepared.error());
        return;
    }

    m_sync.reset(staged.createBaseSyncTask().release());
    connect(m_sync.get(), &Task::succeeded, this, [this] { emitSucceeded(); });
    connect(m_sync.get(), &Task::failed, this, [this](const QString& reason) { emitFailed(tr("Linking the game files failed: %1").arg(reason)); });
    connect(m_sync.get(), &Task::aborted, this, [this] { emitAborted(); });
    connect(m_sync.get(), &Task::progress, this, &Task::setProgress);
    connect(m_sync.get(), &Task::status, this, &Task::setStatus);
    m_sync->start();
}

bool InstanceCopyTask::abort()
{
    m_aborted = true;
    if (m_sync) {
        return m_sync->abort();
    }
    return true;
}
