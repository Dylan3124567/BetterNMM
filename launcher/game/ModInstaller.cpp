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

#include "ModInstaller.h"

#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>

#include "FileSystem.h"
#include "game/GameFiles.h"

namespace ModInstaller {

namespace {

// folder names that hold loose files in Bethesda games' data folders
const QStringList g_dataFolders{ "meshes",    "textures", "scripts",   "interface", "sound",       "music",        "strings", "seq",
                                 "skse",      "f4se",     "nvse",      "obse",      "fose",        "sfse",         "shadersfx", "lodsettings",
                                 "grass",     "video",    "materials", "facegen",   "distantlod",  "trees",        "menus",   "dialogueviews",
                                 "calientetools", "nemesis_engine", "netscriptframework", "platform", "source", "dyndolod", "tools", "lightplacer",
                                 "fonts",     "geometries", "terrain", "particles", "mcm" };

bool hasEntryNamed(const QString& dir, const QString& name)
{
    return !name.isEmpty() && !resolvePath(dir, name).isEmpty();
}

QFileInfoList entries(const QString& dir)
{
    return QDir(dir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
}

/// Moves the contents of @p from into @p to, merging folders
Result<> moveContents(const QString& from, const QString& to, const QStringList& skip)
{
    for (const auto& entry : entries(from)) {
        if (skip.contains(entry.fileName(), Qt::CaseInsensitive)) {
            continue;
        }
        const auto target = FS::PathCombine(to, entry.fileName());
        if (entry.isDir()) {
            TRY(moveContents(entry.absoluteFilePath(), target, {}));
            continue;
        }
        if (QFileInfo::exists(target)) {
            TRY(GameFiles::removeFile(target));
        }
        TRY(GameFiles::moveFile(entry.absoluteFilePath(), target));
    }
    return {};
}

Result<> copyItem(const QString& source, const QString& target)
{
    if (QFileInfo::exists(target)) {
        TRY(GameFiles::removeFile(target));
    }
    if (!FS::ensureFilePathExists(target) || !QFile::copy(source, target)) {
        return std::unexpected(QObject::tr("Could not copy %1 to %2").arg(source, target));
    }
    return {};
}

}  // namespace

QString resolvePath(const QString& root, const QString& relPath)
{
    auto current = root;
    for (const auto& part : relPath.split('/', Qt::SkipEmptyParts)) {
        const auto direct = FS::PathCombine(current, part);
        if (QFileInfo::exists(direct)) {
            current = direct;
            continue;
        }
        QString match;
        for (const auto& entry : entries(current)) {
            if (entry.fileName().compare(part, Qt::CaseInsensitive) == 0) {
                match = entry.absoluteFilePath();
                break;
            }
        }
        if (match.isEmpty()) {
            return {};
        }
        current = match;
    }
    return current;
}

Analysis analyze(const QString& extractedDir, const GameDefinition& game, const QString& gameDir)
{
    Analysis analysis;
    const auto dataDir = game.dataPath.isEmpty() ? gameDir : FS::PathCombine(gameDir, game.dataPath);

    // an installer can sit below wrapper folders; look a few levels down
    QString root = extractedDir;
    for (int depth = 0; depth < 4 && !root.isEmpty(); depth++) {
        if (const auto fomod = resolvePath(root, "fomod"); !fomod.isEmpty() && QFileInfo(fomod).isDir()) {
            analysis.fomodRoot = root;
            if (const auto config = resolvePath(fomod, "ModuleConfig.xml"); !config.isEmpty()) {
                analysis.fomodConfig = config;
            }
            if (const auto info = resolvePath(fomod, "info.xml"); !info.isEmpty()) {
                QFile file(info);
                if (file.open(QIODevice::ReadOnly)) {
                    const auto parsed = Fomod::parseInfo(file.readAll());
                    analysis.name = parsed.name;
                    analysis.version = parsed.version;
                }
            }
            break;
        }
        const auto list = entries(root);
        root = (list.size() == 1 && list.first().isDir()) ? list.first().absoluteFilePath() : QString();
    }

    // a folder means something when the game has one of that name or it is a known data folder
    auto meaningful = [&](const QString& name) {
        if (!game.dataPath.isEmpty() && (name.compare(game.dataPath, Qt::CaseInsensitive) == 0 || g_dataFolders.contains(name, Qt::CaseInsensitive))) {
            return true;
        }
        return hasEntryNamed(gameDir, name) || hasEntryNamed(dataDir, name);
    };

    // a readme or screenshot beside the one wrapper folder of a data mod is not part of the mod's files
    auto document = [&game](const QFileInfo& e) {
        static const QStringList documents{ "txt", "md", "pdf", "rtf", "htm", "html", "url", "nfo", "doc", "docx",
                                            "jpg", "jpeg", "png", "gif", "webp", "bmp" };
        return !game.dataPath.isEmpty() && e.isFile() && documents.contains(e.suffix(), Qt::CaseInsensitive);
    };
    QString content = analysis.fomodRoot.isEmpty() ? extractedDir : analysis.fomodRoot;
    while (true) {
        auto list = entries(content);
        list.removeIf([&document](const QFileInfo& e) { return e.fileName().compare("fomod", Qt::CaseInsensitive) == 0 || document(e); });
        if (list.size() != 1 || !list.first().isDir() || meaningful(list.first().fileName())) {
            break;
        }
        content = list.first().absoluteFilePath();
    }
    analysis.contentRoot = content;

    if (game.dataPath.isEmpty()) {
        analysis.layout = Layout::GameRoot;
        return analysis;
    }
    bool rootFiles = false;
    bool hasLoader = false;
    for (const auto& entry : entries(content)) {
        const auto suffix = entry.suffix().toLower();
        if (entry.isDir() && entry.fileName().compare(game.dataPath, Qt::CaseInsensitive) == 0) {
            rootFiles = true;
        }
        if (entry.isFile() && (suffix == "exe" || suffix == "dll" || suffix == "asi")) {
            rootFiles = true;
            hasLoader |= entry.fileName().endsWith("_loader.exe", Qt::CaseInsensitive);
        }
        if (entry.isDir() && entry.fileName().compare("enbseries", Qt::CaseInsensitive) == 0) {
            rootFiles = true;
        }
    }
    if (rootFiles) {
        analysis.layout = Layout::GameRoot;
        if (hasLoader && hasEntryNamed(content, "src")) {
            analysis.skipped << QFileInfo(resolvePath(content, "src")).fileName();
        }
        return analysis;
    }
    analysis.layout = Layout::DataFolder;
    return analysis;
}

Result<> stage(const Analysis& analysis, const GameDefinition& game, const QString& stagingDir)
{
    auto skip = analysis.skipped;
    skip << "fomod";
    if (analysis.layout == Layout::DataFolder) {
        return moveContents(analysis.contentRoot, FS::PathCombine(stagingDir, game.dataPath), skip);
    }
    // keep the game's spelling of the data folder so deployed paths match the game's own folder
    for (const auto& entry : entries(analysis.contentRoot)) {
        if (entry.isDir() && !game.dataPath.isEmpty() && entry.fileName().compare(game.dataPath, Qt::CaseInsensitive) == 0 &&
            entry.fileName() != game.dataPath) {
            TRY(moveContents(entry.absoluteFilePath(), FS::PathCombine(stagingDir, game.dataPath), {}));
            skip << entry.fileName();
        }
    }
    return moveContents(analysis.contentRoot, stagingDir, skip);
}

Result<> stageFomod(const QList<Fomod::FileItem>& files, const Analysis& analysis, const GameDefinition& game, const QString& stagingDir)
{
    const auto installRoot = game.dataPath.isEmpty() ? stagingDir : FS::PathCombine(stagingDir, game.dataPath);
    if (!FS::ensureFolderPathExists(installRoot)) {
        return std::unexpected(QObject::tr("Could not create %1").arg(installRoot));
    }
    for (const auto& item : files) {
        const auto source = resolvePath(analysis.fomodRoot, item.source);
        if (source.isEmpty()) {
            return std::unexpected(QObject::tr("The installer refers to %1, which is not in the archive.").arg(item.source));
        }
        const auto destination = FS::PathCombine(installRoot, item.destination);
        // destinations must stay inside the mod; a hostile path is rejected outright
        if (!QDir::cleanPath(destination).startsWith(QDir::cleanPath(installRoot), Qt::CaseInsensitive)) {
            return std::unexpected(QObject::tr("The installer tried to write outside the mod: %1").arg(item.destination));
        }
        if (!item.isFolder) {
            TRY(copyItem(source, destination));
            continue;
        }
        const QDir sourceDir(source);
        for (const auto& entry : QDirListing(source, QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly |
                                                         QDirListing::IteratorFlag::IncludeHidden)) {
            TRY(copyItem(entry.absoluteFilePath(), FS::PathCombine(destination, sourceDir.relativeFilePath(entry.absoluteFilePath()))));
        }
    }
    return {};
}

}  // namespace ModInstaller
