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

#include "GameFiles.h"

#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QTimeZone>
#include <QUuid>
#include <QtConcurrent>

#include <filesystem>
#include <vector>

#include "FileSystem.h"
#include "StringUtils.h"
#include "game/PatternMatcher.h"
#include "game/Steam.h"

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace GameFiles {

#ifdef Q_OS_WIN
namespace {
// CopyFileEx asks between chunks whether to go on, which is what lets a multi-gigabyte copy be cancelled
DWORD CALLBACK copyProgress(LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE, LPVOID cancel)
{
    return cancel && static_cast<const std::atomic_bool*>(cancel)->load() ? PROGRESS_CANCEL : PROGRESS_CONTINUE;
}

// the \\?\ form lifts the 260 character limit, which Windows otherwise only does when long paths are enabled system-wide
std::wstring winPath(const QString& path)
{
    const QString prefix = "\\\\?\\";
    if (path.startsWith(prefix)) {
        return path.toStdWString();
    }
    const auto native = QDir::toNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    return (native.startsWith("\\\\") ? prefix + "UNC\\" + native.mid(2) : prefix + native).toStdWString();
}
}  // namespace
#endif

Result<PlaceMode> placeFile(const QString& src, const QString& dst, PlaceMode wanted, const std::atomic_bool* cancel)
{
    if (!FS::ensureFilePathExists(dst)) {
        return std::unexpected(QString("Could not create the folder for %1").arg(dst));
    }
    std::error_code ec;
    if (wanted == PlaceMode::Link) {
#ifdef Q_OS_WIN
        if (CreateHardLinkW(winPath(dst).c_str(), winPath(src).c_str(), nullptr)) {
            return PlaceMode::Link;
        }
#else
        fs::create_hard_link(StringUtils::toStdString(src), StringUtils::toStdString(dst), ec);
        if (!ec) {
            return PlaceMode::Link;
        }
        ec.clear();
#endif
    }
    if (FS::canClone(src, dst) && FS::clone_file(src, dst, ec)) {
        return PlaceMode::Copy;
    }
#ifdef Q_OS_WIN
    if (!CopyFileExW(winPath(src).c_str(), winPath(dst).c_str(), copyProgress, const_cast<std::atomic_bool*>(cancel), nullptr,
                     COPY_FILE_FAIL_IF_EXISTS)) {
        const auto error = GetLastError();
        if (error == ERROR_REQUEST_ABORTED) {
            return std::unexpected(QObject::tr("Cancelled"));
        }
        return std::unexpected(QString("Could not copy %1 to %2: %3").arg(src, dst, qt_error_string(static_cast<int>(error))));
    }
#else
    QFile source(src);
    if (!source.copy(dst)) {
        return std::unexpected(QString("Could not copy %1 to %2: %3").arg(src, dst, source.errorString()));
    }
#endif
    return PlaceMode::Copy;
}

QString fileVersion(const QString& path)
{
#ifdef Q_OS_WIN
    const auto native = winPath(path);
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(native.c_str(), &ignored);
    if (size == 0) {
        return {};
    }
    std::vector<char> data(size);
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!GetFileVersionInfoW(native.c_str(), 0, size, data.data()) ||
        !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info || length < sizeof(VS_FIXEDFILEINFO)) {
        return {};
    }
    return QString("%1.%2.%3.%4")
        .arg(HIWORD(info->dwFileVersionMS))
        .arg(LOWORD(info->dwFileVersionMS))
        .arg(HIWORD(info->dwFileVersionLS))
        .arg(LOWORD(info->dwFileVersionLS));
#else
    Q_UNUSED(path)
    return {};
#endif
}

#ifdef Q_OS_WIN
/// A path as the user knows it: native separators, without the long-path prefix
static QString shownPath(const QString& path)
{
    auto out = QDir::toNativeSeparators(path);
    if (out.startsWith(R"(\\?\UNC\)")) {
        return R"(\\)" + out.mid(8);
    }
    return out.startsWith(R"(\\?\)") ? out.mid(4) : out;
}

