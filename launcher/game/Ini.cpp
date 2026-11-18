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

#include "Ini.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

#include "FileSystem.h"
#include "game/Deployer.h"
#include "game/GameFiles.h"
#include "game/GameInstance.h"
#include "game/ModList.h"

namespace Ini {

Line parse(const QString& line)
{
    const auto trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return { .kind = Line::Kind::Blank };
    }
    if (trimmed.startsWith(';') || trimmed.startsWith('#')) {
        return { .kind = Line::Kind::Comment };
    }
    if (trimmed.startsWith('[')) {
        if (const auto end = trimmed.indexOf(']'); end > 1) {
            return { .kind = Line::Kind::Section, .section = trimmed.mid(1, end - 1).trimmed() };
        }
    }
    if (const auto equals = line.indexOf('='); equals > 0) {
        if (const auto key = line.left(equals).trimmed(); !key.isEmpty()) {
            return { .kind = Line::Kind::Setting, .key = key, .value = line.mid(equals + 1).trimmed() };
        }
    }
    return {};
}

QString withValue(const QString& line, const QString& value)
{
    auto start = line.indexOf('=') + 1;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
        start++;
    }
    return line.left(start) + value;
}

namespace {

/// Bethesda names settings after their type: bShowCompass, iSize W, fDefaultFOV, sLanguage, uGridsToLoad
QChar typePrefix(const QString& key)
{
    return key.size() >= 2 && key[0].isLower() && key[1].isUpper() ? key[0] : QChar();
}

}  // namespace

QString checkValue(const QString& key, const QString& value)
{
    bool ok = true;
    switch (typePrefix(key).unicode()) {
        case 'b':
            ok = value == "0" || value == "1";
            return ok ? QString() : QObject::tr("%1 is an on/off setting: 1 is on, 0 is off.").arg(key);
        case 'i':
            value.toLongLong(&ok);
            return ok ? QString() : QObject::tr("%1 takes a whole number.").arg(key);
        case 'u':
            value.toULongLong(&ok);
            return ok ? QString() : QObject::tr("%1 takes a whole number that is not negative.").arg(key);
        case 'f':
            value.toDouble(&ok);
            return ok ? QString() : QObject::tr("%1 takes a number; use a dot for decimals, like 1.5.").arg(key);
        default:
            return {};
    }
}

QString typeDescription(const QString& key)
{
    switch (typePrefix(key).unicode()) {
        case 'b':
            return QObject::tr("On/off: 1 is on, 0 is off");
        case 'i':
            return QObject::tr("Whole number");
        case 'u':
            return QObject::tr("Whole number, not negative");
        case 'f':
            return QObject::tr("Number, decimals with a dot");
        case 's':
            return QObject::tr("Text");
        default:
            return {};
    }
}

Text decode(const QByteArray& data)
{
    Text out;
    // a new or one-line file gets Windows line ends, which every game reading INIs on Windows expects
    out.crlf = data.contains("\r\n") || !data.contains('\n');
    const std::pair<const char*, QStringConverter::Encoding> marks[] = { { "\xEF\xBB\xBF", QStringConverter::Utf8 },
                                                                         { "\xFF\xFE", QStringConverter::Utf16LE },
                                                                         { "\xFE\xFF", QStringConverter::Utf16BE } };
    const auto mark = std::find_if(std::begin(marks), std::end(marks), [&data](const auto& m) { return data.startsWith(m.first); });
    if (mark != std::end(marks)) {
        out.encoding = mark->second;
        out.bom = true;
        out.text = QStringDecoder(mark->second)(data);  // the decoder drops the mark itself
    } else {
        QStringDecoder utf8(QStringConverter::Utf8);
        out.text = utf8(data);
        if (utf8.hasError()) {
            // older games write INIs in the system code page
            out.encoding = QStringConverter::System;
            out.text = QStringDecoder(QStringConverter::System)(data);
        }
    }
    out.text.replace("\r\n", "\n");
    return out;
}

QByteArray encode(const Text& text)
{
    auto body = text.text;
    if (text.crlf) {
        body.replace("\n", "\r\n");
    }
    QStringEncoder encoder(text.encoding, text.bom ? QStringConverter::Flag::WriteBom : QStringConverter::Flag::Default);
    return encoder(body);
}

