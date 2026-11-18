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

#include "Plugin.h"

#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QtConcurrent>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Loot {

namespace {

struct ParseError {
    QString message;
};

quint32 u32(const uchar* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<quint32>(p[3]) << 24);
}

quint16 u16(const uchar* p)
{
    return static_cast<quint16>(p[0] | (p[1] << 8));
}

int headerLength(GameType game)
{
    return game == GameType::Oblivion ? 20 : 24;
}

QByteArrayView untilNull(QByteArrayView data)
{
    const auto end = data.indexOf('\0');
    return end < 0 ? data : data.first(end);
}

/// A plugin file in memory, read as esplugin does: the TES4 record, then groups of records
class PluginReader {
   public:
    PluginReader(GameType game, const uchar* data, qint64 size) : m_game(game), m_data(data), m_size(size) {}

    PluginHeader header()
    {
        const int length = headerLength(m_game);
        need(length);
        if (std::memcmp(m_data, "TES4", 4) != 0) {
            throw ParseError{ QObject::tr("the file does not start with a TES4 record") };
        }
        PluginHeader header;
        const quint32 subrecordsSize = u32(m_data + 4);
        header.flags = u32(m_data + 8);
        m_pos = length;
        need(subrecordsSize);
        const uchar* p = m_data + m_pos;
        const uchar* end = p + subrecordsSize;
        quint32 largeSize = 0;  // an XXXX subrecord gives the size of the next one
        while (p < end) {
            if (end - p < 6) {
                throw ParseError{ QObject::tr("a subrecord header is cut off") };
            }
            const QByteArrayView type(reinterpret_cast<const char*>(p), 4);
            const quint32 size = largeSize > 0 ? largeSize : u16(p + 4);
            p += 6;
            if (static_cast<quint64>(end - p) < size) {
                throw ParseError{ QObject::tr("a subrecord is cut off") };
            }
            const QByteArrayView data(reinterpret_cast<const char*>(p), size);
            if (type == "XXXX") {
                if (size < 4) {
                    throw ParseError{ QObject::tr("an XXXX subrecord is too short") };
                }
                largeSize = u32(p);
            } else {
                largeSize = 0;
                if (type == "MAST") {
                    header.masters << fromWindows1252(untilNull(data));
                } else if (type == "SNAM" && !header.description) {
                    header.description = fromWindows1252(untilNull(data));
                }
            }
            p += size;
        }
        m_pos += subrecordsSize;
        return header;
    }

    /// The FormIDs of every record after the header, in file order
    std::vector<quint32> formIds()
    {
        std::vector<quint32> ids;
        const int length = headerLength(m_game);
        while (m_pos < m_size) {
            need(length);
            if (std::memcmp(m_data + m_pos, "GRUP", 4) != 0) {
                throw ParseError{ QObject::tr("expected a GRUP at offset %1").arg(m_pos) };
            }
            const auto size = groupContentSize(m_data + m_pos);
            m_pos += length;
            readRecords(size, ids);
        }
        return ids;
    }

   private:
    void need(qint64 count) const
    {
        if (count < 0 || m_pos + count > m_size) {
            throw ParseError{ QObject::tr("unexpected end of file at offset %1").arg(m_pos) };
        }
    }

    quint32 groupContentSize(const uchar* header) const
    {
        const quint32 size = u32(header + 4);
        if (size < static_cast<quint32>(headerLength(m_game))) {
            throw ParseError{ QObject::tr("invalid group size at offset %1").arg(m_pos) };
        }
        return size - headerLength(m_game);
    }

