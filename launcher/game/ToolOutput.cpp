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

#include "ToolOutput.h"

#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QTimeZone>

#include <algorithm>

#include "FileSystem.h"
#include "game/GameFiles.h"
#include "game/PatternMatcher.h"

namespace ToolOutput {

namespace {

constexpr auto k_listing = QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::IncludeHidden;

}  // namespace

Snapshot take(const QString& gameDir)
{
    Snapshot snapshot;
    const QDir root(gameDir);
    for (const auto& entry : QDirListing(gameDir, k_listing)) {
        const auto key = root.relativeFilePath(entry.absoluteFilePath()).toLower();
        if (entry.isDir()) {
            snapshot.dirs.insert(key);
        } else {
            snapshot.files.insert(key, Stamp{ .size = entry.size(), .modified = entry.lastModified(QTimeZone::UTC).toMSecsSinceEpoch() });
        }
    }
    return snapshot;
}

Changes find(const QString& gameDir, const Snapshot& before, const DeploymentManifest& manifest, const Filter& filter, const QString& outputModId)
{
    const PatternMatcher excluded(filter.excludePatterns);
    QStringList own;
    for (const auto& dir : filter.ownDirs) {
        own << QDir::cleanPath(dir).toLower() + '/';
    }
    Changes changes;
    const QDir root(gameDir);
    for (const auto& entry : QDirListing(gameDir, k_listing | QDirListing::IteratorFlag::FilesOnly)) {
        const auto rel = root.relativeFilePath(entry.absoluteFilePath());
        const auto key = rel.toLower();
        if (excluded.matches(rel) || std::any_of(own.begin(), own.end(), [&key](const QString& dir) { return key.startsWith(dir); })) {
            continue;
        }
        const auto deployed = manifest.files.constFind(key);
        const auto was = before.files.constFind(key);
        if (was == before.files.cend()) {
            // a mod's file at a path that was empty before: the tool deleted and wrote it, or deployment put it there late
            if (deployed == manifest.files.cend()) {
                changes.created << rel;
            } else if (deployed->fileId != 0 && GameFiles::fileIndex(entry.absoluteFilePath()) != deployed->fileId) {
                changes.replaced << rel;
            }
            continue;
        }
        if (was->size == entry.size() && was->modified == entry.lastModified(QTimeZone::UTC).toMSecsSinceEpoch()) {
            continue;
        }
        if (deployed == manifest.files.cend()) {
            changes.changedGameFiles << rel;
        } else if (deployed->fileId != 0 && GameFiles::fileIndex(entry.absoluteFilePath()) != deployed->fileId) {
            changes.replaced << rel;
        } else if (deployed->mod != outputModId) {
            // written through the deployed link: the mod's own file is this very file
            changes.changedInPlace << rel;
            changes.modOf.insert(rel, deployed->mod);
        }
    }
    return changes;
}

Result<> collect(const QString& gameDir, const Snapshot& before, const Changes& changes, const QString& modDir)
{
    const auto clear = [](const QString& target) -> Result<> {
        if (QFileInfo::exists(target)) {
            return GameFiles::removeFile(target);
        }
        return {};
    };
    QSet<QString> touchedDirs;
    for (const auto& rel : changes.created + changes.replaced) {
        const auto target = FS::PathCombine(modDir, rel);
        TRY(clear(target));
        TRY(GameFiles::moveFile(FS::PathCombine(gameDir, rel), target));
        touchedDirs.insert(QFileInfo(rel).path());
    }
    for (const auto& rel : changes.changedInPlace) {
        const auto target = FS::PathCombine(modDir, rel);
        TRY(clear(target));
        if (!FS::ensureFilePathExists(target) || !QFile::copy(FS::PathCombine(gameDir, rel), target)) {
            return std::unexpected(QObject::tr("Could not copy %1 into %2").arg(QDir::toNativeSeparators(rel), QDir::toNativeSeparators(modDir)));
        }
    }
    // the tool's new folders go with its files; deployment makes them again, and takes them away again with the mod
    for (auto dir : std::as_const(touchedDirs)) {
        while (!dir.isEmpty() && dir != "." && !before.dirs.contains(dir.toLower())) {
            if (!QDir(gameDir).rmdir(dir)) {
                break;  // something else is in it
            }
            dir = QFileInfo(dir).path();
        }
    }
    return {};
}

}  // namespace ToolOutput
