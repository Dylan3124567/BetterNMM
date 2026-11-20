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

#include "FomodPatches.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QObject>
#include <QUuid>
#include <QtConcurrent/QtConcurrentMap>
#include <algorithm>
#include <atomic>
#include <climits>
#include <tuple>

#include <archive.h>

#include "FileSystem.h"
#include "archive/ArchiveReader.h"
#include "archive/ArchiveWriter.h"
#include "game/GameFiles.h"

namespace FomodPatches {
namespace {

struct Entry {
    QString path;  ///< '/' separated
    qint64 size = -1;
};

/// A mod's archive, read for its installer
struct Source {
    QHash<QString, Entry> entries;  ///< by lower-case path
    QString root;                   ///< lower-case folder holding "fomod"; empty at the top
    Fomod::Module module;
};

/// An archive file and where it goes, relative to the mod's folder
struct Placement {
    QString key;  ///< Source::entries key
    QString destination;
};

QString normal(QString path)
{
    path.replace('\\', '/');
    while (path.startsWith("./")) {
        path.remove(0, 2);
    }
    while (path.startsWith('/')) {
        path.remove(0, 1);
    }
    return path;
}

bool isPlugin(const QString& path)
{
    return path.endsWith(".esp", Qt::CaseInsensitive) || path.endsWith(".esm", Qt::CaseInsensitive) || path.endsWith(".esl", Qt::CaseInsensitive);
}

/// What an archive holds for the check: its files and its installer configuration
struct Listing {
    QHash<QString, Entry> entries;  ///< by lower-case path
    QString config;                 ///< lower-case path of the installer configuration; empty when it has none
    QByteArray xml;
};

/**
 * The cache file of an archive: where 7-Zip put the installer configuration last, reading it unpacks the whole archive
 * (half a minute for a gigabyte). An archive with another size or time is another file.
 */
QString cachePath(const QString& cacheDir, const QString& archive)
{
    if (cacheDir.isEmpty()) {
        return {};
    }
    const QFileInfo info(archive);
    const auto id = QString("%1|%2|%3").arg(QDir::cleanPath(info.absoluteFilePath()).toLower()).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
    return FS::PathCombine(cacheDir, QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha1).toHex() + ".json");
}

std::optional<Listing> readCache(const QString& path)
{
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const auto obj = QJsonDocument::fromJson(file.readAll()).object();
    if (!obj.contains("entries")) {
        return std::nullopt;
    }
    Listing listing;
    for (const auto& value : obj.value("entries").toArray()) {
        const auto entry = value.toArray();
        const auto entryPath = entry.at(0).toString();
        listing.entries.insert(entryPath.toLower(), Entry{ entryPath, entry.at(1).toInteger(-1) });
    }
    listing.config = obj.value("config").toString();
    listing.xml = QByteArray::fromBase64(obj.value("xml").toString().toLatin1());
    return listing;
}

void writeCache(const QString& path, const Listing& listing)
{
    if (path.isEmpty()) {
        return;
    }
    QJsonArray entries;
    // an archive without an installer is never asked for its files
    if (!listing.config.isEmpty()) {
        for (const auto& entry : listing.entries) {
            entries.append(QJsonArray{ entry.path, entry.size });
        }
    }
    const QJsonObject obj{ { "entries", entries }, { "config", listing.config }, { "xml", QString::fromLatin1(listing.xml.toBase64()) } };
    if (!FS::ensureFilePathExists(path)) {
        qWarning() << "Could not create the installer cache folder for" << path;
        return;
    }
    if (auto written = FS::write(path, QJsonDocument(obj).toJson(QJsonDocument::Compact)); !written) {
        qWarning() << "Could not cache an archive's installer:" << written.error();
    }
}

/**
 * A 7z skips entries without unpacking them only until something is read; after that, every entry skipped in the same
 * solid block is unpacked, and that block can be gigabytes long. Other archives skip the same either way.
 */
bool isSevenZip(MMCZip::ArchiveReader::File* f)
{
    return (f->format() & ARCHIVE_FORMAT_BASE_MASK) == ARCHIVE_FORMAT_7ZIP;
}

/// Adds the file to @p listing; true when it is the installer configuration nearest the top so far, the one installing
/// picked
bool addEntry(Listing& listing, MMCZip::ArchiveReader::File* f)
{
    const auto path = normal(f->filename());
    const auto key = path.toLower();
    listing.entries.insert(key, Entry{ path, f->size() });
    if ((key == "fomod/moduleconfig.xml" || key.endsWith("/fomod/moduleconfig.xml")) &&
        (listing.config.isEmpty() || key.size() < listing.config.size())) {
        listing.config = key;
        return true;
    }
    return false;
}

/// The archive's files and its XML installer, or the cache of them. A 7z is listed first and its installer read in a
/// second pass that stops there, so nothing after the installer is unpacked.
Result<Listing> list(const QString& archive, const QString& cacheDir)
{
    const auto cached = cachePath(cacheDir, archive);
    if (auto listing = readCache(cached)) {
        return *listing;
    }
    Listing listing;
    bool sevenZip = false;
    TRY(MMCZip::ArchiveReader(archive).parse([&](MMCZip::ArchiveReader::File* f) -> Result<> {
        sevenZip = isSevenZip(f);
        if (f->isFile() && addEntry(listing, f) && !sevenZip) {
            TRY_INTO(listing.xml, f->readAll());
            return {};
        }
        return f->skip();
    }));
    if (sevenZip && !listing.config.isEmpty()) {
        TRY(MMCZip::ArchiveReader(archive).parse([&](MMCZip::ArchiveReader::File* f) -> Result<bool> {
            if (f->isFile() && normal(f->filename()).toLower() == listing.config) {
                TRY_INTO(listing.xml, f->readAll());
                return true;
            }
            TRY(f->skip());
            return false;
        }));
    }
    writeCache(cached, listing);
    return listing;
}

/// The archive's installer; nullopt when it has no XML installer
Result<std::optional<Source>> load(const QString& archive, const QString& cacheDir)
{
    TRY_INTO(auto listing, list(archive, cacheDir));
    if (listing.config.isEmpty()) {
        return std::nullopt;
    }
    Source source;
    TRY_INTO(source.module, Fomod::parse(listing.xml));
    source.entries = std::move(listing.entries);
    source.root = listing.config.section('/', 0, -3);
    return source;
}

const Fomod::Plugin& pluginOf(const Fomod::Module& module, const Fomod::Selection& selection)
{
    return module.steps[selection.step].groups[selection.group].plugins[selection.plugin];
}

bool sameOption(const Fomod::Selection& a, const Fomod::Selection& b)
{
    return a.step == b.step && a.group == b.group && a.plugin == b.plugin;
}

bool picked(const QList<Fomod::Selection>& selections, const Fomod::Selection& option)
{
    return std::any_of(selections.begin(), selections.end(), [&](const Fomod::Selection& s) { return sameOption(s, option); });
}

void sortSelections(QList<Fomod::Selection>& selections)
{
    std::sort(selections.begin(), selections.end(), [](const Fomod::Selection& a, const Fomod::Selection& b) {
        return std::tie(a.step, a.group, a.plugin) < std::tie(b.step, b.group, b.plugin);
    });
}

/// The flags the options set, in installer order, counting the steps before @p beforeStep
QHash<QString, QString> flagsOf(const Fomod::Module& module, const QList<Fomod::Selection>& sorted, int beforeStep = INT_MAX)
{
    QHash<QString, QString> flags;
    for (const auto& selection : sorted) {
        if (selection.step >= beforeStep) {
            break;
        }
        for (const auto& [name, value] : pluginOf(module, selection).flags) {
            flags.insert(name, value);
        }
    }
    return flags;
}

QList<Placement> expand(const Source& source, const Fomod::FileItem& item)
{
    QList<Placement> out;
    const auto relative = normal(item.source).toLower();
    const auto key = source.root.isEmpty() ? relative : (relative.isEmpty() ? source.root : source.root + '/' + relative);
    if (!item.isFolder) {
        if (source.entries.contains(key)) {
            out.append({ key, item.destination.isEmpty() ? QFileInfo(item.source).fileName() : item.destination });
        }
        return out;
    }
    const auto prefix = key.isEmpty() ? key : key + '/';
    for (auto it = source.entries.cbegin(); it != source.entries.cend(); ++it) {
        if (it.key().startsWith(prefix)) {
            const auto inside = it->path.mid(prefix.size());
            out.append({ it.key(), item.destination.isEmpty() ? inside : item.destination + '/' + inside });
        }
    }
    return out;
}

QList<Placement> expand(const Source& source, const QList<Fomod::FileItem>& items)
{
    QList<Placement> out;
    for (const auto& item : items) {
        out << expand(source, item);
    }
    return out;
}

/// Whether the mod's folder has this file as the archive has it
bool present(const QString& folder, const Source& source, const Placement& placement)
{
    const QFileInfo file(FS::PathCombine(folder, placement.destination));
    const auto size = source.entries.value(placement.key).size;
    return file.isFile() && (size < 0 || file.size() == size);
}

bool allPresent(const QString& folder, const Source& source, const QList<Placement>& placements)
{
    return !placements.isEmpty() && std::all_of(placements.begin(), placements.end(), [&](const Placement& p) { return present(folder, source, p); });
}

QList<Fomod::Selection> fromNames(const Fomod::Module& module, const QList<QStringList>& names)
{
    QList<Fomod::Selection> out;
    for (const auto& name : names) {
        if (name.size() != 3) {
            continue;
        }
        bool found = false;
        for (int s = 0; s < module.steps.size() && !found; s++) {
            if (module.steps[s].name != name[0]) {
                continue;
            }
            for (int g = 0; g < module.steps[s].groups.size() && !found; g++) {
                const auto& group = module.steps[s].groups[g];
                if (group.name != name[1]) {
                    continue;
                }
                for (int p = 0; p < group.plugins.size() && !found; p++) {
                    if (group.plugins[p].name == name[2]) {
                        out << Fomod::Selection{ s, g, p };
                        found = true;
                    }
                }
            }
        }
    }
    return out;
}

/// The options a mod was installed with: an option whose files are all in the mod's folder as the archive has them, and
/// an option that only sets flags when files the installer adds because of those flags are there
QList<Fomod::Selection> inferred(const Source& source, const QString& folder, const FileStates& fileState)
{
    const auto& module = source.module;
    QList<Fomod::Selection> out;
    QList<Fomod::Selection> flagsOnly;
    for (int s = 0; s < module.steps.size(); s++) {
        for (int g = 0; g < module.steps[s].groups.size(); g++) {
            for (int p = 0; p < module.steps[s].groups[g].plugins.size(); p++) {
                const auto& plugin = module.steps[s].groups[g].plugins[p];
                if (!plugin.files.isEmpty()) {
                    if (allPresent(folder, source, expand(source, plugin.files))) {
                        out << Fomod::Selection{ s, g, p };
                    }
                } else if (!plugin.flags.isEmpty()) {
                    flagsOnly << Fomod::Selection{ s, g, p };
                }
            }
        }
    }
    sortSelections(out);
    Fomod::Context before{ fileState, flagsOf(module, out) };
    for (const auto& option : flagsOnly) {
        Fomod::Context with = before;
        for (const auto& [name, value] : pluginOf(module, option).flags) {
            with.flags.insert(name, value);
        }
        const bool marked = std::any_of(module.conditionalInstalls.begin(), module.conditionalInstalls.end(), [&](const Fomod::ConditionalInstall& c) {
            return !Fomod::evaluate(c.condition, before) && Fomod::evaluate(c.condition, with) && allPresent(folder, source, expand(source, c.files));
        });
        if (marked) {
            out << option;
            before = with;
        }
    }
    sortSelections(out);
    return out;
}

QList<Fomod::Selection> selectionsOf(const Mod& mod, const Source& source, const FileStates& fileState)
{
    auto out = mod.choices.isEmpty() ? inferred(source, mod.folder, fileState) : fromNames(source.module, mod.choices);
    sortSelections(out);
    return out;
}

/// Plugin names among the destinations, or else the first destinations' top folders and files
QStringList describe(const QList<Placement>& placements)
{
    QStringList plugins;
    QStringList tops;
    for (const auto& placement : placements) {
        const auto name = placement.destination.section('/', -1);
        if (isPlugin(name) && !plugins.contains(name, Qt::CaseInsensitive)) {
            plugins << name;
        }
        const auto top = placement.destination.section('/', 0, 0);
        if (!tops.contains(top, Qt::CaseInsensitive)) {
            tops << top;
        }
    }
    return plugins.isEmpty() ? tops.mid(0, 3) : plugins;
}

bool recommended(Fomod::PluginType type)
{
    return type == Fomod::PluginType::Recommended || type == Fomod::PluginType::Required;
}

void checkMod(const Mod& mod, const Source& source, const FileStates& fileState, QList<Patch>& out)
{
    const auto& module = source.module;
    const auto selections = selectionsOf(mod, source, fileState);
    // the same installer without any other mod: what it offers or recommends only because of plugins the game has differs
    const FileStates alone = [](const QString&) { return Fomod::FileState::Missing; };
    for (int s = 0; s < module.steps.size(); s++) {
        const auto& step = module.steps[s];
        const Fomod::Context now{ fileState, flagsOf(module, selections, s) };
        const Fomod::Context bare{ alone, now.flags };
        if (!Fomod::stepVisible(step, now)) {
            continue;
        }
        const bool shownBare = Fomod::stepVisible(step, bare);
        for (int g = 0; g < step.groups.size(); g++) {
            const auto& group = step.groups[g];
            for (int p = 0; p < group.plugins.size(); p++) {
                const Fomod::Selection option{ s, g, p };
                const auto& plugin = group.plugins[p];
                if (picked(selections, option) || !recommended(Fomod::pluginType(plugin, now)) ||
                    (shownBare && recommended(Fomod::pluginType(plugin, bare)))) {
                    continue;
                }
                const auto placements = expand(source, plugin.files);
                if (placements.isEmpty() && plugin.flags.isEmpty()) {
                    continue;
                }
                Patch patch{ .modId = mod.id, .modName = mod.name, .title = plugin.name, .files = describe(placements), .option = option };
                if (group.type == Fomod::GroupType::SelectExactlyOne || group.type == Fomod::GroupType::SelectAtMostOne) {
                    for (const auto& other : selections) {
                        if (other.step == s && other.group == g) {
                            patch.replaces << pluginOf(module, other).name;
                        }
                    }
                }
                out << patch;
            }
        }
    }
    const Fomod::Context now{ fileState, flagsOf(module, selections) };
    const Fomod::Context bare{ alone, now.flags };
    for (int i = 0; i < module.conditionalInstalls.size(); i++) {
        const auto& conditional = module.conditionalInstalls[i];
        if (!Fomod::evaluate(conditional.condition, now) || Fomod::evaluate(conditional.condition, bare)) {
            continue;
        }
        const auto placements = expand(source, conditional.files);
        if (placements.isEmpty() || allPresent(mod.folder, source, placements)) {
            continue;
        }
        out << Patch{ .modId = mod.id, .modName = mod.name, .files = describe(placements), .conditional = i };
    }
}

/// @p destination inside @p folder, or empty when it points outside it
QString inside(const QString& folder, const QString& destination)
{
    const auto root = QDir::cleanPath(folder);
    const auto path = QDir::cleanPath(FS::PathCombine(folder, destination));
    return path.startsWith(root + '/', Qt::CaseInsensitive) ? path : QString();
}

}  // namespace