    void readRecords(quint32 size, std::vector<quint32>& ids)
    {
        const int length = headerLength(m_game);
        quint64 read = 0;
        while (read < size) {
            need(length);
            const uchar* header = m_data + m_pos;
            m_pos += length;
            read += length;
            if (std::memcmp(header, "GRUP", 4) == 0) {
                const auto inner = groupContentSize(header);
                readRecords(inner, ids);
                read += inner;
            } else {
                // skipped without a bounds check, like esplugin's seek: a cut-off last record still loads
                const quint32 subrecords = u32(header + 4);
                m_pos += subrecords;
                read += subrecords;
                if (const quint32 formId = u32(header + 12); formId != 0) {
                    ids.push_back(formId);
                }
            }
        }
    }

    GameType m_game;
    const uchar* m_data;
    qint64 m_size;
    qint64 m_pos = 0;
};

/// A mapped (or, when mapping fails, read) file
struct FileData {
    QFile file;
    QByteArray buffer;
    const uchar* data = nullptr;
    qint64 size = 0;

    bool open(const QString& path, qint64 limit = -1)
    {
        file.setFileName(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        size = limit >= 0 ? qMin(limit, file.size()) : file.size();
        if (size == 0) {
            data = reinterpret_cast<const uchar*>("");
            return true;
        }
        data = limit < 0 ? file.map(0, size) : nullptr;
        if (!data) {
            buffer = file.read(size);
            data = reinterpret_cast<const uchar*>(buffer.constData());
            size = buffer.size();
        }
        return true;
    }
};

bool hasExtension(const QString& name, const char* extension)
{
    return name.endsWith(QLatin1Char('.') + QLatin1String(extension), Qt::CaseInsensitive);
}

QString pluginStem(const QString& name)
{
    const auto dot = name.lastIndexOf('.');
    return dot > 0 ? name.left(dot) : name;
}

/// The plugin's extension, looking through ".ghost"
QString pluginExtension(const QString& path)
{
    auto name = QFileInfo(path).fileName();
    if (hasExtension(name, "ghost")) {
        name.chop(6);
    }
    const auto dot = name.lastIndexOf('.');
    return dot > 0 ? name.mid(dot + 1).toLower() : QString();
}

quint64 nameHash(const QString& name)
{
    return qHash(name.toLower(), 0);
}

////// Archives (libloot archive/) //////

void normalise(QByteArray& path)
{
    for (auto& byte : path) {
        if (static_cast<quint8>(byte) > 127) {
            continue;
        }
        byte = byte == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(byte)));
    }
}

QByteArray trimAndNormalise(QByteArrayView bytes)
{
    if (bytes.endsWith('\0')) {
        bytes.chop(1);
    }
    while (!bytes.isEmpty() && (bytes.front() == '\\' || bytes.front() == '/')) {
        bytes = bytes.mid(1);
    }
    while (!bytes.isEmpty() && (bytes.back() == '\\' || bytes.back() == '/')) {
        bytes.chop(1);
    }
    QByteArray out = bytes.toByteArray();
    normalise(out);
    return out;
}

