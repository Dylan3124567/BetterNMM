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

// The plugin rules follow PluginManager::ScanPlugins/CheckPluginCompatibility of each SKSE64, F4SE and SFSE release
// (github.com/ianpatt): every game version is checked with the rules of the newest release made for it.

#include "VersionCheck.h"

#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QObject>
#include <QRegularExpression>

#include <cstring>

#include "FileSystem.h"
#include "game/GameDefinition.h"
#include "game/GameFiles.h"

namespace VersionCheck {

quint32 packVersion(int major, int minor, int build, int store)
{
    return ((static_cast<quint32>(major) & 0xFF) << 24) | ((static_cast<quint32>(minor) & 0xFF) << 16) |
           ((static_cast<quint32>(build) & 0xFFF) << 4) | (static_cast<quint32>(store) & 0xF);
}

QString versionText(quint32 packed)
{
    auto text = QStringLiteral("%1.%2.%3").arg(packed >> 24).arg((packed >> 16) & 0xFF).arg((packed >> 4) & 0xFFF);
    switch (packed & 0xF) {
        case 1: return text + QObject::tr(" (GOG)");
        case 2: return text + QObject::tr(" (Epic)");
        default: return text;
    }
}

namespace {

quint32 u32(const uchar* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<quint32>(p[3]) << 24);
}

quint16 u16(const uchar* p)
{
    return static_cast<quint16>(p[0] | (p[1] << 8));
}

/// What a DLL says about itself without running it: its machine, link time and named exports
struct Dll {
    bool valid = false;
    quint16 machine = 0;
    quint32 buildTime = 0;
    QHash<QByteArray, QByteArray> exports;  ///< export name -> the bytes it points at (up to the size asked for)
};

Dll readDll(const QString& path, const QList<QByteArray>& wanted, qsizetype dataSize)
{
    Dll dll;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return dll;
    }
    const qint64 size = file.size();
    const uchar* data = size > 0 ? file.map(0, size) : nullptr;
    QByteArray buffer;
    if (!data && size > 0) {
        buffer = file.readAll();
        data = reinterpret_cast<const uchar*>(buffer.constData());
    }
    auto fits = [size](qint64 at, qint64 count) { return at >= 0 && count >= 0 && at + count <= size; };
    if (!fits(0, 0x40) || data[0] != 'M' || data[1] != 'Z') {
        return dll;
    }
    const qint64 pe = u32(data + 0x3C);
    if (!fits(pe, 24) || std::memcmp(data + pe, "PE\0\0", 4) != 0) {
        return dll;
    }
    const uchar* coff = data + pe + 4;
    dll.machine = u16(coff);
    const int sectionCount = u16(coff + 2);
    dll.buildTime = u32(coff + 4);
    const qint64 optional = pe + 24;
    const int optionalSize = u16(coff + 16);
    if (!fits(optional, optionalSize) || optionalSize < 2) {
        return dll;
    }
    dll.valid = true;
    const bool pe32plus = u16(data + optional) == 0x20b;
    const qint64 directories = optional + (pe32plus ? 112 : 96);
    const qint64 directoryCountAt = optional + (pe32plus ? 108 : 92);
    if (!fits(directoryCountAt, 4) || u32(data + directoryCountAt) == 0 || !fits(directories, 8)) {
        return dll;
    }
    const quint32 exportRva = u32(data + directories);
    const qint64 sections = optional + optionalSize;
    auto toOffset = [&](quint32 rva) -> qint64 {
        for (int i = 0; i < sectionCount; i++) {
            const qint64 at = sections + 40LL * i;
            if (!fits(at, 40)) {
                return -1;
            }
            const quint32 virtualSize = u32(data + at + 8);
            const quint32 virtualAddress = u32(data + at + 12);
            const quint32 rawSize = u32(data + at + 16);
            const quint32 rawOffset = u32(data + at + 20);
            if (rva >= virtualAddress && rva < virtualAddress + qMax(virtualSize, rawSize)) {
                return static_cast<qint64>(rawOffset) + (rva - virtualAddress);
            }
        }
        return -1;
    };
    const qint64 exportDir = exportRva ? toOffset(exportRva) : -1;
    if (!fits(exportDir, 40)) {
        return dll;
    }
    const quint32 nameCount = u32(data + exportDir + 24);
    const qint64 functions = toOffset(u32(data + exportDir + 28));
    const qint64 names = toOffset(u32(data + exportDir + 32));
    const qint64 ordinals = toOffset(u32(data + exportDir + 36));
    for (quint32 i = 0; i < nameCount && fits(names + 4LL * i, 4) && fits(ordinals + 2LL * i, 2); i++) {
        const qint64 nameAt = toOffset(u32(data + names + 4LL * i));
        if (!fits(nameAt, 1)) {
            continue;
        }
        const auto name = QByteArray(reinterpret_cast<const char*>(data + nameAt), qMin<qint64>(256, size - nameAt)).split('\0').first();
        if (!wanted.contains(name)) {
            continue;
        }
        const quint16 ordinal = u16(data + ordinals + 2LL * i);
        if (!fits(functions + 4LL * ordinal, 4)) {
            continue;
        }
        const qint64 target = toOffset(u32(data + functions + 4LL * ordinal));
        QByteArray bytes(dataSize, '\0');
        if (target >= 0 && target < size) {
            std::memcpy(bytes.data(), data + target, static_cast<size_t>(qMin<qint64>(dataSize, size - target)));
        }
        dll.exports.insert(name, bytes);
    }
    return dll;
}