static QString deleteError(const QString& path, DWORD error)
{
    if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) {
        return QObject::tr("Could not delete %1: a program has it open. Close that program (the game, a tool, an editor) and try again.")
            .arg(shownPath(path));
    }
    return QObject::tr("Could not delete %1: %2").arg(shownPath(path), qt_error_string(static_cast<int>(error)));
}
#endif

Result<> removeFile(const QString& path)
{
#ifdef Q_OS_WIN
    const auto native = winPath(path);
    HANDLE handle = CreateFileW(native.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return {};
        }
        return std::unexpected(deleteError(path, error));
    }
    FILE_DISPOSITION_INFO_EX info{};
    info.Flags = FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS | FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE;
    const bool deleted = SetFileInformationByHandle(handle, FileDispositionInfoEx, &info, sizeof(info)) != 0;
    CloseHandle(handle);
    if (deleted) {
        return {};
    }
    // File systems without POSIX deletes (FAT, older Windows) fall back to a plain delete
    if (DeleteFileW(native.c_str()) != 0) {
        return {};
    }
    return std::unexpected(deleteError(path, GetLastError()));
#else
    std::error_code ec;
    fs::remove(StringUtils::toStdString(path), ec);
    if (ec) {
        return std::unexpected(QString("Could not delete %1: %2").arg(path, QString::fromStdString(ec.message())));
    }
    return {};
#endif
}

Result<> removeTree(const QString& path)
{
#ifdef Q_OS_WIN
    const auto native = QString::fromStdWString(winPath(path));
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<const wchar_t*>(native.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return {};
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return removeFile(path);
    }
    if (!(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        WIN32_FIND_DATAW data;
        const auto pattern = native + "\\*";
        HANDLE find = FindFirstFileW(reinterpret_cast<const wchar_t*>(pattern.utf16()), &data);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                const auto name = QString::fromWCharArray(data.cFileName);
                if (name == "." || name == "..") {
                    continue;
                }
                if (auto removed = removeTree(native + "\\" + name); !removed) {
                    FindClose(find);
                    return removed;
                }
            } while (FindNextFileW(find, &data));
            FindClose(find);
        }
    }
    // a junction or symlinked folder is removed as a link; its target stays untouched
    if (RemoveDirectoryW(reinterpret_cast<const wchar_t*>(native.utf16())) == 0) {
        return std::unexpected(deleteError(path, GetLastError()));
    }
    return {};
#else
    std::error_code ec;
    fs::remove_all(StringUtils::toStdString(path), ec);
    if (ec) {
        return std::unexpected(QString("Could not delete %1: %2").arg(path, QString::fromStdString(ec.message())));
    }
    return {};
#endif
}

Result<> removeTreeWhole(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return {};
    }
    const auto parking = FS::PathCombine(info.absolutePath(), ".deleting");
    // what earlier deletes left: files a program had open through another hard link of theirs (the game running from
    // its own folder), which do not stop the move but do stop deleting
    for (const auto& left : QDir(parking).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
        if (auto removed = removeTree(FS::PathCombine(parking, left)); !removed) {
            qWarning() << "Left for a later delete:" << removed.error();
        }
    }
    if (!FS::ensureFolderPathExists(parking)) {
        return std::unexpected(QObject::tr("Could not create %1").arg(QDir::toNativeSeparators(parking)));
    }
    const auto parked = FS::PathCombine(parking, info.fileName() + "-" + QUuid::createUuid().toString(QUuid::Id128).left(8));
    std::error_code ec;
    fs::rename(StringUtils::toStdString(info.absoluteFilePath()), StringUtils::toStdString(parked), ec);
    if (ec) {
        QDir().rmdir(parking);
        return std::unexpected(QObject::tr("Could not delete %1 (%2): a program may have one of its files open. Close it and try again. "
                                           "Nothing was deleted.")
                                   .arg(QDir::toNativeSeparators(info.absoluteFilePath()), QString::fromStdString(ec.message())));
    }
    // gone from its place either way; what is still open goes the next time
    if (auto removed = removeTree(parked); !removed) {
        qWarning() << "Left for a later delete:" << removed.error();
    }
    QDir().rmdir(parking);
    return {};
}