QList<QStringList> choiceNames(const Fomod::Module& module, const QList<Fomod::Selection>& selections, const Fomod::Context& base)
{
    auto sorted = selections;
    sortSelections(sorted);
    QList<QStringList> out;
    Fomod::Context context = base;
    for (int s = 0; s < module.steps.size(); s++) {
        if (!Fomod::stepVisible(module.steps[s], context)) {
            continue;
        }
        for (const auto& selection : sorted) {
            if (selection.step != s) {
                continue;
            }
            const auto& group = module.steps[s].groups[selection.group];
            out << QStringList{ module.steps[s].name, group.name, group.plugins[selection.plugin].name };
            for (const auto& [name, value] : group.plugins[selection.plugin].flags) {
                context.flags.insert(name, value);
            }
        }
    }
    return out;
}

Check find(const QList<Mod>& mods, const FileStates& fileState, const QString& cacheDir, const std::function<void(int, int)>& progress)
{
    struct Outcome {
        QList<Patch> patches;
        QString unchecked;
        QString error;
    };
    std::atomic<int> done = 0;
    const auto total = static_cast<int>(mods.size());
    // archives unpack on several cores at once; a big one would otherwise hold up all the others
    const auto outcomes = QtConcurrent::blockingMapped<QList<Outcome>>(mods, [&](const Mod& mod) {
        Outcome outcome;
        if (mod.archive.isEmpty() || !QFileInfo(mod.archive).isFile()) {
            outcome.unchecked = mod.name;
        } else if (auto source = load(mod.archive, cacheDir); !source) {
            outcome.error = QObject::tr("%1: %2").arg(mod.name, source.error());
        } else if (*source) {
            checkMod(mod, **source, fileState, outcome.patches);
        }
        if (progress) {
            progress(++done, total);
        }
        return outcome;
    });
    Check check;
    for (const auto& outcome : outcomes) {
        check.patches << outcome.patches;
        if (!outcome.unchecked.isEmpty()) {
            check.unchecked << outcome.unchecked;
        }
        if (!outcome.error.isEmpty()) {
            check.errors << outcome.error;
        }
    }
    return check;
}

