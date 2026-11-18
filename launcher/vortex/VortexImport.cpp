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

#include "VortexImport.h"

#include <QDateTime>
#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QtConcurrent>

#include <optional>
#include <set>

#ifdef Q_OS_WIN
#include <shlobj.h>
#include <windows.h>
#endif

#include "FileSystem.h"
#include "game/GameFiles.h"
#include "game/GameInstance.h"
#include "game/GameInstanceCreationTask.h"
#include "game/Ini.h"
#include "game/ModList.h"
#include "game/PatternMatcher.h"
#include "game/Steam.h"
#include "settings/INISettingsObject.h"
#include "vortex/VortexState.h"

namespace VortexImport {

namespace {

using Entries = std::map<std::string, std::string>;

QJsonValue jsonValue(const std::string& raw)
{
    const auto doc = QJsonDocument::fromJson(QByteArray("[") + QByteArray::fromStdString(raw) + "]");
    return doc.array().isEmpty() ? QJsonValue() : doc.array().at(0);
}

int number(const QJsonValue& value)
{
    return value.isDouble() ? value.toInt() : value.toString().toInt();
}

/// Vortex's path templates: {USERDATA} is its user data folder, {GAME} the game id
QString resolve(QString pattern, const QString& userData, const QString& gameId)
{
    pattern.replace("{USERDATA}", userData, Qt::CaseInsensitive);
    pattern.replace("{GAME}", gameId, Qt::CaseInsensitive);
    return QDir::cleanPath(QDir::fromNativeSeparators(pattern));
}

QString quoteArgument(const QString& argument)
{
    return argument.contains(' ') && !argument.startsWith('"') ? '"' + argument + '"' : argument;
}

/// Vortex's default folder for a game's default mod type, where BetterNMM's data folder differs from it or is not
/// enough to know it; empty when Vortex's game extension decides it without a fixed answer
std::optional<QString> defaultModPath(const Game& game)
{
    static const QHash<QString, QString> paths{
        { "witcher3", "Mods" }, { "stardewvalley", "Mods" }, { "mountandblade2bannerlord", "Modules" }, { "cyberpunk2077", "" }
    };
    if (const auto it = paths.constFind(game.vortexId); it != paths.cend()) {
        return *it;
    }
    if (!game.definition.dataPath.isEmpty()) {
        return game.definition.dataPath;  // Bethesda games: Data, Data Files
    }
    return std::nullopt;
}

/// The mod type a deployment manifest is for: vortex.deployment.json is the default type, vortex.deployment.<type>.json
/// (or .msgpack in the staging folder) another
std::optional<QString> manifestType(const QString& fileName)
{
    static const QRegularExpression pattern(R"(^vortex\.deployment(?:\.(.+))?\.(json|msgpack)$)",
                                            QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(fileName);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return match.captured(1);
}

quint64 bigEndian(const uchar* p, int n)
{
    quint64 value = 0;
    for (int i = 0; i < n; i++) {
        value = (value << 8) | p[i];
    }
    return value;
}

bool msgpackString(const uchar*& p, const uchar* end, QByteArray* out)
{
    if (p >= end) {
        return false;
    }
    const uchar tag = *p++;
    quint64 length = 0;
    if ((tag & 0xe0) == 0xa0) {
        length = tag & 0x1f;
    } else if (tag >= 0xd9 && tag <= 0xdb) {
        const int bytes = 1 << (tag - 0xd9);
        if (end - p < bytes) {
            return false;
        }
        length = bigEndian(p, bytes);
        p += bytes;
    } else {
        return false;
    }
    if (static_cast<quint64>(end - p) < length) {
        return false;
    }
    if (out) {
        *out = QByteArray(reinterpret_cast<const char*>(p), static_cast<qsizetype>(length));
    }
    p += length;
    return true;
}

bool msgpackSkip(const uchar*& p, const uchar* end, int depth = 0)
{
    if (p >= end || depth > 64) {
        return false;
    }
    const uchar tag = *p;
    if (tag <= 0x7f || tag >= 0xe0 || tag == 0xc0 || tag == 0xc2 || tag == 0xc3) {
        p++;
        return true;
    }
    if ((tag & 0xe0) == 0xa0 || (tag >= 0xd9 && tag <= 0xdb)) {
        return msgpackString(p, end, nullptr);
    }
    p++;
    auto skip = [&](quint64 n) {
        if (static_cast<quint64>(end - p) < n) {
            return false;
        }
        p += n;
        return true;
    };
    auto sized = [&](int lengthBytes, int extra) {
        if (end - p < lengthBytes) {
            return false;
        }
        const auto length = bigEndian(p, lengthBytes);
        p += lengthBytes;
        return skip(length + static_cast<quint64>(extra));
    };
    quint64 items = 0;
    switch (tag) {
        case 0xcc:
        case 0xd0:
            return skip(1);
        case 0xcd:
        case 0xd1:
            return skip(2);
        case 0xca:
        case 0xce:
        case 0xd2:
            return skip(4);
        case 0xcb:
        case 0xcf:
        case 0xd3:
            return skip(8);
        case 0xc4:
            return sized(1, 0);
        case 0xc5:
            return sized(2, 0);
        case 0xc6:
            return sized(4, 0);
        case 0xc7:
            return sized(1, 1);
        case 0xc8:
            return sized(2, 1);
        case 0xc9:
            return sized(4, 1);
        case 0xd4:
            return skip(2);
        case 0xd5:
            return skip(3);
        case 0xd6:
            return skip(5);
        case 0xd7:
            return skip(9);
        case 0xd8:
            return skip(17);
        case 0xdc:
        case 0xdd:
        case 0xde:
        case 0xdf: {
            const int bytes = (tag == 0xdc || tag == 0xde) ? 2 : 4;
            if (end - p < bytes) {
                return false;
            }
            items = bigEndian(p, bytes) * (tag >= 0xde ? 2 : 1);
            p += bytes;
            break;
        }
        default:
            if ((tag & 0xf0) == 0x80) {
                items = (tag & 0x0f) * 2u;
            } else if ((tag & 0xf0) == 0x90) {
                items = tag & 0x0f;
            } else {
                return false;
            }
    }
    for (quint64 i = 0; i < items; i++) {
        if (!msgpackSkip(p, end, depth + 1)) {
            return false;
        }
    }
    return true;
}

/// A string field of the top-level map of a MessagePack document
QString msgpackField(const QByteArray& data, const QByteArray& key)
{
    const auto* p = reinterpret_cast<const uchar*>(data.constData());
    const auto* end = p + data.size();
    if (p >= end) {
        return {};
    }
    quint64 pairs = 0;
    const uchar tag = *p++;
    if ((tag & 0xf0) == 0x80) {
        pairs = tag & 0x0f;
    } else if ((tag == 0xde || tag == 0xdf) && end - p >= (tag == 0xde ? 2 : 4)) {
        const int bytes = tag == 0xde ? 2 : 4;
        pairs = bigEndian(p, bytes);
        p += bytes;
    } else {
        return {};
    }
    for (quint64 i = 0; i < pairs; i++) {
        QByteArray name;
        if (!msgpackString(p, end, &name)) {
            return {};
        }
        QByteArray value;
        if (name == key && msgpackString(p, end, &value)) {
            return QString::fromUtf8(value);
        }
        if (!msgpackSkip(p, end)) {
            return {};
        }
    }
    return {};
}

/// A rule's reference as Vortex matches it: by mod id, else archive checksum, else logical file name, else file name
/// pattern. Built once per rule: a pattern compiled for every mod would make big setups slow.
class Reference {
   public:
    explicit Reference(const QJsonObject& reference)
        : m_id(reference.value("id").toString())
        , m_md5(reference.value("fileMD5").toString())
        , m_logical(reference.value("logicalFileName").toString())
    {
        if (const auto expression = reference.value("fileExpression").toString(); !expression.isEmpty()) {
            m_expression = QRegularExpression(
                QRegularExpression::wildcardToRegularExpression(expression, QRegularExpression::NonPathWildcardConversion),
                QRegularExpression::CaseInsensitiveOption);
        }
    }

    bool matches(const Mod& mod) const
    {
        if (!m_id.isEmpty()) {
            return m_id == mod.id;
        }
        if (!m_md5.isEmpty()) {
            return m_md5.compare(mod.md5, Qt::CaseInsensitive) == 0;
        }
        if (!m_logical.isEmpty()) {
            return m_logical.compare(mod.logicalFileName, Qt::CaseInsensitive) == 0;
        }
        if (!m_expression.pattern().isEmpty()) {
            return m_expression.match(QFileInfo(mod.archive).completeBaseName()).hasMatch() || m_expression.match(mod.id).hasMatch();
        }
        return false;
    }

   private:
    QString m_id;
    QString m_md5;
    QString m_logical;
    QRegularExpression m_expression;
};

}  // namespace

std::optional<QString> insideGamePath(const QString& gameDir, const QString& path)
{
    if (gameDir.isEmpty() || path.isEmpty()) {
        return std::nullopt;
    }
    const auto rel = QDir::cleanPath(QDir(gameDir).relativeFilePath(QDir::fromNativeSeparators(path)));
    if (rel == ".") {
        return QString();
    }
    if (rel == ".." || rel.startsWith("../") || QDir::isAbsolutePath(rel)) {
        return std::nullopt;
    }
    return rel;
}

const Game* State::game(const QString& vortexId) const
{
    for (const auto& game : games) {
        if (game.vortexId == vortexId) {
            return &game;
        }
    }
    return nullptr;
}

const Profile* State::profile(const QString& id) const
{
    for (const auto& profile : profiles) {
        if (profile.id == id) {
            return &profile;
        }
    }
    return nullptr;
}

Result<State> load(const QString& databaseDir)
{
    const auto dir =
        databaseDir.isEmpty() ? QDir::fromNativeSeparators(QString::fromStdWString(VortexState::defaultDatabase())) : databaseDir;
    if (dir.isEmpty() || !QFileInfo::exists(FS::PathCombine(dir, "CURRENT"))) {
        return std::unexpected(QObject::tr("Vortex is not set up on this PC (there is no %1).").arg(QDir::toNativeSeparators(dir)));
    }
    std::map<std::string, std::string> entries;
    std::string error;
    if (!VortexState::readDatabase(QDir::toNativeSeparators(dir).toStdWString(), entries, error)) {
        return std::unexpected(QString::fromStdString(error));
    }
    return fromEntries(entries, QDir::cleanPath(QFileInfo(dir).absolutePath()));
}

State fromEntries(const Entries& entries, const QString& userData)
{
    State state;
    state.userData = userData;
    QMap<QString, Game> games;
    auto gameFor = [&](const QString& id) -> Game* {
        if (auto it = games.find(id); it != games.end()) {
            return &*it;
        }
        const auto* known = VortexState::gameByVortexId(id.toStdString());
        const auto definition = known ? GameRegistry::byDomain(QString::fromLatin1(known->domain)) : GameDefinition();
        if (!definition.isValid()) {
            return nullptr;
        }
        Game game;
        game.vortexId = id;
        game.definition = definition;
        game.stagingDir = resolve("{USERDATA}/{GAME}/mods", userData, id);
        return &*games.insert(id, game);
    };

    QHash<QString, QHash<QString, Mod>> mods;  // game -> mod id -> mod
    QHash<QString, QJsonObject> attributes;    // game + '/' + mod id
    QSet<QString> installed;                   // game + '/' + mod id
    QHash<QString, Profile> profiles;
    struct ToolEntry {
        QJsonObject fields;
    };
    QHash<QString, QMap<QString, QJsonObject>> tools;  // game -> tool id -> fields
    struct Download {
        QStringList games;
        bool finished = false;
        QString file;
    };
    QHash<QString, Download> downloads;
    QString downloadsRoot = resolve("{USERDATA}/downloads", userData, {});

    for (const auto& [rawKey, raw] : entries) {
        const auto split = VortexState::splitKey(rawKey);
        QStringList parts;
        parts.reserve(static_cast<qsizetype>(split.size()));
        for (const auto& part : split) {
            parts << QString::fromStdString(part);
        }
        if (parts.size() < 3) {
            continue;
        }
        if (parts[0] == "persistent" && parts[1] == "mods" && parts.size() >= 5) {
            if (!gameFor(parts[2])) {
                continue;
            }
            auto& mod = mods[parts[2]][parts[3]];
            mod.id = parts[3];
            const auto& field = parts[4];
            const auto value = jsonValue(raw);
            if (field == "installationPath" && parts.size() == 5) {
                mod.folder = value.toString();
            } else if (field == "type" && parts.size() == 5) {
                mod.type = value.toString();
            } else if (field == "state" && parts.size() == 5 && value.toString() == "installed") {
                installed.insert(parts[2] + '/' + parts[3]);
            } else if (field == "rules" && parts.size() == 5) {
                mod.rules = value.toArray();
            } else if (field == "attributes" && parts.size() == 6) {
                attributes[parts[2] + '/' + parts[3]].insert(parts[5], value);
            }
        } else if (parts[0] == "persistent" && parts[1] == "profiles" && parts.size() >= 4) {
            auto& profile = profiles[parts[2]];
            profile.id = parts[2];
            const auto value = jsonValue(raw);
            if (parts.size() == 4 && parts[3] == "name") {
                profile.name = value.toString();
            } else if (parts.size() == 4 && parts[3] == "gameId") {
                profile.gameId = value.toString();
            } else if (parts.size() == 4 && parts[3] == "lastActivated") {
                profile.lastActivated = value.toInteger();
            } else if (parts.size() == 6 && parts[3] == "modState" && parts[5] == "enabled" && value.toBool()) {
                profile.enabledMods.insert(parts[4]);
            } else if (parts.size() == 5 && parts[3] == "features" && parts[4] == "local_saves") {
                profile.localSaves = value.toBool();
            } else if (parts.size() == 5 && parts[3] == "features" && parts[4] == "local_game_settings") {
                profile.localSettings = value.toBool();
            }
        } else if (parts[0] == "persistent" && parts[1] == "downloads" && parts[2] == "files" && parts.size() == 5) {
            auto& download = downloads[parts[3]];
            const auto value = jsonValue(raw);
            if (parts[4] == "game") {
                download.games.clear();
                for (const auto& game : value.toArray()) {
                    download.games << game.toString();
                }
            } else if (parts[4] == "state") {
                download.finished = value.toString() == "finished";
            } else if (parts[4] == "localPath") {
                download.file = value.toString();
            }
        } else if (parts[0] == "settings" && parts[1] == "gameMode" && parts[2] == "discovered" && parts.size() >= 5) {
            auto* game = gameFor(parts[3]);
            if (!game) {
                continue;
            }
            if (parts.size() == 5 && parts[4] == "path") {
                game->gameDir = QDir::cleanPath(QDir::fromNativeSeparators(jsonValue(raw).toString()));
            } else if (parts.size() == 7 && parts[4] == "tools") {
                tools[parts[3]][parts[5]].insert(parts[6], jsonValue(raw));
            }
        } else if (parts[0] == "settings" && parts[1] == "mods" && parts[2] == "installPath" && parts.size() == 4) {
            if (auto* game = gameFor(parts[3])) {
                game->stagingDir = resolve(jsonValue(raw).toString(), userData, parts[3]);
            }
        } else if (parts[0] == "settings" && parts[1] == "downloads" && parts[2] == "path" && parts.size() == 3) {
            downloadsRoot = resolve(jsonValue(raw).toString(), userData, {});
        } else if (parts[0] == "settings" && parts[1] == "profiles" && parts[2] == "lastActiveProfile" && parts.size() == 4) {
            if (auto* game = gameFor(parts[3])) {
                game->deployedProfile = jsonValue(raw).toString();
            }
        }
    }

    for (auto& game : games) {
        const auto gameMods = mods.value(game.vortexId);
        for (auto mod : gameMods) {
            const auto key = game.vortexId + '/' + mod.id;
            if (!installed.contains(key) || mod.folder.isEmpty()) {
                continue;  // still installing, or a broken entry Vortex shows as missing
            }
            const auto attrs = attributes.value(key);
            for (const auto* field : { "customFileName", "logicalFileName", "name" }) {
                if (mod.name.isEmpty()) {
                    mod.name = attrs.value(field).toString().trimmed();
                }
            }
            if (mod.name.isEmpty()) {
                mod.name = mod.id;
            }
            mod.version = attrs.value("version").toString();
            const auto downloadGame = attrs.value("downloadGame").toString();
            const auto* known = VortexState::gameByVortexId(downloadGame.toStdString());
            mod.nexusGame = known ? QString::fromLatin1(known->domain) : downloadGame.isEmpty() ? game.definition.domain : downloadGame;
            mod.nexusModId = number(attrs.value("modId"));
            mod.nexusFileId = number(attrs.value("fileId"));
            mod.archive = attrs.value("fileName").toString();
            mod.md5 = attrs.value("fileMD5").toString();
            mod.logicalFileName = attrs.value("logicalFileName").toString();
            mod.installTime = QDateTime::fromString(attrs.value("installTime").toString(), Qt::ISODateWithMs).toMSecsSinceEpoch();
            game.mods << mod;
        }
        std::sort(game.mods.begin(), game.mods.end(), [](const Mod& a, const Mod& b) { return a.id < b.id; });

        const auto gameTools = tools.value(game.vortexId);
        for (auto it = gameTools.cbegin(); it != gameTools.cend(); ++it) {
            const auto& fields = *it;
            const auto path = QDir::fromNativeSeparators(fields.value("path").toString());
            if (path.isEmpty() || fields.value("hidden").toBool()) {
                continue;  // never found on this PC, or the user hid it
            }
            QStringList arguments;
            for (const auto& argument : fields.value("parameters").toArray()) {
                arguments << quoteArgument(argument.toString());
            }
            game.tools << Tool{ fields.value("name").toString(it.key()), path, arguments.join(' '),
                                QDir::fromNativeSeparators(fields.value("workingDirectory").toString()) };
        }

        for (const auto& download : std::as_const(downloads)) {
            if (download.finished && !download.file.isEmpty() && download.games.contains(game.vortexId)) {
                // a download for several games lives in the folder of the first
                game.downloads << FS::PathCombine(downloadsRoot, download.games.first(), download.file);
            }
        }
        game.downloads.sort();
        state.games << game;
    }

    for (auto profile : std::as_const(profiles)) {
        if (!games.contains(profile.gameId)) {
            continue;
        }
        if (profile.name.isEmpty()) {
            profile.name = profile.id;
        }
        profile.folder = FS::PathCombine(userData, profile.gameId, "profiles", profile.id);
        state.profiles << profile;
    }
    std::stable_sort(state.profiles.begin(), state.profiles.end(), [](const Profile& a, const Profile& b) {
        return a.lastActivated != b.lastActivated ? a.lastActivated > b.lastActivated : a.name < b.name;
    });
    return state;
}

QHash<QString, QString> typeTargets(const Game& game)
{
    QHash<QString, QString> targets;
    // the copies of the manifests Vortex keeps in its staging folder survive a purge of the game folder
    if (QFileInfo(game.stagingDir).isDir()) {
        for (const auto& entry : QDirListing(game.stagingDir, { "vortex.deployment*" }, QDirListing::IteratorFlag::FilesOnly)) {
            const auto type = manifestType(entry.fileName());
            QFile file(entry.absoluteFilePath());
            if (!type || !file.open(QIODevice::ReadOnly)) {
                continue;
            }
            const auto data = file.readAll();
            const auto target = entry.fileName().endsWith(".msgpack", Qt::CaseInsensitive)
                                    ? msgpackField(data, "targetPath")
                                    : QJsonDocument::fromJson(data).object().value("targetPath").toString();
            if (const auto rel = insideGamePath(game.gameDir, target)) {
                targets.insert(*type, *rel);
            }
        }
    }
    // a manifest in the game folder sits where its files went, with the folder's real name
    if (!game.gameDir.isEmpty() && QFileInfo(game.gameDir).isDir()) {
        for (const auto& path : GameFiles::vortexManifests(game.gameDir, game.definition.dataPath)) {
            const auto type = manifestType(QFileInfo(path).fileName());
            if (const auto rel = insideGamePath(game.gameDir, QFileInfo(path).absolutePath()); type && rel) {
                targets.insert(*type, *rel);
            }
        }
    }
    if (!targets.contains(QString())) {
        if (const auto fallback = defaultModPath(game)) {
            targets.insert(QString(), *fallback);
        }
    }
    return targets;
}

QHash<QString, QString> deployedFiles(const Game& game)
{
    QHash<QString, QString> out;
    if (game.gameDir.isEmpty() || !QFileInfo(game.gameDir).isDir()) {
        return out;
    }
    for (const auto& path : GameFiles::vortexManifests(game.gameDir, game.definition.dataPath)) {
        const auto rel = insideGamePath(game.gameDir, QFileInfo(path).absolutePath());
        QFile file(path);
        if (!rel || !file.open(QIODevice::ReadOnly)) {
            continue;
        }
        for (const auto& item : QJsonDocument::fromJson(file.readAll()).object().value("files").toArray()) {
            const auto obj = item.toObject();
            const auto relPath = QDir::fromNativeSeparators(obj.value("relPath").toString());
            const auto source = obj.value("source").toString();
            if (!relPath.isEmpty() && !source.isEmpty()) {
                out.insert(QDir::cleanPath(rel->isEmpty() ? relPath : *rel + '/' + relPath).toLower(), source);
            }
        }
    }
    return out;
}

QStringList order(const QList<Mod>& mods,
                  const QHash<QString, QSet<QString>>& files,
                  const QHash<QString, QString>& winners,
                  const QSet<QString>& enabled,
                  QStringList* warnings)
{
    const auto count = mods.size();
    QHash<QString, qsizetype> byFolder;
    for (qsizetype i = 0; i < count; i++) {
        byFolder.insert(mods[i].folder.toLower(), i);
    }
    // later[i] holds the mods that go after mod i
    QList<QSet<qsizetype>> later(count);
    auto reaches = [&](qsizetype from, qsizetype to) {
        QList<qsizetype> stack{ from };
        QSet<qsizetype> seen{ from };
        while (!stack.isEmpty()) {
            const auto at = stack.takeLast();
            if (at == to) {
                return true;
            }
            for (const auto next : later[at]) {
                if (!seen.contains(next)) {
                    seen.insert(next);
                    stack << next;
                }
            }
        }
        return false;
    };
    // an edge that would close a loop is left out, so the order always exists
    auto addEdge = [&](qsizetype first, qsizetype second) {
        if (first == second || later[first].contains(second)) {
            return true;
        }
        if (reaches(second, first)) {
            return false;
        }
        later[first].insert(second);
        return true;
    };

    for (qsizetype i = 0; i < count; i++) {
        for (const auto& value : mods[i].rules) {
            const auto rule = value.toObject();
            const auto type = rule.value("type").toString();
            if (type != "before" && type != "after") {
                continue;
            }
            const Reference reference(rule.value("reference").toObject());
            for (qsizetype j = 0; j < count; j++) {
                if (j == i || !reference.matches(mods[j])) {
                    continue;
                }
                const bool added = type == "before" ? addEdge(i, j) : addEdge(j, i);
                if (!added && warnings) {
                    *warnings << QObject::tr("Vortex's rule that \"%1\" loads %2 \"%3\" contradicts its other rules and was left out.")
                                     .arg(mods[i].name, type == "before" ? QObject::tr("before") : QObject::tr("after"), mods[j].name);
                }
            }
        }
    }

    if (!winners.isEmpty()) {
        QHash<QString, QList<qsizetype>> holders;
        for (qsizetype i = 0; i < count; i++) {
            if (enabled.contains(mods[i].id)) {
                for (const auto& path : files.value(mods[i].id)) {
                    holders[path] << i;
                }
            }
        }
        std::set<std::pair<qsizetype, qsizetype>> pairs;
        for (auto it = holders.cbegin(); it != holders.cend(); ++it) {
            if (it->size() < 2) {
                continue;
            }
            const auto winner = byFolder.value(winners.value(it.key()).toLower(), -1);
            if (winner < 0 || !it->contains(winner)) {
                continue;
            }
            for (const auto loser : *it) {
                if (loser != winner) {
                    pairs.emplace(loser, winner);
                }
            }
        }
        for (const auto& [loser, winner] : pairs) {
            addEdge(loser, winner);  // the rules decide where they disagree with a deployment made before a rule changed
        }
    }

    QList<int> incoming(count, 0);
    for (const auto& next : later) {
        for (const auto j : next) {
            incoming[j]++;
        }
    }
    std::set<std::tuple<QString, QString, qsizetype>> ready;
    for (qsizetype i = 0; i < count; i++) {
        if (incoming[i] == 0) {
            ready.emplace(mods[i].name.toLower(), mods[i].id, i);
        }
    }
    QStringList out;
    while (!ready.empty()) {
        const auto [name, id, i] = *ready.begin();
        ready.erase(ready.begin());
        out << id;
        for (const auto j : later[i]) {
            if (--incoming[j] == 0) {
                ready.emplace(mods[j].name.toLower(), mods[j].id, j);
            }
        }
    }
    return out;
}

QList<SaveFolder> saveFolders(const GameDefinition& game, const Profile& profile, const QString& realGameDir, const QString& instanceRoot)
{
    QList<SaveFolder> out;
    const auto profileRoot = FS::PathCombine(instanceRoot, "profile");
    const auto documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!game.myGamesFolder.isEmpty()) {
        const auto real = FS::PathCombine(documents, "My Games", game.myGamesFolder, "Saves");
        const auto mine =
            FS::PathCombine(FS::PathCombine(profileRoot, GameInstance::documentsSubfolder()), "My Games", game.myGamesFolder, "Saves");
        // Vortex keeps a profile's own saves in Saves/<profile id> and points the game's INI there
        out << (profile.localSaves ? SaveFolder{ FS::PathCombine(real, profile.id), FS::PathCombine(mine, profile.id) }
                                   : SaveFolder{ real, mine });
    }
    const auto shared = GameInstance::sharedProfileFolders();
    for (const auto& folder : game.saveFolders) {
        const auto token = folder.section('/', 0, 0).toLower();
        const auto rest = folder.section('/', 1);
        QString real;
        QString mine;
        if (token == "{documents}") {
            real = documents;
            mine = FS::PathCombine(profileRoot, GameInstance::documentsSubfolder());
        } else if (token == "{appdata}") {
            real = QDir::fromNativeSeparators(qEnvironmentVariable("APPDATA"));
            mine = FS::PathCombine(profileRoot, GameInstance::roamingAppDataSubfolder());
        } else if (token == "{localappdata}") {
            real = QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA"));
            mine = FS::PathCombine(profileRoot, GameInstance::localAppDataSubfolder());
        } else if (token == "{savedgames}") {
            if (shared.contains("Saved Games")) {
                continue;  // the instance sees the real folder, so the saves are there already
            }
#ifdef Q_OS_WIN
            PWSTR path = nullptr;
            if (SHGetKnownFolderPath(FOLDERID_SavedGames, KF_FLAG_DONT_VERIFY, nullptr, &path) == S_OK) {
                real = QDir::fromNativeSeparators(QString::fromWCharArray(path));
            }
            CoTaskMemFree(path);
#endif
            mine = FS::PathCombine(profileRoot, GameInstance::savedGamesSubfolder());
        } else if (token == "{game}") {
            real = realGameDir;
            mine = FS::PathCombine(instanceRoot, "game");
        }
        if (!real.isEmpty() && !mine.isEmpty()) {
            out << SaveFolder{ FS::PathCombine(real, rest), FS::PathCombine(mine, rest) };
        }
    }
    return out;
}

Result<> pointAtProfileSaves(const QString& iniPath, const QString& profileId)
{
    QByteArray data;
    if (QFile file(iniPath); file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            return std::unexpected(QObject::tr("Could not read %1: %2").arg(QDir::toNativeSeparators(iniPath), file.errorString()));
        }
        data = file.readAll();
    }
    auto document = Ini::Document::fromBytes(data);
    document.setValue("General", "SLocalSavePath", "Saves\\" + profileId + "\\");
    // written as a new file, so an INI that is a hard link never changes anywhere else
    return FS::write(iniPath, document.toBytes());
}

}  // namespace VortexImport

