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

#include "CrashReport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include "FileSystem.h"
#include "game/Deployer.h"
#include "game/GameFiles.h"
#include "game/GameInstance.h"
#include "game/ModList.h"
#include "game/PluginList.h"
#include "game/VersionCheck.h"

namespace CrashReport {

QString describeCode(quint32 code)
{
    switch (code) {
        case 0xC0000005:
            return QObject::tr("access violation (0xC0000005): it used memory it should not have, which mods are the usual cause of");
        case 0xC00000FD:
            return QObject::tr("stack overflow (0xC00000FD)");
        case 0xC0000409:
            return QObject::tr("it stopped itself after detecting a problem (0xC0000409)");
        case 0xC0000374:
            return QObject::tr("memory corruption (0xC0000374)");
        case 0xC000001D:
            return QObject::tr("illegal instruction (0xC000001D)");
        case 0xC0000094:
            return QObject::tr("division by zero (0xC0000094)");
        case 0xE06D7363:
            return QObject::tr("an unhandled C++ exception (0xE06D7363)");
        case 0x80000003:
            return QObject::tr("a breakpoint (0x80000003)");
        case 0:
            return QObject::tr("an error Windows did not name");
        default:
            return QObject::tr("error 0x%1").arg(QString::number(code, 16).rightJustified(8, '0').toUpper());
    }
}

QString crashSection(const QString& log)
{
    // Crash Logger, Trainwreck, Buffout and .NET Script Framework all end with what was loaded: every DLL and plugin
    static const QRegularExpression lists(R"(^\[?\s*(MODULES|((SKSE|F4SE|SFSE|NVSE|OBSE|FOSE)\s+)?PLUGINS|LOADED MODULES|GAME PLUGINS)\b)",
                                          QRegularExpression::CaseInsensitiveOption);
    QStringList kept;
    for (const auto& line : log.split('\n')) {
        if (lists.match(line.trimmed()).hasMatch()) {
            break;
        }
        kept << line;
    }
    return kept.join('\n');
}

QMap<QString, int> mentions(const QString& text, const QSet<QString>& known)
{
    // a candidate ends in a plugin or DLL extension; names can hold spaces, so the longest known tail of it counts
    static const QRegularExpression candidate(R"([A-Za-z0-9_][^"\r\n\t:<>|*?=,;]*?\.(?:esp|esm|esl|dll)(?![A-Za-z0-9_]))",
                                              QRegularExpression::CaseInsensitiveOption);
    QMap<QString, int> counts;
    for (auto it = candidate.globalMatch(text); it.hasNext();) {
        const auto words = it.next().captured(0).split(' ', Qt::SkipEmptyParts);
        for (qsizetype i = 0; i < words.size(); i++) {
            auto tail = words.mid(i).join(' ').toLower();
            while (!tail.isEmpty() && QString("`'\"[(").contains(tail.front())) {
                tail.remove(0, 1);
            }
            if (known.contains(tail)) {
                counts[tail]++;
                break;
            }
        }
    }
    return counts;
}

QString findCrashLog(const QStringList& folders, const QDateTime& since)
{
    // the clock of a crash logger's file and BetterNMM's start can be a moment apart
    const auto after = since.addSecs(-5);
    QString newest;
    QDateTime newestTime;
    for (const auto& folder : folders) {
        if (!QFileInfo(folder).isDir()) {
            continue;
        }
        // crash loggers write one or two levels down (SKSE, SKSE/Crashlogs); saves are never looked at
        QList<std::pair<QString, int>> pending{ { folder, 0 } };
        while (!pending.isEmpty()) {
            const auto [dir, depth] = pending.takeFirst();
            for (const auto& entry : QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (entry.isDir()) {
                    if (depth < 2 && entry.fileName().compare("Saves", Qt::CaseInsensitive) != 0) {
                        pending.append({ entry.absoluteFilePath(), depth + 1 });
                    }
                    continue;
                }
                const auto name = entry.fileName().toLower();
                if (!name.contains("crash") || !(name.endsWith(".log") || name.endsWith(".txt"))) {
                    continue;
                }
                if (const auto time = entry.lastModified(); time >= after && (newest.isEmpty() || time > newestTime)) {
                    newest = entry.absoluteFilePath();
                    newestTime = time;
                }
            }
        }
    }
    return newest;
}