ArchiveAssets bsaAssets(const uchar* data, qint64 size)
{
    constexpr qint64 headerSize = 36;
    if (size < headerSize) {
        throw ParseError{ QObject::tr("unexpected end of file") };
    }
    const quint32 version = u32(data + 4);
    const quint32 recordsOffset = u32(data + 8);
    const quint32 flags = u32(data + 12);
    const quint32 folderCount = u32(data + 16);
    const quint32 fileCount = u32(data + 20);
    const quint32 folderNamesLength = u32(data + 24);
    const quint32 fileNamesLength = u32(data + 28);
    if (recordsOffset != headerSize) {
        throw ParseError{ QObject::tr("invalid records offset %1").arg(recordsOffset) };
    }
    qint64 folderRecordSize = 0;
    if (version == 103 || version == 104) {
        folderRecordSize = 16;
    } else if (version == 105) {
        folderRecordSize = 24;
    } else {
        throw ParseError{ QObject::tr("unsupported BSA version %1").arg(version) };
    }
    if ((flags & 0x40) != 0) {
        throw ParseError{ QObject::tr("big-endian archive") };
    }
    if ((flags & 0x1) == 0 || (flags & 0x2) == 0) {
        throw ParseError{ QObject::tr("the archive does not include its directory or file names") };
    }
    const qint64 foldersSize = folderRecordSize * folderCount;
    const qint64 fileRecordsSize = static_cast<qint64>(folderCount) + folderNamesLength + static_cast<qint64>(fileCount) * 16;
    if (headerSize + foldersSize + fileRecordsSize + fileNamesLength > size) {
        throw ParseError{ QObject::tr("unexpected end of file") };
    }
    const uchar* folders = data + headerSize;
    const QByteArrayView fileRecords(reinterpret_cast<const char*>(folders + foldersSize), fileRecordsSize);
    const QByteArrayView fileNames(reinterpret_cast<const char*>(folders + foldersSize + fileRecordsSize), fileNamesLength);
    const qint64 baseline = headerSize + foldersSize + fileNamesLength;

    std::vector<std::pair<QByteArray, quint32>> folderNames;
    for (quint32 i = 0; i < folderCount; i++) {
        const uchar* record = folders + i * folderRecordSize;
        const quint32 count = u32(record + 8);
        const quint32 offset = folderRecordSize == 16 ? u32(record + 12) : u32(record + 16);
        const qint64 lengthAt = static_cast<qint64>(offset) - baseline;
        if (lengthAt < 0 || lengthAt >= fileRecords.size()) {
            throw ParseError{ QObject::tr("invalid folder name offset %1").arg(lengthAt) };
        }
        const qint64 nameLength = static_cast<quint8>(fileRecords[lengthAt]);
        if (lengthAt + 1 + nameLength > fileRecords.size()) {
            throw ParseError{ QObject::tr("invalid folder name range") };
        }
        folderNames.emplace_back(trimAndNormalise(fileRecords.mid(lengthAt + 1, nameLength)), count);
    }
    ArchiveAssets assets;
    qsizetype nameStart = 0;
    bool namesLeft = true;
    for (const auto& [folder, count] : folderNames) {
        for (quint32 k = 0; k < count && namesLeft; k++) {
            auto end = fileNames.indexOf('\0', nameStart);
            if (end < 0) {
                end = fileNames.size();
                namesLeft = false;  // the last piece of a split; nothing follows it
            }
            assets[folder].insert(trimAndNormalise(fileNames.sliced(nameStart, end - nameStart)));
            nameStart = end + 1;
        }
    }
    return assets;
}

ArchiveAssets ba2Assets(const uchar* data, qint64 size)
{
    if (size < 24) {
        throw ParseError{ QObject::tr("unexpected end of file") };
    }
    const quint32 version = u32(data + 4);
    const QByteArrayView type(reinterpret_cast<const char*>(data + 8), 4);
    const quint32 fileCount = u32(data + 12);
    const quint64 namesOffset = u32(data + 16) | (static_cast<quint64>(u32(data + 20)) << 32);
    if (version != 1 && version != 2 && version != 3 && version != 7 && version != 8) {
        throw ParseError{ QObject::tr("unsupported BA2 version %1").arg(version) };
    }
    if (type != "GNRL" && type != "DX10") {
        throw ParseError{ QObject::tr("unsupported BA2 type") };
    }
    ArchiveAssets assets;
    quint64 at = namesOffset;
    for (quint32 i = 0; i < fileCount; i++) {
        if (at + 2 > static_cast<quint64>(size)) {
            throw ParseError{ QObject::tr("unexpected end of file") };
        }
        const quint16 length = u16(data + at);
        at += 2;
        if (at + length > static_cast<quint64>(size)) {
            throw ParseError{ QObject::tr("unexpected end of file") };
        }
        QByteArray path(reinterpret_cast<const char*>(data + at), length);
        at += length;
        normalise(path);
        while (path.endsWith('\\')) {
            path.chop(1);
        }
        while (path.startsWith('\\')) {
            path.remove(0, 1);
        }
        // rsplit_on: without a separator the folder is the path minus its last byte and the file name is empty
        const auto slash = path.lastIndexOf('\\');
        const QByteArray file = slash < 0 ? QByteArray() : path.mid(slash + 1);
        path.truncate(slash < 0 ? qMax<qsizetype>(path.size() - 1, 0) : slash);
        assets[path].insert(file);
    }
    return assets;
}

