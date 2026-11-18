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

#include "Steam.h"

#include <QDateTime>
#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

#include "FileSystem.h"

namespace Steam {

namespace {

class VdfParser {
   public:
    explicit VdfParser(const QByteArray& text) : m_text(text) {}

    QVariantMap parseObject(bool topLevel)
    {
        QVariantMap out;
        while (true) {
            skipSpaceAndComments();
            if (m_pos >= m_text.size()) {
                return out;
            }
            if (m_text[m_pos] == '}') {
                m_pos++;
                if (!topLevel) {
                    return out;
                }
                continue;
            }
            const auto key = readToken();
            skipSpaceAndComments();
            if (m_pos >= m_text.size()) {
                return out;
            }
            if (m_text[m_pos] == '{') {
                m_pos++;
                out.insert(key, parseObject(false));
            } else {
                out.insert(key, readToken());
            }
        }
    }

   private:
    void skipSpaceAndComments()
    {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                m_pos++;
            } else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
                while (m_pos < m_text.size() && m_text[m_pos] != '\n') {
                    m_pos++;
                }
            } else {
                return;
            }
        }
    }

    QString readToken()
    {
        QByteArray out;
        if (m_text[m_pos] == '"') {
            m_pos++;
            while (m_pos < m_text.size() && m_text[m_pos] != '"') {
                char c = m_text[m_pos++];
                if (c == '\\' && m_pos < m_text.size()) {
                    const char escaped = m_text[m_pos++];
                    switch (escaped) {
                        case 'n':
                            c = '\n';
                            break;
                        case 't':
                            c = '\t';
                            break;
                        default:
                            c = escaped;
                    }
                }
                out += c;
            }
            m_pos++;  // closing quote
        } else {
            while (m_pos < m_text.size()) {
                const char c = m_text[m_pos];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '{' || c == '}' || c == '"') {
                    break;
                }
                out += c;
                m_pos++;
            }
            if (out.isEmpty()) {
                m_pos++;  // never loop forever on stray characters
            }
        }
        return QString::fromUtf8(out);
    }

    const QByteArray& m_text;
    qsizetype m_pos = 0;
};

QVariantMap childMap(const QVariantMap& map, const QString& key)
{
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (it.key().compare(key, Qt::CaseInsensitive) == 0) {
            return it.value().toMap();
        }
    }
    return {};
}

QString childString(const QVariantMap& map, const QString& key)
{
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (it.key().compare(key, Qt::CaseInsensitive) == 0) {
            return it.value().toString();
        }
    }
    return {};
}

std::optional<AppInstall> readAppManifest(const QString& library, const QString& manifestPath)
{
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const auto state = childMap(parseVdf(file.readAll()), "AppState");
    AppInstall app;
    app.appId = childString(state, "appid").toInt();
    app.name = childString(state, "name");
    const auto installDir = childString(state, "installdir");
    if (app.appId <= 0 || installDir.isEmpty()) {
        return std::nullopt;
    }
    app.libraryPath = library;
    app.installDir = QDir::cleanPath(FS::PathCombine(library, "steamapps", "common", installDir));
    const auto depots = childMap(state, "InstalledDepots");
    for (auto it = depots.cbegin(); it != depots.cend(); ++it) {
        app.depots.append({ it.key(), childString(it.value().toMap(), "manifest") });
    }
    return app;
}

/// Minimal protobuf wire-format reader, enough for Steam's ContentManifestPayload
class ProtoReader {
   public:
    ProtoReader(const char* data, qsizetype size) : m_data(data), m_size(size) {}

    bool atEnd() const { return m_pos >= m_size; }

    bool readVarint(quint64& out)
    {
        out = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (m_pos >= m_size) {
                return false;
            }
            const auto byte = static_cast<quint8>(m_data[m_pos++]);
            out |= static_cast<quint64>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                return true;
            }
        }
        return false;
    }

    bool readField(quint32& field, quint32& wireType)
    {
        quint64 tag = 0;
        if (!readVarint(tag)) {
            return false;
        }
        field = static_cast<quint32>(tag >> 3);
        wireType = static_cast<quint32>(tag & 7);
        return true;
    }

    bool readBytes(const char*& out, qsizetype& length)
    {
        quint64 len = 0;
        if (!readVarint(len) || len > static_cast<quint64>(m_size - m_pos)) {
            return false;
        }
        out = m_data + m_pos;
        length = static_cast<qsizetype>(len);
        m_pos += length;
        return true;
    }

    bool skip(quint32 wireType)
    {
        quint64 ignored = 0;
        const char* data = nullptr;
        qsizetype len = 0;
        switch (wireType) {
            case 0:
                return readVarint(ignored);
            case 1:
                return advance(8);
            case 2:
                return readBytes(data, len);
            case 5:
                return advance(4);
            default:
                return false;
        }
    }

   private:
    bool advance(qsizetype n)
    {
        if (n > m_size - m_pos) {
            return false;
        }
        m_pos += n;
        return true;
    }

    const char* m_data;
    qsizetype m_size;
    qsizetype m_pos = 0;
};