/// The rules of one script extender release, as their PluginManager applies them
enum class Rules {
    SkseQuery,  ///< 2.0.20-2.1.1 (1.5.97-1.6.317): SKSEPlugin_Query decides when the game runs
    Skse212,    ///< 1.6.318: version data, no Address Library check
    Skse213,    ///< 1.6.323-1.6.342: + Address Library file
    Skse215,    ///< 1.6.353: + 64-bit check
    Skse220,    ///< 1.6.629: structure layout of 1.6.629+
    Skse221,    ///< 1.6.640-1.7.99: + unknown independence flags refused
    Skse231,    ///< 1.7.104+: + Address Library v5
    F4seQuery,  ///< 1.10.163
    F4se980,    ///< 1.10.980-1.11.137
    F4se1159,   ///< 1.11.159+
    SfseEarly,  ///< before 1.7.33: only the version list
    Sfse013,    ///< 1.7.33-1.7.36
    Sfse020,    ///< 1.8.86-1.13.x
    Sfse0213,   ///< 1.14.70-1.14.x
    Sfse0217,   ///< 1.15.216+
};

Rules rulesFor(ScriptExtender se, quint32 runtime)
{
    const quint32 version = runtime & ~0xFu;
    switch (se) {
        case ScriptExtender::SKSE64:
            if (version >= packVersion(1, 7, 104)) return Rules::Skse231;
            if (version >= packVersion(1, 6, 640)) return Rules::Skse221;
            if (version >= packVersion(1, 6, 629)) return Rules::Skse220;
            if (version >= packVersion(1, 6, 353)) return Rules::Skse215;
            if (version >= packVersion(1, 6, 323)) return Rules::Skse213;
            if (version >= packVersion(1, 6, 318)) return Rules::Skse212;
            return Rules::SkseQuery;
        case ScriptExtender::F4SE:
            if (version >= packVersion(1, 11, 159)) return Rules::F4se1159;
            if (version >= packVersion(1, 10, 980)) return Rules::F4se980;
            return Rules::F4seQuery;
        case ScriptExtender::SFSE:
        case ScriptExtender::None:
            if (version >= packVersion(1, 15, 216)) return Rules::Sfse0217;
            if (version >= packVersion(1, 14, 70)) return Rules::Sfse0213;
            if (version >= packVersion(1, 8, 86)) return Rules::Sfse020;
            if (version >= packVersion(1, 7, 33)) return Rules::Sfse013;
            return Rules::SfseEarly;
    }
    return Rules::SfseEarly;
}

struct Names {
    const char* name;      ///< "SKSE"
    const char* folder;    ///< "SKSE"
    const char* prefix;    ///< export and DLL prefix: "SKSEPlugin_", "skse64"
    const char* exports;
    const char* game;
};

