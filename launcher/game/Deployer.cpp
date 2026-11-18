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

#include "Deployer.h"

#include <QDir>
#include <QDirListing>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QtConcurrent>

#include <algorithm>

#include "FileSystem.h"
#include "game/GameFiles.h"

Result<DeploymentManifest> DeploymentManifest::load(const QString& path)
{
    DeploymentManifest manifest;
    if (!QFile::exists(path)) {
        return manifest;
    }
    TRY_INTO(auto data, FS::read(path));
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        return std::unexpected(QObject::tr("%1 is corrupt: %2").arg(path, error.errorString()));
    }
    for (const auto& value : doc.object().value("files").toArray()) {
        const auto obj = value.toObject();
        DeployedFile file{ .path = obj.value("path").toString(),
                           .mod = obj.value("mod").toString(),
                           .stamp = obj.value("stamp").toInteger(),
                           .displacedOriginal = obj.value("displacedOriginal").toBool(),
                           .fileId = obj.value("fileId").toString().toULongLong() };
        if (!file.path.isEmpty()) {
            manifest.files.insert(file.path.toLower(), file);
        }
    }
    for (const auto& dir : doc.object().value("createdDirs").toArray()) {
        manifest.createdDirs << dir.toString();
    }
    return manifest;
}

Result<> DeploymentManifest::save(const QString& path) const
{
    QJsonArray files;
    for (const auto& file : this->files) {
        QJsonObject obj{ { "path", file.path }, { "mod", file.mod }, { "stamp", file.stamp }, { "displacedOriginal", file.displacedOriginal } };
        if (file.fileId != 0) {
            // a string: JSON numbers lose 64-bit precision
            obj.insert("fileId", QString::number(file.fileId));
        }
        files.append(obj);
    }
    return FS::write(path, QJsonDocument(QJsonObject{ { "files", files }, { "createdDirs", QJsonArray::fromStringList(createdDirs) } })
                               .toJson(QJsonDocument::Indented));
}

QSet<QString> DeploymentManifest::keys() const
{
    QSet<QString> out;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        out.insert(it.key());
    }
    return out;
}

DeployTask::DeployTask(Params params) : m_params(std::move(params)) {}

namespace DeployedChanges {

Result<QList<DeployedFile>> find(const QString& manifestPath, const QString& gameDir)
{
    TRY_INTO(const auto manifest, DeploymentManifest::load(manifestPath));
    QList<DeployedFile> out;
    for (const auto& file : manifest.files) {
        const auto target = FS::PathCombine(gameDir, file.path);
        if (file.fileId != 0 && QFileInfo::exists(target) && GameFiles::fileIndex(target) != file.fileId) {
            out << file;
        }
    }
    return out;
}

namespace {

/// The changed file at @p path and its manifest entry, or why there is none
Result<DeployedFile> changedEntry(const DeploymentManifest& manifest, const QString& gameDir, const QString& path)
{
    const auto entry = manifest.files.constFind(path.toLower());
    const auto target = FS::PathCombine(gameDir, path);
    if (entry == manifest.files.cend() || entry->fileId == 0 || !QFileInfo::exists(target) || GameFiles::fileIndex(target) == entry->fileId) {
        return std::unexpected(QObject::tr("%1 is not a changed mod file (any more).").arg(QDir::toNativeSeparators(path)));
    }
    return *entry;
}

}  // namespace

Result<> saveIntoMod(const QString& manifestPath, const QString& gameDir, const QString& modsDir, const QString& path)
{
    TRY_INTO(auto manifest, DeploymentManifest::load(manifestPath));
    TRY_INTO(const auto entry, changedEntry(manifest, gameDir, path));
    const auto target = FS::PathCombine(gameDir, entry.path);
    const auto modFile = FS::PathCombine(modsDir, entry.mod, entry.path);
    // beside the mod's file first, so a failure leaves both versions where they were
    const auto incoming = modFile + ".betternmm-new";
    TRY(GameFiles::moveFile(target, incoming));
    if (auto removed = GameFiles::removeFile(modFile); !removed) {
        TRY(GameFiles::moveFile(incoming, target));
        return removed;
    }
    TRY(GameFiles::moveFile(incoming, modFile));
    TRY(GameFiles::placeFile(modFile, target, GameFiles::PlaceMode::Link).transform([](auto) {}));
    manifest.files[entry.path.toLower()].fileId = GameFiles::fileIndex(target);
    return manifest.save(manifestPath);
}

Result<> restore(const QString& manifestPath,
                 const QString& gameDir,
                 const QString& modsDir,
                 const QString& path,
                 const std::function<bool(const QString&)>& discard)
{
    TRY_INTO(auto manifest, DeploymentManifest::load(manifestPath));
    TRY_INTO(const auto entry, changedEntry(manifest, gameDir, path));
    const auto target = FS::PathCombine(gameDir, entry.path);
    const auto modFile = FS::PathCombine(modsDir, entry.mod, entry.path);
    if (!QFileInfo::exists(modFile)) {
        return std::unexpected(QObject::tr("%1 has no %2 any more; reinstall it.").arg(entry.mod, QDir::toNativeSeparators(entry.path)));
    }
    if (!discard(target)) {
        return std::unexpected(QObject::tr("Could not move %1 to the Recycle Bin; nothing was changed.").arg(QDir::toNativeSeparators(target)));
    }
    TRY(GameFiles::placeFile(modFile, target, GameFiles::PlaceMode::Link).transform([](auto) {}));
    manifest.files[entry.path.toLower()].fileId = GameFiles::fileIndex(target);
    return manifest.save(manifestPath);
}

}  // namespace DeployedChanges