namespace {

struct ModWork {
    VortexImport::Mod mod;
    QString source;     ///< its folder in Vortex's staging folder
    QString prefix;     ///< where its files go, relative to the game folder
    QStringList files;  ///< relative to source
    qint64 bytes = 0;
};

struct Work {
    QList<ModWork> mods;
    QStringList order;  ///< Vortex mod ids, priority order
    QSet<QString> enabled;
    QString modsDir;
    QString listFile;
    QStringList localPatterns;
    QString pluginListDir;
    QString profileFolder;  ///< Vortex's folder of the profile
    QString myGamesDir;
    QStringList iniFiles;  ///< to bring from the profile folder, empty when the profile has none of its own
    QList<VortexImport::SaveFolder> saves;
    QStringList skipSaveFolders;  ///< other profiles' save folders inside a shared Saves folder
    QString profileSavesIni;      ///< the INI to point at the profile's own saves, when it has them
    QString profileId;
    QStringList downloads;
    QString downloadsDir;
};

struct Outcome {
    int mods = 0;
    int enabled = 0;
    int saveFiles = 0;
    int downloads = 0;
    bool plugins = false;
    int inis = 0;
};

qint64 treeSize(const QString& dir, const QStringList& skipTop)
{
    qint64 total = 0;
    if (!QFileInfo(dir).isDir()) {
        return 0;
    }
    const QDir root(dir);
    for (const auto& entry : QDirListing(
             dir, QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly | QDirListing::IteratorFlag::IncludeHidden)) {
        if (!skipTop.contains(root.relativeFilePath(entry.absoluteFilePath()).section('/', 0, 0), Qt::CaseInsensitive)) {
            total += entry.size();
        }
    }
    return total;
}

Result<Outcome> perform(const Work& work, const std::atomic_bool& cancel, const std::function<void(qint64, qint64, QString)>& progress)
{
    Outcome outcome;
    qint64 total = 0;
    for (const auto& mod : work.mods) {
        total += mod.bytes;
    }
    qint64 done = 0;
    auto canceled = [&]() -> Result<> {
        if (cancel.load()) {
            return std::unexpected(QObject::tr("The import was canceled."));
        }
        return {};
    };

    ModList list(work.modsDir, work.listFile);
    if (auto loaded = list.load(); !loaded) {
        return std::unexpected(loaded.error());
    }
    const PatternMatcher local(work.localPatterns);
    QHash<QString, QString> newIds;  // Vortex mod id -> BetterNMM mod id
    for (const auto& mod : work.mods) {
        progress(done, total, QObject::tr("Bringing over %1...").arg(mod.mod.name));
        const auto id = list.uniqueId(mod.mod.name);
        const auto target = list.modDir(id);
        for (const auto& rel : mod.files) {
            TRY(canceled());
            const auto gameRel = mod.prefix.isEmpty() ? rel : mod.prefix + '/' + rel;
            const auto from = FS::PathCombine(mod.source, rel);
            // configs and programs a game or tool may rewrite in place stay apart from Vortex's copies
            const auto mode = local.matches(gameRel) ? GameFiles::PlaceMode::Copy : GameFiles::PlaceMode::Link;
            if (auto placed = GameFiles::placeFile(from, FS::PathCombine(target, gameRel), mode, &cancel); !placed) {
                return std::unexpected(
                    QObject::tr("Could not bring over %1 of %2: %3").arg(QDir::toNativeSeparators(from), mod.mod.name, placed.error()));
            }
            done += QFileInfo(from).size();
            progress(done, total, {});
        }
        FS::ensureFolderPathExists(target);  // a mod without files still shows up
        ModEntry entry;
        entry.id = id;
        entry.name = mod.mod.name;
        entry.enabled = work.enabled.contains(mod.mod.id);
        entry.version = mod.mod.version;
        if (mod.mod.nexusModId > 0) {
            entry.nexusGame = mod.mod.nexusGame;
            entry.nexusModId = mod.mod.nexusModId;
            entry.nexusFileId = mod.mod.nexusFileId;
        }
        entry.archive = mod.mod.archive;
        entry.stamp = mod.mod.installTime > 0 ? mod.mod.installTime : QDateTime::currentMSecsSinceEpoch();
        list.upsert(entry);
        newIds.insert(mod.mod.id, id);
        outcome.mods++;
        outcome.enabled += entry.enabled ? 1 : 0;
    }
    QStringList ordered;
    for (const auto& vortexId : work.order) {
        if (newIds.contains(vortexId)) {
            ordered << newIds.value(vortexId);
        }
    }
    list.setOrder(ordered);
    TRY(list.save());

    progress(total, total, QObject::tr("Bringing over the load order and settings..."));
    if (!work.pluginListDir.isEmpty()) {
        for (const auto* name : { "plugins.txt", "loadorder.txt" }) {
            const auto from = FS::PathCombine(work.profileFolder, name);
            if (!QFileInfo::exists(from)) {
                continue;
            }
            const auto to = FS::PathCombine(work.pluginListDir, name);
            QFile::remove(to);
            if (auto copied = GameFiles::placeFile(from, to, GameFiles::PlaceMode::Copy); !copied) {
                return std::unexpected(QObject::tr("Could not bring over the profile's %1: %2").arg(name, copied.error()));
            }
            outcome.plugins = true;
        }
    }
    if (!work.iniFiles.isEmpty() && QFileInfo(work.profileFolder).isDir()) {
        // Vortex keeps a profile's own INI files in its folder, not always with the game's capitalization
        const QDir profileDir(work.profileFolder);
        for (const auto& ini : work.iniFiles) {
            for (const auto& present : profileDir.entryList({ ini }, QDir::Files)) {
                const auto to = FS::PathCombine(work.myGamesDir, ini);
                QFile::remove(to);
                if (auto copied = GameFiles::placeFile(profileDir.filePath(present), to, GameFiles::PlaceMode::Copy); !copied) {
                    return std::unexpected(QObject::tr("Could not bring over the profile's %1: %2").arg(ini, copied.error()));
                }
                outcome.inis++;
            }
        }
    }

    if (!work.profileSavesIni.isEmpty()) {
        TRY(VortexImport::pointAtProfileSaves(work.profileSavesIni, work.profileId));
    }
    for (const auto& folder : work.saves) {
        if (!QFileInfo(folder.from).isDir()) {
            continue;
        }
        progress(total, total, QObject::tr("Copying saves from %1...").arg(QDir::toNativeSeparators(folder.from)));
        const QDir root(folder.from);
        for (const auto& entry : QDirListing(folder.from, QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly |
                                                              QDirListing::IteratorFlag::IncludeHidden)) {
            TRY(canceled());
            const auto rel = root.relativeFilePath(entry.absoluteFilePath());
            if (work.skipSaveFolders.contains(rel.section('/', 0, 0), Qt::CaseInsensitive)) {
                continue;
            }
            // always a copy: the instance writes its saves, and the originals must never change
            const auto to = FS::PathCombine(folder.to, rel);
            if (QFileInfo::exists(to)) {
                continue;
            }
            if (auto copied = GameFiles::placeFile(entry.absoluteFilePath(), to, GameFiles::PlaceMode::Copy, &cancel); !copied) {
                return std::unexpected(
                    QObject::tr("Could not copy the save %1: %2").arg(QDir::toNativeSeparators(entry.absoluteFilePath()), copied.error()));
            }
            outcome.saveFiles++;
        }
    }

    for (const auto& archive : work.downloads) {
        TRY(canceled());
        const QFileInfo from(archive);
        if (!from.isFile()) {
            continue;
        }
        const QFileInfo to(FS::PathCombine(work.downloadsDir, from.fileName()));
        if (to.exists()) {
            continue;  // brought over with another profile of the game already, or BetterNMM's own file of that name
        }
        progress(total, total, QObject::tr("Bringing over the download %1...").arg(from.fileName()));
        if (auto placed = GameFiles::placeFile(archive, to.absoluteFilePath(), GameFiles::PlaceMode::Link, &cancel); !placed) {
            return std::unexpected(QObject::tr("Could not bring over the download %1: %2").arg(from.fileName(), placed.error()));
        }
        outcome.downloads++;
    }
    return outcome;
}

}  // namespace