Names namesOf(ScriptExtender se)
{
    switch (se) {
        case ScriptExtender::SKSE64: return { "SKSE", "SKSE", "skse64", "SKSEPlugin_", "Skyrim Special Edition" };
        case ScriptExtender::F4SE: return { "F4SE", "F4SE", "f4se", "F4SEPlugin_", "Fallout 4" };
        case ScriptExtender::SFSE:
        case ScriptExtender::None: return { "SFSE", "SFSE", "sfse", "SFSEPlugin_", "Starfield" };
    }
    return {};
}

/// The Address Library file the script extender looks for
QString addressLibraryFile(ScriptExtender se, quint32 runtime)
{
    const auto numbers = QStringLiteral("%1-%2-%3-0").arg(runtime >> 24).arg((runtime >> 16) & 0xFF).arg((runtime >> 4) & 0xFFF);
    switch (se) {
        case ScriptExtender::SKSE64: return "SKSE/Plugins/versionlib-" + numbers + ".bin";
        case ScriptExtender::F4SE: return "F4SE/Plugins/version-" + numbers + ".bin";
        case ScriptExtender::SFSE:
        case ScriptExtender::None: return "SFSE/Plugins/versionlib-" + numbers + ".bin";
    }
    return {};
}

/// SFSE's list of plugin releases known to break (kMinVersionList, later kPluginCompatList)
struct CompatEntry {
    const char* name;
    quint32 minVersion;
    const char* reason;  ///< empty: the plugin may not claim version independence
};
constexpr CompatEntry kSfseCompat[] = {
    { "BakaAchievementEnabler", 0x02000000, "broken before plugin version 2.0.0" },
    { "BakaKillMyGames", 0x02000000, "broken before plugin version 2.0.0" },
    { "BakaQuitGameFix", 0x02000000, "broken before plugin version 2.0.0" },
    { "BakaQuickFullSaves", 0x02000000, "broken before plugin version 2.0.0" },
    { "StarfieldRadio", 10001, "crashes due to bad version data (update past version 1.0.3)" },
    { "SaveTweaks", 4, "crashes due to bad version data (update past version 3)" },
    { "Starfield-NoAffinityLoss", 10301, "crashes due to bad version data (update past version 1.3.0)" },
};

/// Why the script extender refuses a plugin, as plain words; empty when it loads it
struct Verdict {
    enum Kind { Loads, Incompatible, AddressLibrary, NewAddressLibrary, BeforeLayout, NewerExtender, ThirtyTwoBit, Unknown, OlderApi, NewerApi, Blocked };
    Kind kind = Loads;
    QList<quint32> versions;  ///< the game versions it is made for
    quint32 extenderNeeded = 0;
    QString detail;
};