constexpr quint32 g_payloadMagic = 0x71F617D0;
constexpr quint32 g_metadataMagic = 0x1F4812BE;
constexpr quint32 g_depotFlagDirectory = 64;

quint32 readLe32(const QByteArray& data, qsizetype pos)
{
    const auto* p = reinterpret_cast<const quint8*>(data.constData() + pos);
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<quint32>(p[3]) << 24);
}

}  // namespace

QVariantMap parseVdf(const QByteArray& text)
{
    VdfParser parser(text);
    return parser.parseObject(true);
}

QString steamRoot()
{
    QSettings user("HKEY_CURRENT_USER\\Software\\Valve\\Steam", QSettings::NativeFormat);
    auto path = user.value("SteamPath").toString();
    if (path.isEmpty()) {
        QSettings machine("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam", QSettings::NativeFormat);
        path = machine.value("InstallPath").toString();
    }
    if (path.isEmpty() || !QFileInfo(path).isDir()) {
        return {};
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QStringList libraryFolders(const QString& root)
{
    QStringList out;
    if (root.isEmpty()) {
        return out;
    }
    out << root;
    QFile file(FS::PathCombine(root, "steamapps", "libraryfolders.vdf"));
    if (!file.open(QIODevice::ReadOnly)) {
        return out;
    }
    const auto folders = childMap(parseVdf(file.readAll()), "libraryfolders");
    for (auto it = folders.cbegin(); it != folders.cend(); ++it) {
        const auto path = childString(it.value().toMap(), "path");
        if (path.isEmpty()) {
            continue;
        }
        const auto clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        if (!out.contains(clean, Qt::CaseInsensitive) && QFileInfo(clean).isDir()) {
            out << clean;
        }
    }
    return out;
}

QList<AppInstall> installedApps(const QString& root)
{
    QList<AppInstall> apps;
    for (const auto& library : libraryFolders(root)) {
        const auto steamapps = FS::PathCombine(library, "steamapps");
        for (const auto& entry : QDirListing(steamapps, { "appmanifest_*.acf" }, QDirListing::IteratorFlag::FilesOnly)) {
            if (auto app = readAppManifest(library, entry.absoluteFilePath()); app && QFileInfo(app->installDir).isDir()) {
                apps << *app;
            }
        }
    }
    return apps;
}

std::optional<AppInstall> findApp(const QString& root, int appId)
{
    for (const auto& library : libraryFolders(root)) {
        const auto manifest = FS::PathCombine(library, "steamapps", QString("appmanifest_%1.acf").arg(appId));
        if (auto app = readAppManifest(library, manifest); app && QFileInfo(app->installDir).isDir()) {
            return app;
        }
    }
    return std::nullopt;
}

std::optional<AppInstall> findAppByInstallDir(const QString& root, const QString& dir)
{
    const auto wanted = QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
    for (const auto& app : installedApps(root)) {
        if (app.installDir.compare(wanted, Qt::CaseInsensitive) == 0) {
            return app;
        }
    }
    return std::nullopt;
}

Result<QList<DepotFile>> parseDepotManifest(const QByteArray& data)
{
    QList<DepotFile> files;
    bool sawPayload = false;
    qsizetype pos = 0;
    while (pos + 8 <= data.size()) {
        const auto magic = readLe32(data, pos);
        const auto length = static_cast<qsizetype>(readLe32(data, pos + 4));
        pos += 8;
        if (magic != g_payloadMagic && magic != g_metadataMagic) {
            break;  // signature and end-of-manifest sections carry nothing we need
        }
        if (length > data.size() - pos) {
            return std::unexpected(QString("Depot manifest is truncated"));
        }
        ProtoReader reader(data.constData() + pos, length);
        pos += length;
        quint32 field = 0;
        quint32 wireType = 0;
        if (magic == g_metadataMagic) {
            while (!reader.atEnd() && reader.readField(field, wireType)) {
                if (field == 4 && wireType == 0) {  // filenames_encrypted
                    quint64 encrypted = 0;
                    if (!reader.readVarint(encrypted)) {
                        return std::unexpected(QString("Depot manifest metadata is malformed"));
                    }
                    if (encrypted != 0) {
                        return std::unexpected(QString("Depot manifest file names are encrypted"));
                    }
                } else if (!reader.skip(wireType)) {
                    return std::unexpected(QString("Depot manifest metadata is malformed"));
                }
            }
            continue;
        }
        sawPayload = true;
        while (!reader.atEnd()) {
            if (!reader.readField(field, wireType)) {
                return std::unexpected(QString("Depot manifest payload is malformed"));
            }
            if (field != 1 || wireType != 2) {
                if (!reader.skip(wireType)) {
                    return std::unexpected(QString("Depot manifest payload is malformed"));
                }
                continue;
            }
            const char* mappingData = nullptr;
            qsizetype mappingLength = 0;
            if (!reader.readBytes(mappingData, mappingLength)) {
                return std::unexpected(QString("Depot manifest payload is malformed"));
            }
            ProtoReader mapping(mappingData, mappingLength);
            DepotFile file;
            quint64 flags = 0;
            while (!mapping.atEnd()) {
                if (!mapping.readField(field, wireType)) {
                    return std::unexpected(QString("Depot manifest file entry is malformed"));
                }
                bool ok = true;
                if (field == 1 && wireType == 2) {
                    const char* name = nullptr;
                    qsizetype nameLength = 0;
                    ok = mapping.readBytes(name, nameLength);
                    QByteArray raw(name, nameLength);
                    while (raw.endsWith('\0')) {
                        raw.chop(1);
                    }
                    file.path = QString::fromUtf8(raw).replace('\\', '/');
                } else if (field == 2 && wireType == 0) {
                    ok = mapping.readVarint(file.size);
                } else if (field == 3 && wireType == 0) {
                    ok = mapping.readVarint(flags);
                } else {
                    ok = mapping.skip(wireType);
                }
                if (!ok) {
                    return std::unexpected(QString("Depot manifest file entry is malformed"));
                }
            }
            if (file.path.isEmpty()) {
                continue;
            }
            file.isDirectory = (flags & g_depotFlagDirectory) != 0;
            files << file;
        }
    }
    if (!sawPayload) {
        return std::unexpected(QString("Not a depot manifest"));
    }
    return files;
}

Result<QList<DepotFile>> vanillaFiles(const QString& root, const AppInstall& app)
{
    if (app.depots.isEmpty()) {
        return std::unexpected(QString("Steam lists no installed depots for app %1").arg(app.appId));
    }
    QList<DepotFile> files;
    for (const auto& [depot, manifest] : app.depots) {
        const auto path = FS::PathCombine(root, "depotcache", QString("%1_%2.manifest").arg(depot, manifest));
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return std::unexpected(QString("Steam has not cached the file list of depot %1 (%2)").arg(depot, path));
        }
        auto parsed = parseDepotManifest(file.readAll());
        if (!parsed) {
            return std::unexpected(QString("%1 (%2)").arg(parsed.error(), path));
        }
        files << *parsed;
    }
    return files;
}

namespace {

std::optional<qint64> playtimeIn(const QVariantMap& localConfig, int appId)
{
    // UserLocalConfigStore/Software/Valve/Steam/apps/<appid>/Playtime, with whatever case Steam wrote ("valve")
    auto child = [](const QVariant& node, const QString& key) {
        const auto map = node.toMap();
        for (auto it = map.begin(); it != map.end(); ++it) {
            if (it.key().compare(key, Qt::CaseInsensitive) == 0) {
                return it.value();
            }
        }
        return QVariant();
    };
    QVariant node = localConfig;
    for (const auto& key : { QString("UserLocalConfigStore"), QString("Software"), QString("Valve"), QString("Steam"), QString("apps"),
                             QString::number(appId), QString("Playtime") }) {
        node = child(node, key);
        if (!node.isValid()) {
            return std::nullopt;
        }
    }
    bool ok = false;
    const auto minutes = node.toString().toLongLong(&ok);
    return ok ? std::optional(minutes) : std::nullopt;
}

}  // namespace

std::optional<qint64> playtimeMinutes(const QByteArray& localConfig, int appId)
{
    return playtimeIn(parseVdf(localConfig), appId);
}

std::optional<qint64> playtimeMinutes(const QString& root, int appId)
{
    if (root.isEmpty() || appId <= 0) {
        return std::nullopt;
    }
    // the account Steam is signed in to, else the one that signed in last
    qint64 account = QSettings(R"(HKEY_CURRENT_USER\Software\Valve\Steam\ActiveProcess)", QSettings::NativeFormat).value("ActiveUser").toLongLong();
    if (account <= 0) {
        QFile users(FS::PathCombine(root, "config/loginusers.vdf"));
        if (users.open(QIODevice::ReadOnly)) {
            const auto all = parseVdf(users.readAll()).value("users").toMap();
            for (auto it = all.begin(); it != all.end(); ++it) {
                if (it.value().toMap().value("MostRecent").toString() == "1") {
                    // SteamID64 minus the individual-account base is the account id the userdata folder is named after
                    account = it.key().toLongLong() - 76561197960265728LL;
                }
            }
        }
    }
    if (account <= 0) {
        return std::nullopt;
    }
    const auto path = FS::PathCombine(root, "userdata", QString::number(account), "config/localconfig.vdf");
    // the file can be megabytes and the main window asks on every selection, so a parse is reused until Steam rewrites it
    static QHash<QString, std::pair<QDateTime, QVariantMap>> cache;
    const auto modified = QFileInfo(path).lastModified();
    if (!cache.contains(path) || cache.value(path).first != modified) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return std::nullopt;
        }
        cache.insert(path, { modified, parseVdf(file.readAll()) });
    }
    return playtimeIn(cache.value(path).second, appId);
}

}  // namespace Steam
