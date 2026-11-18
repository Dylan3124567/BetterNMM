// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (c) 2022 flowln <flowlnlnln@gmail.com>
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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "InstanceImportTask.h"

#include <QDir>
#include <QFileInfo>

#include "FileSystem.h"
#include "archive/ArchiveReader.h"
#include "archive/ExtractZipTask.h"
#include "game/GameFiles.h"
#include "game/GameInstance.h"
#include "game/Steam.h"
#include "settings/INISettingsObject.h"

namespace {

/// The exported instance's settings as plain key/value pairs, read straight from the archive
QMap<QString, QString> readExportedSettings(const QString& archive)
{
    QMap<QString, QString> values;
    MMCZip::ArchiveReader reader(archive);
    if (!reader.collectFiles()) {
        return values;
    }
    for (const auto& file : reader.getFiles()) {
        const auto clean = QString(file).replace('\\', '/');
        if (clean.compare("instance.cfg", Qt::CaseInsensitive) != 0 && !clean.endsWith("/instance.cfg", Qt::CaseInsensitive)) {
            continue;
        }
        auto data = reader.readFile(file);
        if (!data) {
            return values;
        }
        for (const auto& line : QString::fromUtf8(*data).split('\n')) {
            const auto at = line.indexOf('=');
            if (at > 0) {
                values.insert(line.left(at).trimmed(), line.mid(at + 1).trimmed());
            }
        }
        break;
    }
    return values;
}

QString resolveGameDir(const QString& recorded, int appId)
{
    if (!recorded.isEmpty() && QFileInfo(recorded).isDir()) {
        return QDir::cleanPath(recorded);
    }
    if (appId > 0) {
        if (auto app = Steam::findApp(Steam::steamRoot(), appId)) {
            return app->installDir;
        }
    }
    return {};
}

}  // namespace

InstanceImportTask::InstanceImportTask(QString archive) : m_archive(std::move(archive)) {}

QString InstanceImportTask::gameDirFor(const QString& archive)
{
    const auto settings = readExportedSettings(archive);
    return resolveGameDir(settings.value("SourceGameDir"), settings.value("SteamAppId").toInt());
}

void InstanceImportTask::executeTask()
{
    setStatus(tr("Extracting %1...").arg(QFileInfo(m_archive).fileName()));
    m_step.reset(new MMCZip::ExtractZipTask(m_archive, QDir(m_stagingPath)));
    connect(m_step.get(), &Task::progress, this, &Task::setProgress);
    connect(m_step.get(), &Task::failed, this, [this](const QString& reason) { emitFailed(reason); });
    connect(m_step.get(), &Task::succeeded, this, &InstanceImportTask::extracted);
    m_step->start();
}

void InstanceImportTask::extracted()
{
    // exports made from a folder put everything one level down
    if (!QFileInfo::exists(FS::PathCombine(m_stagingPath, "instance.cfg"))) {
        const auto entries = QDir(m_stagingPath).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (entries.size() == 1 && QFileInfo::exists(FS::PathCombine(entries.first().absoluteFilePath(), "instance.cfg"))) {
            const auto inner = entries.first().absoluteFilePath();
            for (const auto& entry : QDir(inner).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
                QDir().rename(entry.absoluteFilePath(), FS::PathCombine(m_stagingPath, entry.fileName()));
            }
            QDir().rmdir(inner);
        }
    }
    auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg"));
    settings->registerSetting("InstanceType", "");
    if (settings->get("InstanceType").toString() != "Game") {
        emitFailed(tr("This archive does not contain a BetterNMM instance."));
        return;
    }
    GameInstance staged(m_globalSettings, std::move(settings), m_stagingPath);
    const auto gameDir = resolveGameDir(staged.sourceGameDir(), staged.steamAppId());
    if (gameDir.isEmpty()) {
        emitFailed(tr("%1 is not installed on this computer. Install it with Steam and import again.")
                       .arg(staged.settings()->get("GameName").toString()));
        return;
    }
    // an exported Steam instance becomes an isolated one: the importer's Steam installation is not ours to change
    staged.settings()->set("InstanceKind", "Isolated");
    staged.settings()->set("SourceGameDir", gameDir);
    staged.setName(name());
    if (!m_instIcon.isEmpty()) {
        staged.setIconKey(m_instIcon);
    }
    staged.regenerateUuid();
    for (const auto& stale : { "game", "overwritten" }) {
        GameFiles::removeTree(FS::PathCombine(m_stagingPath, stale));
    }
    for (const auto& stale : { "deployment.json", "base.json" }) {
        GameFiles::removeFile(FS::PathCombine(m_stagingPath, stale));
    }
    if (auto prepared = staged.prepareProfile(); !prepared) {
        emitFailed(prepared.error());
        return;
    }
    setStatus(tr("Linking game files..."));
    m_step.reset(staged.createBaseSyncTask().release());
    connect(m_step.get(), &Task::progress, this, &Task::setProgress);
    connect(m_step.get(), &Task::failed, this, [this](const QString& reason) { emitFailed(tr("Linking the game files failed: %1").arg(reason)); });
    connect(m_step.get(), &Task::succeeded, this, [this] { emitSucceeded(); });
    m_step->start();
}

bool InstanceImportTask::abort()
{
    return m_step ? m_step->abort() : true;
}
