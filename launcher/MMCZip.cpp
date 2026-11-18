// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (c) 2023-2024 Trial97 <alexandru.tripon97@gmail.com>
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

#include "MMCZip.h"
#include <archive.h>
#include "FileSystem.h"
#include "archive/ArchiveReader.h"
#include "archive/ArchiveWriter.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QUrl>
#include <memory>

namespace MMCZip {
// ours
bool collectFileListRecursively(const QString& rootDir, const QString& subDir, QFileInfoList* files, FilterFileFunction excludeFilter)
{
    QDir rootDirectory(rootDir);
    if (!rootDirectory.exists())
        return false;

    QDir directory;
    if (subDir == nullptr)
        directory = rootDirectory;
    else
        directory = QDir(subDir);

    if (!directory.exists())
        return false;  // shouldn't ever happen

    // recurse directories
    QFileInfoList entries = directory.entryInfoList(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const auto& e : entries) {
        if (!collectFileListRecursively(rootDir, e.filePath(), files, excludeFilter))
            return false;
    }

    // collect files
    entries = directory.entryInfoList(QDir::Files);
    for (const auto& e : entries) {
        if (excludeFilter && excludeFilter(e)) {
            QString relativeFilePath = rootDirectory.relativeFilePath(e.absoluteFilePath());
            qDebug() << "Skipping file" << relativeFilePath;
            continue;
        }

        files->append(e);  // we want the original paths for compressDirFiles
    }
    return true;
}
}  // namespace MMCZip