Document Document::fromBytes(const QByteArray& data)
{
    Document document;
    document.m_format = decode(data);
    if (!document.m_format.text.isEmpty()) {
        document.m_lines = document.m_format.text.split('\n');
        if (document.m_lines.last().isEmpty()) {
            document.m_lines.removeLast();  // the line end of the last line
        }
    }
    document.m_format.text.clear();
    return document;
}

QByteArray Document::toBytes() const
{
    auto text = m_format;
    text.text = m_lines.isEmpty() ? QString() : m_lines.join('\n') + '\n';
    return encode(text);
}

namespace {

/// The lines of @p section: from just after its header to the next header; -1, -1 when the file has no such section
std::pair<qsizetype, qsizetype> sectionRange(const QStringList& lines, const QString& section)
{
    for (qsizetype i = 0; i < lines.size(); i++) {
        const auto line = parse(lines[i]);
        if (line.kind == Line::Kind::Section && line.section.compare(section, Qt::CaseInsensitive) == 0) {
            auto end = i + 1;
            while (end < lines.size() && parse(lines[end]).kind != Line::Kind::Section) {
                end++;
            }
            return { i + 1, end };
        }
    }
    return { -1, -1 };
}

}  // namespace

std::optional<QString> Document::value(const QString& section, const QString& key) const
{
    const auto [begin, end] = sectionRange(m_lines, section);
    for (auto i = begin; i >= 0 && i < end; i++) {
        const auto line = parse(m_lines[i]);
        if (line.kind == Line::Kind::Setting && line.key.compare(key, Qt::CaseInsensitive) == 0) {
            return line.value;
        }
    }
    return std::nullopt;
}

void Document::setValue(const QString& section, const QString& key, const QString& value)
{
    const auto [begin, end] = sectionRange(m_lines, section);
    if (begin < 0) {
        if (!m_lines.isEmpty() && !m_lines.last().trimmed().isEmpty()) {
            m_lines << QString();
        }
        m_lines << QString("[%1]").arg(section) << QString("%1=%2").arg(key, value);
        m_modified = true;
        return;
    }
    auto lastSetting = begin - 1;  // the header, if the section has no settings
    for (auto i = begin; i < end; i++) {
        const auto line = parse(m_lines[i]);
        if (line.kind != Line::Kind::Setting) {
            continue;
        }
        if (line.key.compare(key, Qt::CaseInsensitive) == 0) {
            if (line.value != value) {
                m_lines[i] = withValue(m_lines[i], value);
                m_modified = true;
            }
            return;
        }
        lastSetting = i;
    }
    // after the section's last setting: comments and blank lines that lead into the next section stay with it
    m_lines.insert(lastSetting + 1, QString("%1=%2").arg(key, value));
    m_modified = true;
}

Result<> save(GameInstance* instance, const QString& path, const QByteArray& data)
{
    QString staged;
    const auto rel = QDir(instance->gameRoot()).relativeFilePath(path);
    if (!rel.startsWith("..") && !QDir::isAbsolutePath(rel)) {
        if (auto manifest = DeploymentManifest::load(instance->deploymentManifestPath())) {
            if (const auto owner = manifest->files.value(rel.toLower()).mod; !owner.isEmpty()) {
                staged = FS::PathCombine(instance->modList()->modDir(owner), rel);
                if (!QFileInfo::exists(staged)) {
                    staged.clear();
                }
            }
        }
    }
    const auto target = staged.isEmpty() ? path : staged;
    if (!FS::ensureFilePathExists(target)) {
        return std::unexpected(QObject::tr("Could not create the folder for %1.").arg(QDir::toNativeSeparators(target)));
    }
    // QSaveFile writes a new file and renames it over the old one: a hard link is replaced, never written through
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
        return std::unexpected(QObject::tr("Could not save %1: %2").arg(QDir::toNativeSeparators(target), out.errorString()));
    }
    if (!staged.isEmpty()) {
        TRY(GameFiles::removeFile(path));
        TRY(GameFiles::placeFile(staged, path, GameFiles::PlaceMode::Link).transform([](auto) {}));
        // the file in the game folder is a new one: deployment must still know it as the mod's
        TRY_INTO(auto manifest, DeploymentManifest::load(instance->deploymentManifestPath()));
        manifest.files[rel.toLower()].fileId = GameFiles::fileIndex(path);
        TRY(manifest.save(instance->deploymentManifestPath()));
    }
    return {};
}

}  // namespace Ini