Report analyze(GameInstance* instance, const QDateTime& since)
{
    Report report;
    auto* mods = instance->modList();
    auto modName = [mods](const QString& id) {
        const auto row = mods->indexOf(id);
        return row >= 0 ? mods->mods()[row].name : id;
    };

    // which mod put each plugin and DLL into the game folder
    QHash<QString, QString> owners;     // lower-case file name -> mod id
    QHash<QString, QString> realNames;  // lower-case file name -> as the mod spells it
    QHash<QString, QString> pathOwners;  // lower-case path in the game folder -> mod id
    if (auto manifest = DeploymentManifest::load(instance->deploymentManifestPath())) {
        for (auto it = manifest->files.cbegin(); it != manifest->files.cend(); ++it) {
            pathOwners.insert(it.key(), it->mod);
        }
        for (const auto& file : std::as_const(manifest->files)) {
            const auto real = QFileInfo(file.path).fileName();
            const auto name = real.toLower();
            if (name.endsWith(".dll") || name.endsWith(".esp") || name.endsWith(".esm") || name.endsWith(".esl")) {
                owners.insert(name, file.mod);
                realNames.insert(name, real);
            }
        }
    }

    QStringList folders;
    if (const auto myGames = instance->myGamesDir(); !myGames.isEmpty()) {
        folders << myGames;
    }
    folders << FS::PathCombine(instance->gameRoot(), "CrashLogs") << FS::PathCombine(instance->dataDir(), "NetScriptFramework/Crash");
    report.logPath = findCrashLog(folders, since);
    QSet<QString> suspectIds;
    if (!report.logPath.isEmpty()) {
        QFile log(report.logPath);
        if (log.open(QIODevice::ReadOnly)) {
            const auto counts = mentions(crashSection(QString::fromUtf8(log.read(16 * 1024 * 1024))), QSet<QString>(owners.keyBegin(), owners.keyEnd()));
            QMap<QString, Suspect> byMod;
            for (auto it = counts.begin(); it != counts.end(); ++it) {
                const auto id = owners.value(it.key());
                auto& suspect = byMod[id];
                suspect.modId = id;
                suspect.modName = modName(id);
                suspect.files.insert(realNames.value(it.key(), it.key()), it.value());
                suspect.mentions += it.value();
            }
            report.suspects = byMod.values();
            std::sort(report.suspects.begin(), report.suspects.end(), [](const Suspect& a, const Suspect& b) { return a.mentions > b.mentions; });
            for (const auto& suspect : std::as_const(report.suspects)) {
                suspectIds.insert(suspect.modId);
            }
        }
    }

    // a plugin whose masters do not load crashes the game while it starts
    QStringList activePlugins;
    if (auto* plugins = instance->pluginList(); plugins->refresh()) {
        report.tooManyPlugins = plugins->slotProblems();
        for (const auto& plugin : plugins->plugins()) {
            if (plugin.active || plugin.implicit) {
                activePlugins << plugin.name;
            }
        }
        QSet<QString> active;
        QSet<QString> present;
        for (const auto& plugin : plugins->plugins()) {
            present.insert(plugin.name.toLower());
            if (plugin.active || plugin.implicit) {
                active.insert(plugin.name.toLower());
            }
        }
        for (const auto& plugin : plugins->plugins()) {
            if (!plugin.active && !plugin.implicit) {
                continue;
            }
            for (const auto& master : PluginList::readMasters(FS::PathCombine(instance->dataDir(), plugin.name))) {
                if (!active.contains(master.toLower())) {
                    const auto owner = owners.value(plugin.name.toLower());
                    report.missingMasters << MissingMaster{ .plugin = plugin.name,
                                                            .pluginMod = owner.isEmpty() ? QString() : modName(owner),
                                                            .master = master,
                                                            .presentButInactive = present.contains(master.toLower()) };
                }
            }
        }
    }

    // mods made for another version of the game: DLLs the script extender refuses, plugins made for an older game
    const auto def = instance->definition();
    report.gameVersion = GameFiles::fileVersion(FS::PathCombine(instance->gameRoot(), def.executable));
    for (const auto& finding : VersionCheck::check(def, instance->gameRoot(), instance->dataDir(), activePlugins)) {
        const auto owner = pathOwners.value(finding.file.toLower());
        report.wrongVersions << WrongVersion{ .file = finding.file, .modName = owner.isEmpty() ? QString() : modName(owner), .reason = finding.reason };
    }

    for (const auto& overwrite : mods->overwrites()) {
        report.conflicts << Conflict{ .winner = modName(overwrite.winner),
                                      .loser = modName(overwrite.loser),
                                      .files = overwrite.files,
                                      .examples = overwrite.examples,
                                      .involvesSuspect = suspectIds.contains(overwrite.winner) || suspectIds.contains(overwrite.loser) };
    }
    // the pairs that include a mod the crash log names come first
    std::stable_sort(report.conflicts.begin(), report.conflicts.end(),
                     [](const Conflict& a, const Conflict& b) { return a.involvesSuspect && !b.involvesSuspect; });
    return report;
}

}  // namespace CrashReport