/// The archives libloot counts as loaded by a plugin
QStringList associatedArchives(GameType game, const QString& dataPath, const QString& pluginName, const QStringList& dataArchives)
{
    const auto stem = pluginStem(pluginName);
    QStringList out;
    auto withSuffixes = [&](std::initializer_list<const char*> suffixes, const char* extension) {
        for (const char* suffix : suffixes) {
            const auto path = QDir(dataPath).filePath(stem + QLatin1String(suffix) + '.' + QLatin1String(extension));
            if (QFileInfo::exists(path)) {
                out << path;
            }
        }
    };
    switch (game) {
        case GameType::Skyrim:
            withSuffixes({ "" }, "bsa");
            break;
        case GameType::SkyrimSE:
            withSuffixes({ "", " - Textures" }, "bsa");
            break;
        case GameType::Starfield:
            withSuffixes({ " - Main", " - Textures", " - Localization", " - Voices_en" }, "ba2");
            break;
        case GameType::Oblivion:
        case GameType::Fallout3:
        case GameType::FalloutNV:
        case GameType::Fallout4: {
            if (game == GameType::Oblivion && !hasExtension(pluginName, "esp")) {
                break;
            }
            // any archive whose name starts with the plugin's name (compared as the same file would be)
            const auto stemBytes = stem.toUtf8().size();
            for (const auto& archive : dataArchives) {
                const auto bytes = archive.toUtf8();
                if (bytes.size() < stemBytes) {
                    continue;
                }
                const auto prefix = QString::fromUtf8(bytes.first(stemBytes));
                if (prefix.toUtf8() != bytes.first(stemBytes)) {
                    continue;  // not cut at a character boundary
                }
#ifdef Q_OS_WIN
                const auto sensitivity = Qt::CaseInsensitive;
#else
                const auto sensitivity = Qt::CaseSensitive;
#endif
                if (prefix.compare(stem, sensitivity) == 0) {
                    out << QDir(dataPath).filePath(archive);
                }
            }
            break;
        }
    }
    return out;
}

}  // namespace

ArchiveAssets assetsInArchives(const QStringList& archivePaths)
{
    ArchiveAssets assets;
    for (const auto& path : archivePaths) {
        FileData file;
        if (!file.open(path)) {
            qWarning() << "Could not read" << path;
            continue;
        }
        try {
            ArchiveAssets found;
            if (file.size >= 4 && std::memcmp(file.data, "BSA\0", 4) == 0) {
                found = bsaAssets(file.data, file.size);
            } else if (file.size >= 4 && std::memcmp(file.data, "BTDX", 4) == 0) {
                found = ba2Assets(file.data, file.size);
            } else {
                throw ParseError{ QObject::tr("not a Bethesda archive") };
            }
            for (auto& [folder, files] : found) {
                assets[folder].merge(files);
            }
        } catch (const ParseError& error) {
            qWarning() << "Could not read the archive" << path << ":" << error.message;
        }
    }
    return assets;
}

bool doAssetsOverlap(const ArchiveAssets& a, const ArchiveAssets& b)
{
    auto left = a.begin();
    auto right = b.begin();
    while (left != a.end() && right != b.end()) {
        if (left->first < right->first) {
            ++left;
        } else if (right->first < left->first) {
            ++right;
        } else {
            for (const auto& file : left->second) {
                if (right->second.contains(file)) {
                    return true;
                }
            }
            ++left;
        }
    }
    return false;
}