Result<> moveFile(const QString& src, const QString& dst)
{
    if (!FS::ensureFilePathExists(dst)) {
        return std::unexpected(QString("Could not create the folder for %1").arg(dst));
    }
    std::error_code ec;
    fs::rename(StringUtils::toStdString(src), StringUtils::toStdString(dst), ec);
    if (!ec) {
        return {};
    }
    if (!QFile::copy(src, dst)) {
        return std::unexpected(QString("Could not move %1 to %2: %3").arg(src, dst, QString::fromStdString(ec.message())));
    }
    return removeFile(src);
}

#ifdef Q_OS_WIN
static bool fileId(const QString& path, BY_HANDLE_FILE_INFORMATION& out)
{
    const auto native = winPath(path);
    HANDLE handle = CreateFileW(native.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    const bool ok = GetFileInformationByHandle(handle, &out) != 0;
    CloseHandle(handle);
    return ok;
}
#endif

bool isSameFile(const QString& a, const QString& b)
{
#ifdef Q_OS_WIN
    BY_HANDLE_FILE_INFORMATION infoA{};
    BY_HANDLE_FILE_INFORMATION infoB{};
    if (!fileId(a, infoA) || !fileId(b, infoB)) {
        return false;
    }
    return infoA.dwVolumeSerialNumber == infoB.dwVolumeSerialNumber && infoA.nFileIndexHigh == infoB.nFileIndexHigh &&
           infoA.nFileIndexLow == infoB.nFileIndexLow;
#else
    std::error_code ec;
    return fs::equivalent(StringUtils::toStdString(a), StringUtils::toStdString(b), ec);
#endif
}

quint64 fileIndex(const QString& path)
{
#ifdef Q_OS_WIN
    BY_HANDLE_FILE_INFORMATION info{};
    if (!fileId(path, info)) {
        return 0;
    }
    return (static_cast<quint64>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
#else
    struct stat info {};
    if (::stat(QFile::encodeName(path).constData(), &info) != 0) {
        return 0;
    }
    return static_cast<quint64>(info.st_ino);
#endif
}

bool isSameVolume(const QString& a, const QString& b)
{
#ifdef Q_OS_WIN
    auto volumeOf = [](const QString& path) {
        const auto existing = QDir::toNativeSeparators(FS::nearestExistentAncestor(QFileInfo(path).absoluteFilePath())).toStdWString();
        wchar_t volume[MAX_PATH + 1] = {};
        if (GetVolumePathNameW(existing.c_str(), volume, MAX_PATH) == 0) {
            return QString();
        }
        return QString::fromWCharArray(volume);
    };
    const auto volumeA = volumeOf(a);
    return !volumeA.isEmpty() && volumeA.compare(volumeOf(b), Qt::CaseInsensitive) == 0;
#else
    return FS::statFS(a).rootPath == FS::statFS(b).rootPath;
#endif
}

bool supportsHardLinks(const QString& path)
{
#ifdef Q_OS_WIN
    const auto existing = QDir::toNativeSeparators(FS::nearestExistentAncestor(QFileInfo(path).absoluteFilePath())).toStdWString();
    wchar_t volume[MAX_PATH + 1] = {};
    DWORD flags = 0;
    if (GetVolumePathNameW(existing.c_str(), volume, MAX_PATH) == 0 ||
        GetVolumeInformationW(volume, nullptr, 0, nullptr, nullptr, &flags, nullptr, 0) == 0) {
        return true;  // unknown: placing tries a link first anyway
    }
    return (flags & FILE_SUPPORTS_HARD_LINKS) != 0;
#else
    Q_UNUSED(path)
    return true;
#endif
}

QStringList splitPatterns(const QString& patterns)
{
    QStringList out;
    for (const auto& part : patterns.split(';', Qt::SkipEmptyParts)) {
        if (auto trimmed = part.trimmed(); !trimmed.isEmpty()) {
            out << trimmed;
        }
    }
    return out;
}

bool matchesAny(const QString& relPath, const QStringList& patterns)
{
    return PatternMatcher(patterns).matches(relPath);
}

void pruneEmptyDirs(const QString& dir, const QString& stopAt)
{
    const auto stop = QDir::cleanPath(QFileInfo(stopAt).absoluteFilePath());
    auto current = QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
    while (current.length() > stop.length() && current.startsWith(stop, Qt::CaseInsensitive)) {
        QDir d(current);
        if (!d.exists() || !d.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
            return;
        }
        if (!QDir().rmdir(current)) {
            return;
        }
        current = QFileInfo(current).absolutePath();
    }
}

Result<BaseManifest> BaseManifest::load(const QString& path)
{
    BaseManifest manifest;
    QFile file(path);
    if (!file.exists()) {
        return manifest;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(QString("Could not read %1: %2").arg(path, file.errorString()));
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        return std::unexpected(QString("%1 is corrupt: %2").arg(path, error.errorString()));
    }
    const auto root = doc.object();
    manifest.source = root.value("source").toString();
    const auto files = root.value("files").toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        const auto obj = it.value().toObject();
        manifest.files.insert(it.key(), BaseEntry{ .size = static_cast<quint64>(obj.value("size").toInteger()),
                                                   .mtime = obj.value("mtime").toInteger(),
                                                   .linked = obj.value("linked").toBool() });
    }
    for (const auto& dir : root.value("dirs").toArray()) {
        manifest.dirs << dir.toString();
    }
    return manifest;
}

Result<> BaseManifest::save(const QString& path) const
{
    QJsonObject files;
    for (auto it = this->files.cbegin(); it != this->files.cend(); ++it) {
        files.insert(it.key(), QJsonObject{ { "size", static_cast<qint64>(it->size) }, { "mtime", it->mtime }, { "linked", it->linked } });
    }
    QJsonObject root{ { "source", source }, { "files", files }, { "dirs", QJsonArray::fromStringList(dirs) } };
    return FS::write(path, QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QStringList vortexManifests(const QString& gameDir, const QString& dataPath)
{
    // a mod type's folder is rarely deeper; listing only folders this shallow keeps it quick on the largest games
    QStringList roots{ gameDir };
    for (int depth = 0, from = 0; depth < 2; depth++) {
        const auto upTo = roots.size();
        for (; from < upTo; from++) {
            for (const auto& entry : QDirListing(roots.at(from), QDirListing::IteratorFlag::DirsOnly)) {
                roots << entry.absoluteFilePath();
            }
        }
    }
    if (!dataPath.isEmpty() && !roots.contains(FS::PathCombine(gameDir, dataPath), Qt::CaseInsensitive)) {
        roots << FS::PathCombine(gameDir, dataPath);
    }
    QStringList out;
    for (const auto& root : roots) {
        for (const auto& entry : QDirListing(root, { "vortex.deployment*.json" }, QDirListing::IteratorFlag::FilesOnly)) {
            out << entry.absoluteFilePath();
        }
    }
    return out;
}

QSet<QString> vortexDeployedFiles(const QString& gameDir, const QString& dataPath)
{
    QSet<QString> out;
    const QDir game(gameDir);
    for (const auto& path : vortexManifests(gameDir, dataPath)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        out.insert(game.relativeFilePath(path).toLower());
        const auto root = QFileInfo(path).absolutePath();
        const auto files = QJsonDocument::fromJson(file.readAll()).object().value("files").toArray();
        for (const auto& item : files) {
            const auto rel = item.toObject().value("relPath").toString().replace('\\', '/');
            if (!rel.isEmpty()) {
                out.insert(game.relativeFilePath(FS::PathCombine(root, rel)).toLower());
            }
        }
    }
    return out;
}

BaseSelection selectBaseFiles(const QString& sourceDir,
                              const QStringList& excludePatterns,
                              const QSet<QString>& knownModFiles,
                              const QString& steamRoot)
{
    BaseSelection selection;
    const PatternMatcher excluded(excludePatterns);
    auto keep = [&](const QString& rel) { return !excluded.matches(rel) && !knownModFiles.contains(rel.toLower()); };

    const auto root = steamRoot.isEmpty() ? Steam::steamRoot() : steamRoot;
    if (const auto app = Steam::findAppByInstallDir(root, sourceDir)) {
        const auto vanilla = Steam::vanillaFiles(root, *app);
        if (vanilla) {
            for (const auto& file : *vanilla) {
                if (file.isDirectory) {
                    selection.dirs << file.path;
                    continue;
                }
                if (!keep(file.path)) {
                    continue;
                }
                if (!QFileInfo::exists(FS::PathCombine(sourceDir, file.path))) {
                    selection.missing.insert(file.path);
                    continue;
                }
                selection.files << file.path;
            }
            selection.vanillaKnown = true;
            selection.note = QObject::tr("Using Steam's file list for %1 (app %2): %3 game files. Mods and other files added to the "
                                         "Steam folder are not copied into the instance.")
                                 .arg(app->name)
                                 .arg(app->appId)
                                 .arg(selection.files.size());
            if (!selection.missing.isEmpty()) {
                selection.note += QObject::tr(" %1 files Steam lists are missing from the Steam folder; verify the game files in Steam.")
                                      .arg(selection.missing.size());
            }
            return selection;
        }
        selection.note = QObject::tr("Steam's file list is unavailable (%1), so every file in the folder is used, except files "
                                     "matching the exclude patterns and files deployed by other mod managers.")
                             .arg(vanilla.error());
    } else {
        selection.note = QObject::tr("This folder is not a Steam installation, so every file in it is used, except files matching "
                                     "the exclude patterns and files deployed by other mod managers.");
    }

    const QDir source(sourceDir);
    for (const auto& entry : QDirListing(sourceDir, QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::IncludeHidden)) {
        const auto rel = source.relativeFilePath(entry.absoluteFilePath());
        if (entry.isDir()) {
            selection.dirs << rel;
        } else if (keep(rel)) {
            selection.files << rel;
        }
    }
    return selection;
}

BaseSyncTask::BaseSyncTask(Params params) : m_params(std::move(params)) {}

Result<int> unshareLocalFiles(const QString& manifestPath,
                              const QString& gameDir,
                              const QString& overwrittenDir,
                              const QSet<QString>& deployedFiles,
                              const QStringList& localPatterns)
{
    TRY_INTO(auto manifest, BaseManifest::load(manifestPath));
    const PatternMatcher local(localPatterns);
    int copied = 0;
    // the manifest is saved however far this gets, so a copy made before a failure is known as one
    const auto unshared = [&]() -> Result<> {
        for (auto it = manifest.files.begin(); it != manifest.files.end(); ++it) {
            if (!it->linked || !local.matches(it.key())) {
                continue;
            }
            const auto location = FS::PathCombine(deployedFiles.contains(it.key().toLower()) ? overwrittenDir : gameDir, it.key());
            if (!QFileInfo::exists(location)) {
                continue;
            }
            // beside it first: the link goes only once the copy is complete
            const auto copy = location + ".betternmm-copy";
            TRY(removeFile(copy));
            TRY(placeFile(location, copy, PlaceMode::Copy).transform([](auto) {}));
            TRY(removeFile(location));
            TRY(moveFile(copy, location));
            const QFileInfo placed(location);
            it->linked = false;
            it->size = static_cast<quint64>(placed.size());
            it->mtime = placed.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
            copied++;
        }
        return {};
    }();
    if (copied > 0) {
        TRY(manifest.save(manifestPath));
    }
    TRY(unshared);
    return copied;
}

void BaseSyncTask::executeTask()
{
    setStatus(tr("Linking game files..."));
    setAbortable(true);
    auto* watcher = new QFutureWatcher<Result<>>(this);
    connect(watcher, &QFutureWatcher<Result<>>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (m_cancel) {
            emitAborted();
        } else if (result) {
            emitSucceeded();
        } else {
            emitFailed(result.error());
        }
    });
    watcher->setFuture(QtConcurrent::run([this] { return perform(); }));
}

bool BaseSyncTask::abort()
{
    // the worker sees this between files and inside copies; the task ends once it has stopped
    m_cancel = true;
    return true;
}

Result<> BaseSyncTask::perform()
{
    // a folder that is not there (unplugged drive, moved or uninstalled game) would look like a game without files,
    // and the instance's links, maybe the last copy of the game, would be removed
    if (!QFileInfo(m_params.sourceDir).isDir()) {
        return std::unexpected(tr("The game folder %1 is not there; it may be on a drive that is not connected, or the game was "
                                  "moved or uninstalled. Nothing was changed.")
                                   .arg(QDir::toNativeSeparators(m_params.sourceDir)));
    }
    TRY_INTO(auto manifest, BaseManifest::load(m_params.manifestPath));
    const bool firstSync = manifest.files.isEmpty();
    manifest.source = m_params.sourceDir;

    const auto selection = selectBaseFiles(m_params.sourceDir, m_params.excludePatterns, m_params.knownModFiles, m_params.steamRoot);
    // what an uninstall leaves behind (logs, other managers' files) is no game, not an update that removed every file
    if (selection.files.isEmpty()) {
        return std::unexpected(tr("The game folder %1 has no game files; the game may have been uninstalled. Nothing was changed.")
                                   .arg(QDir::toNativeSeparators(m_params.sourceDir)));
    }
    m_messages << selection.note;
    if (!selection.vanillaKnown || !selection.missing.isEmpty()) {
        m_warnings << selection.note;
    }
    const PatternMatcher local(m_params.localPatterns);

    auto locationOf = [this](const QString& rel) {
        return m_params.deployedFiles.contains(rel.toLower()) ? FS::PathCombine(m_params.overwrittenDir, rel)
                                                              : FS::PathCombine(m_params.gameDir, rel);
    };
    auto sourceOf = [this](const QString& rel) {
        if (!m_params.alternateSourceDir.isEmpty()) {
            const auto alternate = FS::PathCombine(m_params.alternateSourceDir, rel);
            if (QFileInfo::exists(alternate)) {
                return alternate;
            }
        }
        return FS::PathCombine(m_params.sourceDir, rel);
    };
    auto record = [&](const QString& rel, const QString& location, PlaceMode mode) {
        const QFileInfo placed(location);
        manifest.files.insert(rel, BaseEntry{ .size = static_cast<quint64>(placed.size()),
                                              .mtime = placed.lastModified(QTimeZone::UTC).toMSecsSinceEpoch(),
                                              .linked = mode == PlaceMode::Link });
        if (mode == PlaceMode::Link) {
            m_linked++;
        } else {
            m_copied++;
            m_copiedBytes += static_cast<quint64>(placed.size());
        }
    };
    auto unchangedInInstance = [](const QFileInfo& existing, const BaseEntry& entry) {
        return static_cast<quint64>(existing.size()) == entry.size &&
               existing.lastModified(QTimeZone::UTC).toMSecsSinceEpoch() == entry.mtime;
    };

    // copying a whole game onto a full drive would leave it and the instance broken: say so before copying anything
    const bool linksWork = isSameVolume(m_params.sourceDir, m_params.gameDir) && supportsHardLinks(m_params.gameDir);
    qint64 toCopy = 0;
    for (const auto& rel : selection.files) {
        if (!linksWork || local.matches(rel)) {
            const QFileInfo source(sourceOf(rel));
            const QFileInfo existing(locationOf(rel));
            if (!existing.exists() || existing.size() != source.size()) {
                toCopy += source.size();
            }
        }
    }
    if (toCopy > 0) {
        const QStorageInfo drive(FS::nearestExistentAncestor(m_params.gameDir));
        if (drive.isValid() && drive.bytesAvailable() - toCopy < m_params.reserveBytes) {
            const QLocale locale;
            return std::unexpected(tr("The game files to copy need %1 on %2, which has %3 free. Free up space there or put the instance on "
                                      "another drive. Nothing was changed.")
                                       .arg(locale.formattedDataSize(toCopy), QDir::toNativeSeparators(drive.rootPath()),
                                            locale.formattedDataSize(drive.bytesAvailable())));
        }
    }

    const auto total = selection.files.size();
    // the manifest is saved whichever way this ends, so files placed before a failure or cancel are known next time
    const auto synced = [&]() -> Result<> {
        qsizetype done = 0;
        QSet<QString> wanted;
        for (const auto& rel : selection.files) {
            if (m_cancel) {
                return std::unexpected(tr("Cancelled"));
            }
            wanted.insert(rel);
            if (++done % 64 == 0) {
                QMetaObject::invokeMethod(this, [this, done, total] { setProgress(done, total); }, Qt::QueuedConnection);
            }
            const auto src = sourceOf(rel);
            const auto location = locationOf(rel);
            const auto mode = local.matches(rel) ? PlaceMode::Copy : PlaceMode::Link;
            const QFileInfo existing(location);
            const auto old = manifest.files.constFind(rel);

            if (!existing.exists()) {
                TRY_INTO(auto used, placeFile(src, location, mode, &m_cancel));
                record(rel, location, used);
                continue;
            }
            if (old == manifest.files.cend()) {
                m_messages << tr("Kept %1: the instance already has its own file there.").arg(rel);
                continue;
            }
            if (!unchangedInInstance(existing, *old)) {
                if (old->linked && isSameFile(location, src)) {
                    // the source changed in place, so the link already shows the new version
                    manifest.files[rel].size = static_cast<quint64>(existing.size());
                    manifest.files[rel].mtime = existing.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
                } else {
                    m_messages << tr("Kept %1: it was changed inside the instance.").arg(rel);
                    manifest.files.remove(rel);
                }
                continue;
            }
            const QFileInfo source(src);
            const bool upToDate = (old->linked && mode == PlaceMode::Link && isSameFile(location, src)) ||
                                  (!old->linked && mode == PlaceMode::Copy && existing.size() == source.size() &&
                                   existing.lastModified(QTimeZone::UTC) == source.lastModified(QTimeZone::UTC));
            if (upToDate) {
                continue;
            }
            // a cancelled replacement leaves the file missing, which the next sync places again
            TRY(removeFile(location));
            TRY_INTO(auto used, placeFile(src, location, mode, &m_cancel));
            record(rel, location, used);
        }

        for (const auto& rel : manifest.files.keys()) {
            if (m_cancel) {
                return std::unexpected(tr("Cancelled"));
            }
            // a file Steam lists but the folder lacks is still part of the game; the instance keeps its copy
            if (wanted.contains(rel) || selection.missing.contains(rel)) {
                continue;
            }
            const auto location = locationOf(rel);
            const QFileInfo existing(location);
            if (existing.exists()) {
                if (!unchangedInInstance(existing, manifest.files.value(rel))) {
                    m_messages << tr("Kept %1: it is gone from the game folder but was changed inside the instance.").arg(rel);
                    manifest.files.remove(rel);
                    continue;
                }
                TRY(removeFile(location));
                pruneEmptyDirs(existing.absolutePath(), m_params.gameDir);
            }
            manifest.files.remove(rel);
        }

        for (const auto& dir : selection.dirs) {
            FS::ensureFolderPathExists(FS::PathCombine(m_params.gameDir, dir));
        }
        manifest.dirs = selection.dirs;
        return {};
    }();

    auto saved = manifest.save(m_params.manifestPath);
    TRY(synced);
    QMetaObject::invokeMethod(this, [this, total] { setProgress(total, total); }, Qt::QueuedConnection);
    if (firstSync) {
        m_messages << tr("Linked %1 files and copied %2 files (%3 MiB).").arg(m_linked).arg(m_copied).arg(m_copiedBytes / (1024 * 1024));
    } else {
        m_messages << tr("Updated %1 linked and %2 copied files.").arg(m_linked).arg(m_copied);
    }
    return saved;
}

}  // namespace GameFiles