VortexImportTask::VortexImportTask(VortexImport::Game game, VortexImport::Profile profile, QStringList otherProfiles, Options options)
    : m_game(std::move(game)), m_profile(std::move(profile)), m_otherProfiles(std::move(otherProfiles)), m_options(options)
{}

void VortexImportTask::executeTask()
{
    if (m_game.gameDir.isEmpty() || !QFileInfo(m_game.gameDir).isDir()) {
        emitFailed(tr("Vortex has %1 in %2, which does not exist any more.")
                       .arg(m_game.definition.name, QDir::toNativeSeparators(m_game.gameDir)));
        return;
    }
    int appId = 0;
    if (const auto app = Steam::findAppByInstallDir(Steam::steamRoot(), m_game.gameDir)) {
        appId = app->appId;
    }
    auto* creation = new GameInstanceCreationTask(m_game.definition, m_game.gameDir, appId, GameInstance::Kind::Isolated);
    creation->setStagingPath(m_stagingPath);
    creation->setParentSettings(m_globalSettings);
    creation->setName(name());
    creation->setIcon(m_instIcon);
    creation->setGroup(m_instGroup);
    creation->setTargetDir(m_targetDir);
    m_creation.reset(creation);
    setAbortable(true);
    connect(creation, &Task::progress, this, &Task::setProgress);
    connect(creation, &Task::status, this, &Task::setStatus);
    connect(creation, &Task::failed, this, [this](const QString& reason) { emitFailed(reason); });
    connect(creation, &Task::aborted, this, [this] { emitAborted(); });
    connect(creation, &Task::succeeded, this, [this, creation] {
        m_messages << creation->messages();
        for (const auto& warning : creation->warnings()) {
            logWarning(warning);
        }
        importProfile();
    });
    m_creation->start();
}