Result<PluginHeader> readPluginHeader(GameType game, const QString& path)
{
    FileData file;
    if (!file.open(path)) {
        return std::unexpected(file.file.errorString());
    }
    try {
        return PluginReader(game, file.data, file.size).header();
    } catch (const ParseError& error) {
        return std::unexpected(error.message);
    }
}

bool isMasterFile(GameType game, const QString& path, quint32 flags)
{
    if ((flags & 0x1) != 0) {
        return true;
    }
    if (game == GameType::SkyrimSE || game == GameType::Fallout4 || game == GameType::Starfield) {
        const auto extension = pluginExtension(path);
        return extension == "esm" || extension == "esl";
    }
    return false;
}

bool Plugin::recordsOverlap(const Plugin& other) const
{
    auto a = records.begin();
    auto b = other.records.begin();
    while (a != records.end() && b != other.records.end()) {
        if (*a < *b) {
            ++a;
        } else if (*b < *a) {
            ++b;
        } else {
            return true;
        }
    }
    return false;
}

std::optional<QString> extractVersion(const QString& description)
{
    // JavaScript's \s, which libloot's regex engine uses
    static const QString space = QStringLiteral("[\\t\\n\\x{0B}\\f\\r \\x{A0}\\x{1680}\\x{2000}-\\x{200A}\\x{2028}\\x{2029}\\x{202F}\\x{205F}\\x{3000}\\x{FEFF}]");
    static const QString pseudosem = QStringLiteral("(\\d+(?:\\.\\d+)+(?:[-._:]?[A-Za-z0-9]+)*)(?:[^,]|$)");
    static const QList<QRegularExpression> regexes = [] {
        QList<QRegularExpression> list;
        for (const auto& pattern : { QStringLiteral("(\\d{1,2}/\\d{1,2}/\\d{1,4} \\d{1,2}:\\d{1,2}:\\d{1,2})"),
                                     QStringLiteral("version:?") + space + pseudosem, QStringLiteral("(?:^|v|") + space + ")" + pseudosem,
                                     QStringLiteral("(?:^|v|version:") + space + "*)(\\d+)" }) {
            list << QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
        }
        return list;
    }();
    for (const auto& regex : regexes) {
        const auto match = regex.match(description);
        if (match.hasMatch() && match.capturedStart(1) >= 0) {
            return match.captured(1).trimmed();
        }
    }
    return std::nullopt;
}