void remember(const QString& archive, const QString& extractedDir, const QString& cacheDir)
{
    const auto cached = cachePath(cacheDir, archive);
    if (cached.isEmpty() || QFileInfo::exists(cached)) {
        return;
    }
    Listing listing;
    bool sevenZip = false;
    const auto listed = MMCZip::ArchiveReader(archive).parse([&](MMCZip::ArchiveReader::File* f) -> Result<bool> {
        sevenZip = isSevenZip(f);
        if (!sevenZip) {
            return true;
        }
        if (f->isFile()) {
            addEntry(listing, f);
        }
        TRY(f->skip());
        return false;
    });
    if (!listed || !sevenZip) {
        return;
    }
    if (!listing.config.isEmpty()) {
        QFile config(FS::PathCombine(extractedDir, listing.entries.value(listing.config).path));
        if (!config.open(QIODevice::ReadOnly)) {
            return;  // the check reads it from the archive then
        }
        listing.xml = config.readAll();
    }
    writeCache(cached, listing);
}

Result<Installed> install(const Mod& mod, const QList<Patch>& patches, const FileStates& fileState, const QString& cacheDir)
{
    TRY_INTO(const auto loaded, load(mod.archive, cacheDir));
    if (!loaded) {
        return std::unexpected(QObject::tr("%1 has no installer.").arg(QFileInfo(mod.archive).fileName()));
    }
    const auto& source = *loaded;
    const auto& module = source.module;
    const auto before = selectionsOf(mod, source, fileState);

    auto after = before;
    QList<Fomod::FileItem> added;
    QList<Fomod::FileItem> dropped;
    for (const auto& patch : patches) {
        if (!patch.option) {
            continue;
        }
        const auto& option = *patch.option;
        const auto type = module.steps[option.step].groups[option.group].type;
        if (type == Fomod::GroupType::SelectExactlyOne || type == Fomod::GroupType::SelectAtMostOne) {
            for (auto it = after.begin(); it != after.end();) {
                if (it->step == option.step && it->group == option.group && !sameOption(*it, option)) {
                    dropped << pluginOf(module, *it).files;
                    it = after.erase(it);
                } else {
                    ++it;
                }
            }
        }
        if (!picked(after, option)) {
            after << option;
        }
        added << pluginOf(module, option).files;
    }
    sortSelections(after);

    const Fomod::Context was{ fileState, flagsOf(module, before) };
    const Fomod::Context now{ fileState, flagsOf(module, after) };
    QList<Fomod::FileItem> all = module.requiredFiles;
    for (const auto& selection : after) {
        all << pluginOf(module, selection).files;
    }
    for (int i = 0; i < module.conditionalInstalls.size(); i++) {
        const auto& conditional = module.conditionalInstalls[i];
        const bool on = Fomod::evaluate(conditional.condition, now);
        const bool chosen = std::any_of(patches.begin(), patches.end(), [i](const Patch& p) { return p.conditional == i; });
        if (on) {
            all << conditional.files;
            if (chosen || !Fomod::evaluate(conditional.condition, was)) {
                added << conditional.files;
            }
        } else if (Fomod::evaluate(conditional.condition, was)) {
            dropped << conditional.files;
        }
    }
    // where each file comes from with the new options: of two items with the same destination the later one (higher
    // priority) wins, as when installing
    std::stable_sort(all.begin(), all.end(), [](const Fomod::FileItem& a, const Fomod::FileItem& b) { return a.priority < b.priority; });
    QHash<QString, QString> winner;
    for (const auto& placement : expand(source, all)) {
        winner.insert(placement.destination.toLower(), placement.key);
    }

    QHash<QString, Placement> copies;  // by lower-case destination
    for (const auto& placement : expand(source, added)) {
        const auto destination = placement.destination.toLower();
        copies.insert(destination, { winner.value(destination, placement.key), placement.destination });
    }
    QStringList removals;
    for (const auto& placement : expand(source, dropped)) {
        // a file changed since it was installed is the user's now
        if (!winner.contains(placement.destination.toLower()) && present(mod.folder, source, placement)) {
            removals << placement.destination;
        }
    }
    for (const auto& destination : QStringList(copies.keys()) + removals) {
        if (inside(mod.folder, destination).isEmpty()) {
            return std::unexpected(QObject::tr("The installer tried to write outside the mod: %1").arg(destination));
        }
    }

    // unpack next to each destination first, so a failed or damaged archive leaves every file as it was
    QMultiHash<QString, QString> wanted;  // archive key -> absolute destination
    for (const auto& copy : copies) {
        wanted.insert(copy.key, inside(mod.folder, copy.destination));
    }
    const auto suffix = ".part-" + QUuid::createUuid().toString(QUuid::Id128).left(8);
    QStringList parts;
    auto writer = MMCZip::ArchiveWriter::createDiskWriter();
    auto pending = QSet<QString>(wanted.keyBegin(), wanted.keyEnd());
    const auto unpacked = MMCZip::ArchiveReader(mod.archive).parse([&](MMCZip::ArchiveReader::File* f) -> Result<bool> {
        const auto key = f->isFile() ? normal(f->filename()).toLower() : QString();
        if (!pending.remove(key)) {
            TRY(f->skip());
            return false;
        }
        const auto targets = wanted.values(key);
        const auto first = targets.first() + suffix;
        FS::ensureFilePathExists(first);
        parts << first;
        TRY(f->writeFile(writer.get(), first, QDir(mod.folder)));
        for (const auto& other : targets.mid(1)) {
            FS::ensureFilePathExists(other + suffix);
            parts << other + suffix;
            if (!QFile::copy(first, other + suffix)) {
                return std::unexpected(QObject::tr("Could not copy %1 to %2").arg(first, other + suffix));
            }
        }
        // the rest of a solid archive would only be unpacked to be skipped
        return pending.isEmpty();
    });
    const auto discard = [&parts] {
        for (const auto& part : parts) {
            QFile::remove(part);
        }
    };
    if (!unpacked) {
        discard();
        return std::unexpected(unpacked.error());
    }
    if (QSet<QString>(parts.begin(), parts.end()).size() != copies.size()) {
        discard();
        return std::unexpected(QObject::tr("%1 is missing files its installer names.").arg(QFileInfo(mod.archive).fileName()));
    }

    Installed result;
    for (const auto& copy : copies) {
        const auto target = inside(mod.folder, copy.destination);
        if (QFileInfo::exists(target)) {
            if (auto removed = GameFiles::removeFile(target); !removed) {
                discard();
                return std::unexpected(removed.error());
            }
        }
        if (!QFile::rename(target + suffix, target)) {
            discard();
            return std::unexpected(QObject::tr("Could not put %1 in place.").arg(target));
        }
        parts.removeOne(target + suffix);
        if (isPlugin(target)) {
            result.plugins << QFileInfo(target).fileName().toLower();
        }
    }
    for (const auto& removal : removals) {
        if (const auto target = inside(mod.folder, removal); QFileInfo::exists(target)) {
            TRY(GameFiles::removeFile(target));
        }
    }
    result.choices.reserve(after.size());
    for (const auto& selection : after) {
        const auto& group = module.steps[selection.step].groups[selection.group];
        result.choices << QStringList{ module.steps[selection.step].name, group.name, group.plugins[selection.plugin].name };
    }
    return result;
}

}  // namespace FomodPatches