void VortexImportTask::importProfile()
{
    setStatus(tr("Reading the mods of %1...").arg(m_profile.name));
    auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg"));
    settings->registerSetting("InstanceType", "");
    GameInstance instance(m_globalSettings, std::move(settings), m_stagingPath);

    Work work;
    work.modsDir = instance.modsRoot();
    work.listFile = FS::PathCombine(m_stagingPath, "mods.json");
    work.localPatterns = instance.localPatterns();
    work.pluginListDir = m_game.definition.hasPlugins() ? instance.pluginListDir() : QString();
    work.profileFolder = m_profile.folder;
    work.myGamesDir = instance.myGamesDir();
    if (m_profile.localSettings && !work.myGamesDir.isEmpty()) {
        work.iniFiles = m_game.definition.iniFiles;
    }
    work.enabled = m_profile.enabledMods;
    if (m_options.saves) {
        work.saves = VortexImport::saveFolders(m_game.definition, m_profile, m_game.gameDir, m_stagingPath);
        if (!m_profile.localSaves) {
            work.skipSaveFolders = m_otherProfiles;
        } else if (!work.myGamesDir.isEmpty() && !m_game.definition.iniFiles.isEmpty()) {
            work.profileSavesIni = FS::PathCombine(work.myGamesDir, m_game.definition.iniFiles.first());
            work.profileId = m_profile.id;
        }
    }
    if (m_options.downloads && !m_options.downloadsRoot.isEmpty()) {
        work.downloads = m_game.downloads;
        work.downloadsDir = FS::PathCombine(m_options.downloadsRoot, m_game.definition.domain);
    }

    const auto game = m_game;
    const auto profileId = m_profile.id;
    auto* watcher = new QFutureWatcher<Result<std::pair<Work, QStringList>>>(this);
    // reading folders and Vortex's manifests can take a while for big setups, so it happens off the UI thread too
    auto prepare = [game, profileId, work]() mutable -> Result<std::pair<Work, QStringList>> {
        QStringList warnings;
        const auto targets = VortexImport::typeTargets(game);
        QHash<QString, QSet<QString>> files;
        for (const auto& mod : game.mods) {
            ModWork item;
            item.mod = mod;
            item.source = FS::PathCombine(game.stagingDir, mod.folder);
            if (!QFileInfo(item.source).isDir()) {
                warnings << QObject::tr("The files of \"%1\" are missing from Vortex's staging folder (%2); it was left out.")
                                .arg(mod.name, QDir::toNativeSeparators(item.source));
                continue;
            }
            if (const auto it = targets.constFind(mod.type); it != targets.cend()) {
                item.prefix = *it;
            } else {
                item.prefix = mod.type.isEmpty() ? game.definition.dataPath : QString();
                warnings << QObject::tr(
                                "Vortex never deployed mods of the kind of \"%1\" (%2), so its files were put in the game folder "
                                "as they are; check they landed where the game expects them.")
                                .arg(mod.name, mod.type.isEmpty() ? QObject::tr("default") : mod.type);
            }
            const QDir root(item.source);
            QSet<QString> paths;
            for (const auto& entry : QDirListing(item.source, QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly |
                                                                  QDirListing::IteratorFlag::IncludeHidden)) {
                const auto rel = root.relativeFilePath(entry.absoluteFilePath());
                item.files << rel;
                item.bytes += entry.size();
                paths.insert((item.prefix.isEmpty() ? rel : item.prefix + '/' + rel).toLower());
            }
            files.insert(mod.id, paths);
            work.mods << item;
        }
        // which mod won each conflict is only known for the profile whose mods are in the game folder now
        const auto winners = game.deployedProfile == profileId ? VortexImport::deployedFiles(game) : QHash<QString, QString>();
        QList<VortexImport::Mod> present;
        for (const auto& item : std::as_const(work.mods)) {
            present << item.mod;
        }
        work.order = VortexImport::order(present, files, winners, work.enabled, &warnings);

        // room for what can not be linked: everything when Vortex's staging folder is on another drive
        const bool sameDrive = GameFiles::isSameVolume(game.stagingDir, work.modsDir) && GameFiles::supportsHardLinks(work.modsDir);
        const PatternMatcher local(work.localPatterns);
        qint64 needed = 0;
        for (const auto& item : std::as_const(work.mods)) {
            if (!sameDrive) {
                needed += item.bytes;
                continue;
            }
            for (const auto& rel : item.files) {
                if (local.matches(item.prefix.isEmpty() ? rel : item.prefix + '/' + rel)) {
                    needed += QFileInfo(FS::PathCombine(item.source, rel)).size();
                }
            }
        }
        for (const auto& folder : std::as_const(work.saves)) {
            needed += treeSize(folder.from, work.skipSaveFolders);
        }
        const QStorageInfo drive(QFileInfo(work.modsDir).absolutePath());
        // a little room is kept so Windows and the game do not run out right after
        constexpr qint64 reserve = 512ll * 1024 * 1024;
        if (drive.isValid() && drive.bytesAvailable() >= 0 && needed + reserve > drive.bytesAvailable()) {
            return std::unexpected(QObject::tr("Bringing over this profile takes %1 on %2, which has %3 free.")
                                       .arg(QLocale().formattedDataSize(needed), QDir::toNativeSeparators(drive.rootPath()),
                                            QLocale().formattedDataSize(drive.bytesAvailable())));
        }
        // archives on another drive than BetterNMM's downloads folder are copied, which can be a lot
        if (!work.downloads.isEmpty()) {
            const auto downloadsBase = FS::nearestExistentAncestor(work.downloadsDir);
            const bool linkable = GameFiles::supportsHardLinks(downloadsBase);
            qint64 archives = 0;
            for (const auto& archive : std::as_const(work.downloads)) {
                const QFileInfo from(archive);
                if (from.isFile() && !QFileInfo::exists(FS::PathCombine(work.downloadsDir, from.fileName())) &&
                    !(linkable && GameFiles::isSameVolume(archive, downloadsBase))) {
                    archives += from.size();
                }
            }
            const QStorageInfo downloadsDrive(downloadsBase);
            if (archives > 0 && downloadsDrive.isValid() && downloadsDrive.bytesAvailable() >= 0 &&
                archives + reserve > downloadsDrive.bytesAvailable()) {
                return std::unexpected(QObject::tr("Bringing over Vortex's downloads takes %1 on %2, which has %3 free. Untick bringing "
                                                   "over the archives, or make room.")
                                           .arg(QLocale().formattedDataSize(archives), QDir::toNativeSeparators(downloadsDrive.rootPath()),
                                                QLocale().formattedDataSize(downloadsDrive.bytesAvailable())));
            }
        }
        return std::pair{ work, warnings };
    };
    connect(watcher, &QFutureWatcher<Result<std::pair<Work, QStringList>>>::finished, this, [this, watcher] {
        auto prepared = watcher->result();
        watcher->deleteLater();
        if (m_cancel) {
            emitAborted();
            return;
        }
        if (!prepared) {
            emitFailed(prepared.error());
            return;
        }
        for (const auto& warning : prepared->second) {
            logWarning(warning);
        }
        const auto work = prepared->first;
        auto* copying = new QFutureWatcher<Result<Outcome>>(this);
        connect(copying, &QFutureWatcher<Result<Outcome>>::finished, this, [this, copying] {
            const auto outcome = copying->result();
            copying->deleteLater();
            if (m_cancel) {
                emitAborted();
                return;
            }
            if (!outcome) {
                emitFailed(outcome.error());
                return;
            }
            addExecutables();
            m_messages << tr("Brought over %n mod(s) from the Vortex profile \"%1\"", nullptr, outcome->mods).arg(m_profile.name) +
                              tr(", %n of them enabled.", nullptr, outcome->enabled);
            if (outcome->plugins) {
                m_messages << tr("The plugin load order is the profile's.");
            }
            if (outcome->inis > 0) {
                m_messages << tr("Brought over the profile's own %n INI file(s).", nullptr, outcome->inis);
            }
            if (m_options.saves) {
                m_messages << (outcome->saveFiles > 0
                                   ? tr("Copied %n save file(s); the originals are untouched.", nullptr, outcome->saveFiles)
                                   : tr("There were no saves to copy."));
            }
            if (outcome->downloads > 0) {
                m_messages << tr("Brought over %n downloaded archive(s).", nullptr, outcome->downloads);
            }
            emitSucceeded();
        });
        copying->setFuture(QtConcurrent::run([this, work] {
            return perform(work, m_cancel, [this](qint64 done, qint64 total, const QString& status) {
                QMetaObject::invokeMethod(
                    this,
                    [this, done, total, status] {
                        setProgress(done, total);
                        if (!status.isEmpty()) {
                            setStatus(status);
                        }
                    },
                    Qt::QueuedConnection);
            });
        }));
    });
    watcher->setFuture(QtConcurrent::run(prepare));
}

