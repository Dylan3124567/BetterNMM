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

#pragma once

#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QStringList>

class GameInstance;

/// What can be said about a game crash from the instance's mods, plugins and the crash log a crash logger wrote
namespace CrashReport {

/// A mod whose files the crash log names
struct Suspect {
    QString modId;
    QString modName;
    QMap<QString, int> files;  ///< file name -> how often the log names it
    int mentions = 0;
};

/// An active plugin that needs a master the game will not load
struct MissingMaster {
    QString plugin;
    QString pluginMod;  ///< name of the mod it comes from, empty for loose files
    QString master;
    bool presentButInactive = false;
};

/// Two enabled mods with the same files
struct Conflict {
    QString winner;
    QString loser;
    int files = 0;
    QStringList examples;
    bool involvesSuspect = false;
};

/// A mod file made for another version of the game
struct WrongVersion {
    QString file;     ///< relative to the game folder
    QString modName;  ///< empty for files no mod put there
    QString reason;
};

struct Report {
    QString logPath;      ///< the crash log, empty when none was written
    QString gameVersion;  ///< of the game's executable, empty when it can not be read
    QList<Suspect> suspects;
    QList<WrongVersion> wrongVersions;
    QList<MissingMaster> missingMasters;
    QStringList tooManyPlugins;  ///< PluginList::slotProblems()
    QList<Conflict> conflicts;
};

/// "access violation" for 0xC0000005 and so on; the hex code for anything else
QString describeCode(quint32 code);

/// The part of a crash log about the crash itself: the module and plugin lists that close every log name everything loaded
QString crashSection(const QString& log);

/// File names from @p known (lower-case plugin and DLL names) that @p text mentions, with how often
QMap<QString, int> mentions(const QString& text, const QSet<QString>& known);

/// The newest crash log in @p folders (searched with their subfolders, saves left out) written at or after @p since
QString findCrashLog(const QStringList& folders, const QDateTime& since);

/// Everything above, for a crash of @p instance's game that happened after @p since
Report analyze(GameInstance* instance, const QDateTime& since);

}  // namespace CrashReport
