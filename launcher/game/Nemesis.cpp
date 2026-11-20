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

#include "Nemesis.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>

#include "FileSystem.h"

namespace Nemesis {
namespace {

QString engineDir(const QString& dataDir)
{
    return FS::PathCombine(dataDir, "Nemesis_Engine");
}

QString cacheFile(const QString& dataDir, const QString& name)
{
    return FS::PathCombine(engineDir(dataDir), "cache", name);
}

/// The lines of a text file as Nemesis reads them (line ends dropped, nothing trimmed), empty ones left out
QStringList lines(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QStringList out;
    for (auto line : QString::fromUtf8(file.readAll()).split('\n')) {
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (!line.isEmpty()) {
            out << line;
        }
    }
    return out;
}

/// Writes over the file in place, so a file deployment linked there stays the same file as its mod's
Result<> writeLines(const QString& path, const QStringList& content, const QString& lineEnd)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return std::unexpected(QObject::tr("Could not write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
    }
    file.write((content.join(lineEnd) + lineEnd).toUtf8());
    return {};
}

struct PatchInfo {
    QString name;
    QString site;
    bool hidden = false;
};

/// A patch's info.ini read the way Nemesis reads it: the list shows "<name> (<site>)", and that text is what its
/// "mod settings" file remembers ticked
PatchInfo patchInfo(const QString& folder)
{
    PatchInfo info;
    for (const auto& line : lines(FS::PathCombine(folder, "info.ini"))) {
        const auto value = line.mid(line.indexOf('=') + 1);
        if (line.startsWith("name=", Qt::CaseInsensitive)) {
            info.name = value;
        } else if (line.startsWith("site=", Qt::CaseInsensitive)) {
            info.site = value;
        } else if (line.startsWith("hidden=", Qt::CaseInsensitive)) {
            info.hidden = value.trimmed().compare("true", Qt::CaseInsensitive) == 0;
        }
    }
    return info;
}

/// What the engine update depends on: the patches, the program itself, and where it is (it records absolute paths)
QString fingerprint(const QString& dataDir)
{
    auto codes = patchCodes(FS::PathCombine(engineDir(dataDir), "mod")).values();
    codes.sort();
    const QFileInfo program(FS::PathCombine(dataDir, k_engine));
    return QString("%1|%2|%3|%4")
        .arg(codes.join(','))
        .arg(program.size())
        .arg(program.lastModified().toMSecsSinceEpoch())
        .arg(QDir(dataDir).absolutePath().toLower());
}

}  // namespace

QSet<QString> patchCodes(const QString& modFolder)
{
    QSet<QString> out;
    for (const auto& entry : QDir(modFolder).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (QFileInfo::exists(FS::PathCombine(entry.absoluteFilePath(), "info.ini"))) {
            out << entry.fileName().toLower();
        }
    }
    return out;
}

Result<Setup> prepare(const QString& dataDir, const QSet<QString>& ownPatches)
{
    const auto engine = engineDir(dataDir);

    // left at "auto", Nemesis takes the game installation the registry names: the Steam one, not this instance's
    const auto ini = FS::PathCombine(engine, "nemesis.ini");
    auto settings = lines(ini);
    const auto data = "SkyrimDataDirectory=" + QDir::toNativeSeparators(QDir(dataDir).absolutePath()) + '\\';
    bool found = false;
    for (auto& line : settings) {
        if (line.startsWith("SkyrimDataDirectory=", Qt::CaseInsensitive)) {
            line = data;
            found = true;
        }
    }
    if (!found) {
        settings.prepend(data);
    }
    TRY(writeLines(ini, settings, "\r\n"));

    if (!QDir().mkpath(FS::PathCombine(engine, "cache"))) {
        return std::unexpected(QObject::tr("Could not create %1.").arg(QDir::toNativeSeparators(FS::PathCombine(engine, "cache"))));
    }
    if (!QFileInfo::exists(cacheFile(dataDir, "language setting"))) {
        TRY(writeLines(cacheFile(dataDir, "language setting"), { "english" }, "\n"));
    }

    Setup setup;
    auto ticked = lines(cacheFile(dataDir, "mod settings"));
    auto handled = lines(cacheFile(dataDir, "betternmm ticked"));
    auto codes = patchCodes(FS::PathCombine(engine, "mod")).values();
    codes.sort();
    bool changed = false;
    for (const auto& code : codes) {
        if (ownPatches.contains(code) || handled.contains(code, Qt::CaseInsensitive)) {
            continue;
        }
        handled << code;
        changed = true;
        const auto info = patchInfo(FS::PathCombine(engine, "mod", code));
        // hidden patches are always used; one without a name or site is one Nemesis refuses anyway
        if (info.hidden || info.name.isEmpty() || info.site.isEmpty()) {
            continue;
        }
        if (const auto shown = QString("%1 (%2)").arg(info.name, info.site); !ticked.contains(shown)) {
            ticked << shown;
            setup.ticked << info.name;
        }
    }
    // a missing list makes Nemesis warn that it could not read it
    if (changed || !QFileInfo::exists(cacheFile(dataDir, "mod settings"))) {
        TRY(writeLines(cacheFile(dataDir, "mod settings"), ticked, "\n"));
        TRY(writeLines(cacheFile(dataDir, "betternmm ticked"), handled, "\n"));
    }

    setup.engineOutdated =
        !QFileInfo::exists(cacheFile(dataDir, "engine_update")) || lines(cacheFile(dataDir, "betternmm engine")).value(0) != fingerprint(dataDir);
    return setup;
}

Result<> engineUpdated(const QString& dataDir)
{
    return writeLines(cacheFile(dataDir, "betternmm engine"), { fingerprint(dataDir) }, "\n");
}

}  // namespace Nemesis