void VortexImportTask::addExecutables()
{
    auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg"));
    settings->registerSetting("InstanceType", "");
    GameInstance instance(m_globalSettings, std::move(settings), m_stagingPath);
    auto executables = instance.customExecutables();
    QStringList known;
    for (const auto& exe : instance.executables()) {
        known << QDir::cleanPath(exe.path).toLower();
    }
    // the script extender is one of the instance's programs once its mod is deployed, which has not happened yet
    if (const auto loader = instance.definition().scriptExtender; !loader.isEmpty()) {
        known << QDir::cleanPath(loader).toLower();
    }
    for (const auto& tool : m_game.tools) {
        GameExecutable exe;
        exe.name = tool.name;
        // a tool in the game folder runs from the instance's own game folder
        const auto inside = VortexImport::insideGamePath(m_game.gameDir, tool.path);
        exe.path = inside.value_or(tool.path);
        if (known.contains(QDir::cleanPath(exe.path).toLower())) {
            continue;
        }
        exe.arguments = tool.arguments;
        if (!tool.workingDir.isEmpty()) {
            exe.workingDir = VortexImport::insideGamePath(m_game.gameDir, tool.workingDir).value_or(tool.workingDir);
        }
        executables << exe;
        known << QDir::cleanPath(exe.path).toLower();
        m_messages << tr("Added Vortex's tool \"%1\" to the instance's programs.").arg(tool.name);
    }
    instance.setCustomExecutables(executables);
}

bool VortexImportTask::abort()
{
    m_cancel = true;
    if (m_creation && m_creation->isRunning()) {
        return m_creation->abort();
    }
    return true;
}