Verdict judge(ScriptExtender se, Rules rules, quint32 runtime, quint32 seVersion, const QString& dataDir, const Dll& dll)
{
    const auto names = namesOf(se);
    const QByteArray versionExport = QByteArray(names.exports) + "Version";
    const QByteArray queryExport = QByteArray(names.exports) + "Query";
    const QByteArray loadExport = QByteArray(names.exports) + "Load";
    const bool skse = se == ScriptExtender::SKSE64;
    Verdict verdict;

    if (rules == Rules::SkseQuery || rules == Rules::F4seQuery) {
        // LoadLibrary of a 32-bit DLL fails in the 64-bit game
        if (dll.machine != 0x8664) {
            verdict.kind = Verdict::ThirtyTwoBit;
        } else if (!dll.exports.contains(queryExport) && dll.exports.contains(versionExport)) {
            verdict.kind = Verdict::NewerApi;  // only the version data later releases read
        }
        return verdict;
    }
    if (!dll.exports.contains(versionExport)) {
        // without version data and without the old entry point it is not a plugin at all (a helper DLL)
        if (dll.exports.contains(queryExport)) {
            verdict.kind = Verdict::OlderApi;
        }
        return verdict;
    }
    const bool checksBitness = rules != Rules::Skse212 && rules != Rules::Skse213;
    if (checksBitness && dll.machine != 0x8664) {
        verdict.kind = Verdict::ThirtyTwoBit;
        return verdict;
    }
    const auto& v = dll.exports.value(versionExport);
    const auto* p = reinterpret_cast<const uchar*>(v.constData());
    const quint32 dataVersion = u32(p);
    const quint32 pluginVersion = u32(p + 4);
    const QByteArray name = QByteArray(reinterpret_cast<const char*>(p + 8), 256).split('\0').first();
    QList<quint32> compatible;
    const int listAt = skse ? 780 : 528;
    for (int i = 0; i < 16; i++) {
        const quint32 entry = u32(p + listAt + 4 * i);
        if (!entry) {
            break;
        }
        compatible << entry;
    }
    verdict.versions = compatible;
    const quint32 seRequired = u32(p + (skse ? 844 : 592));
    const bool listed = compatible.contains(runtime);
    auto addressLibraryMissing = [&] { return !QFileInfo::exists(FS::PathCombine(dataDir, addressLibraryFile(se, runtime))); };
    auto unknown = [&verdict](const QString& detail) {
        verdict.kind = Verdict::Unknown;
        verdict.detail = detail;
        return verdict;
    };

    if (rules == Rules::Skse212 || rules == Rules::Skse213 || rules == Rules::Skse215) {
        if (dataVersion != 1) {
            return unknown(QObject::tr("its version information is not in a form %1 reads").arg(names.name));
        }
    } else if (dataVersion == 0) {
        return unknown(QObject::tr("its version information is not in a form %1 reads").arg(names.name));
    }
    if (name.isEmpty()) {
        return unknown(QObject::tr("its version information has no name"));
    }

    bool independent = false;
    if (skse) {
        const quint32 independenceEx = u32(p + 772);
        const quint32 independence = u32(p + 776);
        constexpr quint32 addressLibrary = 1, signatures = 2, structs629 = 4;
        constexpr quint32 noStructUse = 1, addressLibraryV5 = 2;
        if (rules <= Rules::Skse215) {
            if (independence & ~(addressLibrary | signatures)) {
                verdict.kind = Verdict::NewerApi;
                return verdict;
            }
            if (rules != Rules::Skse212 && (independence & addressLibrary)) {
                if (addressLibraryMissing()) {
                    verdict.kind = Verdict::AddressLibrary;
                    return verdict;
                }
                independent = true;
            } else {
                independent = independence != 0;
            }
        } else {
            if (rules != Rules::Skse220 && (independence & ~(addressLibrary | signatures | structs629))) {
                verdict.kind = Verdict::NewerApi;
                return verdict;
            }
            independent = independence & (addressLibrary | signatures);
            Verdict::Kind notListed = Verdict::Incompatible;
            if (independence & addressLibrary) {
                if (addressLibraryMissing()) {
                    verdict.kind = Verdict::AddressLibrary;
                    return verdict;
                }
                // built against the Address Library before its 1.7.x change, between 1986 and 2025-05-26
                if (rules == Rules::Skse231 && !(independenceEx & addressLibraryV5) && dll.buildTime >= 520128000 && dll.buildTime < 1748217600) {
                    notListed = Verdict::NewAddressLibrary;
                    independent = false;
                }
            }
            if (independent && !(independence & structs629) && !(independenceEx & noStructUse)) {
                verdict.kind = Verdict::BeforeLayout;
                return verdict;
            }
            if (!independent && !listed) {
                verdict.kind = notListed;
                return verdict;
            }
        }
        if (rules <= Rules::Skse215 && !independent && !listed) {
            verdict.kind = Verdict::Incompatible;
            return verdict;
        }
    } else {
        const quint32 address = u32(p + 520);
        const quint32 structure = u32(p + 524);
        const quint32 reservedBreaking = u32(p + 600);
        quint32 addressFlags = 0;
        quint32 checkedAddressFlags = 0;
        quint32 structureFlags = 0;
        bool checkOnlyWhenIndependent = false;
        bool notIndependent = false;
        switch (rules) {
            case Rules::F4se980:
                addressFlags = checkedAddressFlags = 1u | 2u;
                structureFlags = 1u | 2u;
                checkedAddressFlags = 2u;
                break;
            case Rules::F4se1159:
                addressFlags = 1u | 4u;
                checkedAddressFlags = 4u;
                structureFlags = 1u | 4u;
                break;
            case Rules::Sfse013:
                addressFlags = 1u | 2u;
                checkedAddressFlags = 2u;
                structureFlags = 1u | 2u;
                checkOnlyWhenIndependent = true;
                break;
            case Rules::Sfse020:
                addressFlags = 1u | 2u;
                checkedAddressFlags = 2u;
                structureFlags = 1u | 4u;
                break;
            case Rules::Sfse0213:
                addressFlags = 1u | 2u;
                checkedAddressFlags = 2u;
                structureFlags = 1u | 8u;
                break;
            case Rules::Sfse0217:
                addressFlags = 1u | 4u;
                checkedAddressFlags = 2u | 4u;
                structureFlags = 1u | 8u;
                break;
            default:
                break;
        }
        // SFSE's list of plugin releases known to break, from 0.1.6 (game version 1.8.86) on
        if (se == ScriptExtender::SFSE && rules >= Rules::Sfse020) {
            for (const auto& entry : kSfseCompat) {
                if (name == entry.name && pluginVersion < entry.minVersion) {
                    verdict.kind = Verdict::Blocked;
                    verdict.detail = QString::fromLatin1(entry.reason);
                    return verdict;
                }
            }
            if (rules == Rules::Sfse0217 && name == "Starfield Engine Fixes - SFSE Plugin by LarannKiar") {
                notIndependent = true;
            }
        }
        if (rules != Rules::SfseEarly) {
            independent = (address & addressFlags) && (structure & structureFlags) && !reservedBreaking && !notIndependent;
            if ((address & checkedAddressFlags) && (!checkOnlyWhenIndependent || independent) && addressLibraryMissing()) {
                verdict.kind = Verdict::AddressLibrary;
                return verdict;
            }
        }
        if (!independent && !listed) {
            verdict.kind = Verdict::Incompatible;
            return verdict;
        }
    }
    if (seVersion != 0 && seRequired > seVersion) {
        verdict.kind = Verdict::NewerExtender;
        verdict.extenderNeeded = seRequired;
    }
    return verdict;
}