Result<std::vector<Plugin>> loadPlugins(GameType game, const QString& dataPath, const QStringList& pluginNames)
{
    const char* archiveExtension = game == GameType::Fallout4 || game == GameType::Starfield ? "ba2" : "bsa";
    QStringList dataArchives;
    for (const auto& entry : QDirListing(dataPath, QDirListing::IteratorFlag::FilesOnly)) {
        if (hasExtension(entry.fileName(), archiveExtension) && !entry.isSymLink()) {
            dataArchives << entry.fileName();
        }
    }

    struct Loaded {
        Plugin plugin;
        std::vector<quint32> formIds;
        QString error;
    };
    auto load = [&](const QString& name) {
        Loaded loaded;
        auto& plugin = loaded.plugin;
        plugin.name = hasExtension(name, "ghost") ? name.chopped(6) : name;
        plugin.path = QDir(dataPath).filePath(name);
        FileData file;
        if (!file.open(plugin.path)) {
            loaded.error = QObject::tr("%1: %2").arg(name, file.file.errorString());
            return loaded;
        }
        try {
            PluginReader reader(game, file.data, file.size);
            plugin.header = reader.header();
            // A plugin without masters overrides nothing, and its own records can only be shared with plugins that
            // list it as a master, which are joined to it by a master edge before overlaps are looked at: its records
            // never decide anything. Skipping them spares reading the whole of Skyrim.esm (250 MB: 7.1 s cold).
            if (!plugin.header.masters.isEmpty()) {
                loaded.formIds = reader.formIds();
            }
        } catch (const ParseError& error) {
            loaded.error = QObject::tr("%1 is not a valid plugin: %2").arg(name, error.message);
            return loaded;
        }
        plugin.master = isMasterFile(game, plugin.path, plugin.header.flags);
        plugin.blueprint = game == GameType::Starfield && (plugin.header.flags & 0x800) != 0;
        if (plugin.header.description) {
            plugin.version = extractVersion(*plugin.header.description);
        }
        plugin.assets = assetsInArchives(associatedArchives(game, dataPath, plugin.name, dataArchives));
        for (const auto& [folder, files] : plugin.assets) {
            plugin.assetCount += static_cast<int>(files.size());
        }
        return loaded;
    };
    auto loaded = QtConcurrent::blockingMapped<std::vector<Loaded>>(pluginNames, load);
    for (const auto& entry : loaded) {
        if (!entry.error.isEmpty()) {
            return std::unexpected(entry.error);
        }
    }

    // Starfield FormIDs depend on how big (full, medium, small) each master is, so masters must be loaded too
    QHash<QString, int> scales;  // lower-case name -> 0 full, 1 medium, 2 small
    for (const auto& entry : loaded) {
        const auto flags = entry.plugin.header.flags;
        const bool light = (flags & 0x100) != 0 || ((flags & 0x200) == 0 && pluginExtension(entry.plugin.path) == "esl");
        scales.insert(entry.plugin.name.toLower(), light ? 2 : ((flags & 0x400) != 0 ? 1 : 0));
    }

    std::vector<Plugin> plugins;
    plugins.reserve(loaded.size());
    for (auto& entry : loaded) {
        auto& plugin = entry.plugin;
        struct Source {
            quint64 hash;
            quint32 modIndexMask;
            quint32 objectIndexMask;
        };
        std::vector<Source> masters;
        quint32 parentMask = 0x00FFFFFF;
        if (game == GameType::Starfield) {
            const int scale = scales.value(plugin.name.toLower());
            parentMask = scale == 2 ? 0xFFF : (scale == 1 ? 0xFFFF : 0x00FFFFFF);
            quint32 full = 0;
            quint32 medium = 0xFD000000;
            quint32 small = 0xFE000000;
            for (const auto& master : plugin.header.masters) {
                const auto it = scales.constFind(master.toLower());
                if (it == scales.constEnd()) {
                    return std::unexpected(QObject::tr("%1 needs %2, which is not installed").arg(plugin.name, master));
                }
                if (*it == 0) {
                    masters.push_back({ nameHash(master), full, 0x00FFFFFF });
                    full += 0x01000000;
                } else if (*it == 1) {
                    masters.push_back({ nameHash(master), medium, 0xFFFF });
                    medium += 0x00010000;
                } else {
                    masters.push_back({ nameHash(master), small, 0xFFF });
                    small += 0x00001000;
                }
            }
        } else {
            for (int i = 0; i < plugin.header.masters.size() && i < 256; i++) {
                masters.push_back({ nameHash(plugin.header.masters[i]), static_cast<quint32>(i) << 24, 0x00FFFFFF });
            }
        }
        const auto parentHash = nameHash(QFileInfo(plugin.path).fileName());
        plugin.records.reserve(entry.formIds.size());
        for (const quint32 formId : entry.formIds) {
            const auto source = std::find_if(masters.begin(), masters.end(),
                                             [formId](const Source& m) { return (formId & ~m.objectIndexMask) == m.modIndexMask; });
            if (source != masters.end()) {
                plugin.records.push_back({ formId & source->objectIndexMask, source->hash, true });
                plugin.overrideCount++;
            } else {
                plugin.records.push_back({ formId & parentMask, parentHash, false });
            }
        }
        std::sort(plugin.records.begin(), plugin.records.end());
        plugins.push_back(std::move(plugin));
    }
    return plugins;
}

}  // namespace Loot