namespace {

/// What is at @p target now is still the file deployment placed there. A tool that saves by writing a new file (xEdit),
/// a Steam update or the user may have put another file there since; that file is not deployment's to delete.
bool stillDeployed(const DeployedFile& deployed, const QString& target)
{
    return deployed.fileId == 0 || GameFiles::fileIndex(target) == deployed.fileId;
}

}  // namespace

void DeployTask::executeTask()
{
    setStatus(tr("Deploying mods..."));
    auto* watcher = new QFutureWatcher<Result<>>(this);
    connect(watcher, &QFutureWatcher<Result<>>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (result) {
            emitSucceeded();
        } else {
            emitFailed(result.error());
        }
    });
    watcher->setFuture(QtConcurrent::run([this] { return perform(); }));
}

Result<> DeployTask::perform()
{
    TRY_INTO(auto manifest, DeploymentManifest::load(m_params.manifestPath));
    // the manifest is saved however far deployment gets, so a failure never orphans files
    auto saveManifest = qScopeGuard([&] {
        if (auto saved = manifest.save(m_params.manifestPath); !saved) {
            qCritical() << "Could not save the deployment manifest:" << saved.error();
        }
    });

    // deploys that overlapped set a mod's own files aside as if they were the game's; put back once the mod goes,
    // they would stay behind. Only the very same file (a hard link of it) is taken for one, never a game file.
    for (auto it = manifest.files.begin(); it != manifest.files.end(); ++it) {
        if (!it->displacedOriginal) {
            continue;
        }
        const auto original = FS::PathCombine(m_params.overwrittenDir, it->path);
        if (QFileInfo::exists(original) && GameFiles::isSameFile(original, FS::PathCombine(m_params.gameDir, it->path))) {
            TRY(GameFiles::removeFile(original));
            GameFiles::pruneEmptyDirs(QFileInfo(original).absolutePath(), m_params.overwrittenDir);
            it->displacedOriginal = false;
        }
    }

    struct Wanted {
        QString path;
        QString source;
        QString mod;
        qint64 stamp;
    };
    QMap<QString, Wanted> wanted;
    for (const auto& mod : m_params.enabledMods) {
        const QDir root(FS::PathCombine(m_params.modsDir, mod.id));
        for (const auto& file : QDirListing(root.path(), QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly |
                                                             QDirListing::IteratorFlag::IncludeHidden)) {
            const auto rel = root.relativeFilePath(file.absoluteFilePath());
            wanted.insert(rel.toLower(), Wanted{ .path = rel, .source = file.absoluteFilePath(), .mod = mod.id, .stamp = mod.stamp });
        }
    }

    const qsizetype total = manifest.files.size() + wanted.size();
    qsizetype done = 0;
    auto progress = [&] {
        if (++done % 64 == 0) {
            QMetaObject::invokeMethod(this, [this, done, total] { setProgress(done, total); }, Qt::QueuedConnection);
        }
    };

    // take out files whose mod is gone, disabled, outranked or reinstalled
    for (const auto& key : manifest.files.keys()) {
        progress();
        const auto deployed = manifest.files.value(key);
        const auto it = wanted.constFind(key);
        if (it != wanted.cend() && it->mod == deployed.mod && it->stamp == deployed.stamp) {
            continue;
        }
        const auto target = FS::PathCombine(m_params.gameDir, deployed.path);
        if (QFileInfo::exists(target) && !stillDeployed(deployed, target)) {
            // it is someone else's file now; putting the original back would delete it as well
            auto note = tr("Kept %1: it was changed after %2 deployed it, so it stays in the game folder.")
                            .arg(QDir::toNativeSeparators(deployed.path), deployed.mod);
            const auto original = FS::PathCombine(m_params.overwrittenDir, deployed.path);
            if (deployed.displacedOriginal && QFileInfo::exists(original)) {
                note += ' ' + tr("The game's file it had replaced is in %1.").arg(QDir::toNativeSeparators(original));
            }
            logWarning(note);
            manifest.files.remove(key);
            m_kept++;
            continue;
        }
        TRY(GameFiles::removeFile(target));
        if (deployed.displacedOriginal) {
            const auto original = FS::PathCombine(m_params.overwrittenDir, deployed.path);
            if (QFileInfo::exists(original)) {
                TRY(GameFiles::moveFile(original, target));
                GameFiles::pruneEmptyDirs(QFileInfo(original).absolutePath(), m_params.overwrittenDir);
            }
        }
        manifest.files.remove(key);
        m_removed++;
    }

    // put the wanted files in place
    for (auto it = wanted.cbegin(); it != wanted.cend(); ++it) {
        progress();
        const auto target = FS::PathCombine(m_params.gameDir, it->path);
        if (const auto known = manifest.files.find(it.key()); known != manifest.files.end()) {
            if (!QFileInfo::exists(target)) {
                // deleted from the game folder by someone else: the mod still wants it there
                TRY(GameFiles::placeFile(it->source, target, GameFiles::PlaceMode::Link).transform([](auto) {}));
                known->fileId = GameFiles::fileIndex(target);
                m_added++;
            } else if (known->fileId == 0 && GameFiles::isSameFile(it->source, target)) {
                // deployed before identities were recorded: the mod's own file there is known from now on
                known->fileId = GameFiles::fileIndex(target);
            }
            continue;
        }
        bool displaced = false;
        if (QFileInfo::exists(target) && GameFiles::isSameFile(it->source, target)) {
            // already this mod's file (a deploy that stopped before saving what it placed): taking it as a game file
            // would bring a copy of it back once the mod is removed
            DeployedFile adopted{ .path = it->path, .mod = it->mod, .stamp = it->stamp, .displacedOriginal = false };
            adopted.fileId = GameFiles::fileIndex(target);
            manifest.files.insert(it.key(), adopted);
            m_added++;
            continue;
        }
        if (QFileInfo::exists(target)) {
            const auto original = FS::PathCombine(m_params.overwrittenDir, it->path);
            if (QFileInfo::exists(original)) {
                // left from a file kept because it was changed after deployment: the game folder's file replaced it then
                TRY(GameFiles::removeFile(original));
            }
            TRY(GameFiles::moveFile(target, original));
            displaced = true;
        } else {
            // remember every folder deployment creates so purging leaves no empty folders behind
            QStringList missing;
            for (auto dir = QFileInfo(target).absolutePath(); !QFileInfo::exists(dir); dir = QFileInfo(dir).absolutePath()) {
                missing.prepend(QDir(m_params.gameDir).relativeFilePath(dir));
            }
            for (const auto& dir : missing) {
                if (!manifest.createdDirs.contains(dir, Qt::CaseInsensitive)) {
                    manifest.createdDirs << dir;
                }
            }
        }
        // a file deployment created must be recorded before anything else can fail
        manifest.files.insert(it.key(), DeployedFile{ .path = it->path, .mod = it->mod, .stamp = it->stamp, .displacedOriginal = displaced });
        auto placed = GameFiles::placeFile(it->source, target, GameFiles::PlaceMode::Link);
        if (!placed) {
            manifest.files.remove(it.key());
            if (displaced) {
                TRY(GameFiles::moveFile(FS::PathCombine(m_params.overwrittenDir, it->path), target));
            }
            return std::unexpected(placed.error());
        }
        manifest.files[it.key()].fileId = GameFiles::fileIndex(target);
        m_added++;
    }

    // drop folders deployment created that no deployed file needs any more, deepest first
    auto dirs = manifest.createdDirs;
    std::sort(dirs.begin(), dirs.end(), [](const QString& a, const QString& b) { return a.count('/') > b.count('/'); });
    for (const auto& dir : dirs) {
        const auto abs = FS::PathCombine(m_params.gameDir, dir);
        QDir d(abs);
        if (!d.exists()) {
            manifest.createdDirs.removeAll(dir);
        } else if (d.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System) && QDir().rmdir(abs)) {
            manifest.createdDirs.removeAll(dir);
        }
    }
    QMetaObject::invokeMethod(this, [this, total] { setProgress(total, total); }, Qt::QueuedConnection);
    return {};
}