QString explain(ScriptExtender se, quint32 runtime, const Verdict& verdict, const QStringList& installedLibraries)
{
    const auto names = namesOf(se);
    const auto game = versionText(runtime);
    QStringList versions;
    for (const auto v : verdict.versions) {
        versions << versionText(v);
    }
    switch (verdict.kind) {
        case Verdict::Loads: return {};
        case Verdict::Incompatible:
            return versions.isEmpty()
                       ? QObject::tr("It names no game version it works with, so %1 for your game (%2) will not load it. Get the mod's file "
                                     "made for %2.")
                             .arg(names.name, game)
                       : QObject::tr("It is made for game version %1, but your game is %2, so %3 will not load it. Get the mod's file made "
                                     "for %2 (often called \"AE\" or listing that version), or one made for all versions.")
                             .arg(versions.join(", "), game, names.name);
        case Verdict::AddressLibrary:
            return QObject::tr("It needs the Address Library for your game version (%1, the file %2), which is not installed%3, so %4 "
                               "will not load it. Install the Address Library file for %1.")
                .arg(game, QFileInfo(addressLibraryFile(se, runtime)).fileName(),
                     installedLibraries.isEmpty() ? QString() : QObject::tr(" (the installed one is for %1)").arg(installedLibraries.join(", ")),
                     names.name);
        case Verdict::NewAddressLibrary:
            return QObject::tr("It was built before the Address Library changed for Skyrim 1.7 (May 2025), so SKSE for your game (%1) "
                               "will not load it. Get a release of the mod made for 1.7.")
                .arg(game);
        case Verdict::BeforeLayout:
            return QObject::tr("It only works with game versions before 1.6.629, and your game is %1, so SKSE will not load it. Get "
                               "the mod's file made for %1.")
                .arg(game);
        case Verdict::NewerExtender:
            return QObject::tr("It needs %1 %2 or newer, but an older %1 is installed. Update %1 (Mods page > Install Script Extender).")
                .arg(names.name, versionText(verdict.extenderNeeded).section(' ', 0, 0));
        case Verdict::ThirtyTwoBit:
            return se == ScriptExtender::SKSE64
                       ? QObject::tr("It is a 32-bit DLL made for the original Skyrim (Legendary Edition); Skyrim Special Edition can not "
                                     "use it. Get the mod's Special Edition file.")
                       : QObject::tr("It is a 32-bit DLL; %1 is 64-bit and can not use it.").arg(names.game);
        case Verdict::OlderApi:
            return QObject::tr("It is made for an older version of %1: it lacks the version information %2 for your game (%3) "
                               "needs, so %2 will not load it. Get the mod's file made for %3.")
                .arg(names.game, names.name, game);
        case Verdict::NewerApi:
            return QObject::tr("It is made for a newer version of %1 than yours (%2), so %3 for %2 will not load it. Get the mod's "
                               "file made for %2, or update the game.")
                .arg(names.game, game, names.name);
        case Verdict::Blocked:
            return QObject::tr("%1 refuses this release of it (%2). Update the mod.").arg(names.name, verdict.detail);
        case Verdict::Unknown:
            return QObject::tr("%1 will not load it: %2. Reinstall or update the mod.").arg(names.name, verdict.detail);
    }
    return {};
}

}  // namespace

QList<Finding> checkPlugins(ScriptExtender se, quint32 runtime, quint32 seVersion, const QString& dataDir)
{
    QList<Finding> findings;
    if (se == ScriptExtender::None) {
        return findings;
    }
    const auto names = namesOf(se);
    const auto rules = rulesFor(se, runtime);
    const auto folder = QString(names.folder) + "/Plugins";
    const auto pluginDir = FS::PathCombine(dataDir, folder);
    if (!QFileInfo(pluginDir).isDir()) {
        return findings;
    }
    QStringList libraries;  // Address Library files that are there, for saying which game version they are for
    static const QRegularExpression libraryName(R"(^version(?:lib)?-(\d+)-(\d+)-(\d+)-\d+\.bin$)", QRegularExpression::CaseInsensitiveOption);
    QStringList dlls;
    for (const auto& entry : QDirListing(pluginDir, QDirListing::IteratorFlag::FilesOnly)) {
        if (const auto m = libraryName.match(entry.fileName()); m.hasMatch()) {
            libraries << QStringLiteral("%1.%2.%3").arg(m.captured(1), m.captured(2), m.captured(3));
        } else if (entry.fileName().endsWith(".dll", Qt::CaseInsensitive)) {
            dlls << entry.fileName();
        }
    }
    std::sort(dlls.begin(), dlls.end(), [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    const QList<QByteArray> wanted{ QByteArray(names.exports) + "Version", QByteArray(names.exports) + "Query", QByteArray(names.exports) + "Load" };
    for (const auto& name : dlls) {
        const auto dll = readDll(FS::PathCombine(pluginDir, name), wanted, 848);
        if (!dll.valid) {
            continue;
        }
        const auto verdict = judge(se, rules, runtime, seVersion, dataDir, dll);
        if (verdict.kind != Verdict::Loads) {
            findings << Finding{ folder + "/" + name, explain(se, runtime, verdict, libraries) };
        }
    }
    return findings;
}

QList<Finding> checkScriptExtender(ScriptExtender se, quint32 runtime, const QString& gameDir)
{
    QList<Finding> findings;
    if (se == ScriptExtender::None) {
        return findings;
    }
    const auto names = namesOf(se);
    if (!QFileInfo::exists(FS::PathCombine(gameDir, QString(names.prefix) + "_loader.exe"))) {
        return findings;
    }
    // the loader starts <prefix>_<major>_<minor>_<build>.dll for the game it finds
    const QRegularExpression runtimeDll(QStringLiteral(R"(^%1_(\d+)_(\d+)_(\d+)(?:_[a-z]+)?\.dll$)").arg(names.prefix),
                                        QRegularExpression::CaseInsensitiveOption);
    QStringList builtFor;
    QString firstDll;
    for (const auto& entry : QDirListing(gameDir, QDirListing::IteratorFlag::FilesOnly)) {
        const auto m = runtimeDll.match(entry.fileName());
        if (!m.hasMatch()) {
            continue;
        }
        const auto version = packVersion(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
        if (version == (runtime & ~0xFu)) {
            return findings;
        }
        builtFor << versionText(version);
        if (firstDll.isEmpty()) {
            firstDll = entry.fileName();
        }
    }
    if (!builtFor.isEmpty()) {
        findings << Finding{ firstDll, QObject::tr("The installed %1 is made for game version %2, but your game is %3, so it can not start "
                                                   "and none of its plugins load. Install the %1 build for %3 (Mods page > Install Script "
                                                   "Extender).")
                                           .arg(names.name, builtFor.join(", "), versionText(runtime)) };
    }
    return findings;
}

QList<Finding> checkPluginHeaders(const QString& gameDomain, const QString& dataDir, const QStringList& pluginNames)
{
    // LOOT's minimum header versions (src/gui/state/game/game_settings.cpp)
    static const QHash<QString, float> minimum{
        { "oblivion", 0.8f },  { "skyrim", 0.94f },   { "fallout3", 0.94f },  { "skyrimspecialedition", 1.7f },
        { "newvegas", 1.32f }, { "fallout4", 0.95f }, { "starfield", 0.96f },
    };
    QList<Finding> findings;
    const auto it = minimum.constFind(gameDomain);
    if (it == minimum.constEnd()) {
        return findings;
    }
    const int headerLength = gameDomain == "oblivion" ? 20 : 24;
    for (const auto& name : pluginNames) {
        QFile file(FS::PathCombine(dataDir, name));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const auto head = file.read(headerLength + 10);
        if (head.size() < headerLength + 10 || !head.startsWith("TES4") || head.mid(headerLength, 4) != "HEDR") {
            continue;
        }
        float version = 0;
        std::memcpy(&version, head.constData() + headerLength + 6, sizeof(version));
        if (version < *it) {
            const auto made = gameDomain == "skyrimspecialedition" ? QObject::tr("the original Skyrim (Legendary Edition)")
                                                                    : QObject::tr("an older game");
            findings << Finding{ name, QObject::tr("Its header version is %1, lower than the %2 every plugin of this game has: it was "
                                                   "made for %3 and not converted, which can crash the game. Get the mod's file made "
                                                   "for this game.")
                                           .arg(QString::number(version, 'g', 3), QString::number(*it, 'g', 3), made) };
        }
    }
    return findings;
}

QList<Finding> check(const GameDefinition& game, const QString& gameDir, const QString& dataDir, const QStringList& pluginNames)
{
    // findings name files relative to the game folder
    const auto dataPrefix = QDir(gameDir).relativeFilePath(dataDir);
    auto inData = [&dataPrefix](QList<Finding> found) {
        for (auto& finding : found) {
            finding.file = dataPrefix == "." ? finding.file : dataPrefix + "/" + finding.file;
        }
        return found;
    };
    auto findings = game.hasPlugins() ? inData(checkPluginHeaders(game.domain, dataDir, pluginNames)) : QList<Finding>();
    const auto se = game.domain == "skyrimspecialedition" ? ScriptExtender::SKSE64
                    : game.domain == "fallout4"           ? ScriptExtender::F4SE
                    : game.domain == "starfield"          ? ScriptExtender::SFSE
                                                          : ScriptExtender::None;
    const auto version = GameFiles::fileVersion(FS::PathCombine(gameDir, game.executable)).split('.');
    if (se == ScriptExtender::None || version.size() < 3) {
        return findings;
    }
    const int store = QFileInfo::exists(FS::PathCombine(gameDir, "Galaxy64.dll"))                 ? 1
                      : QFileInfo::exists(FS::PathCombine(gameDir, "EOSSDK-Win64-Shipping.dll")) ? 2
                                                                                                  : 0;
    const auto runtime = packVersion(version[0].toInt(), version[1].toInt(), version[2].toInt(), store);
    // the installed script extender's own version (file version 0.major.minor.beta), for plugins that need a newer one
    quint32 seVersion = 0;
    const auto names = namesOf(se);
    for (const auto& entry : QDirListing(gameDir, QDirListing::IteratorFlag::FilesOnly)) {
        const auto lower = entry.fileName().toLower();
        if (lower.startsWith(QString(names.prefix) + "_" + version[0] + "_" + version[1] + "_" + version[2]) && lower.endsWith(".dll")) {
            const auto parts = GameFiles::fileVersion(entry.filePath()).split('.');
            if (parts.size() == 4) {
                seVersion = packVersion(parts[1].toInt(), parts[2].toInt(), parts[3].toInt());
            }
        }
    }
    findings << checkScriptExtender(se, runtime, gameDir);
    findings << inData(checkPlugins(se, runtime, seVersion, dataDir));
    return findings;
}

}  // namespace VersionCheck
