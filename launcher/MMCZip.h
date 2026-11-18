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

#pragma once

#include <QDir>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QHash>
#include <QSet>
#include <QString>
#include <functional>
#include <optional>

#include "Result.h"
#include "archive/ArchiveReader.h"


namespace MMCZip {
using FilterFileFunction = std::function<bool(const QFileInfo&)>;

/**
 * Populate a QFileInfoList with a directory tree recursively, while allowing to excludeFilter what shouldn't be included.
 * \param rootDir directory to start off
 * \param subDir subdirectory, should be nullptr for first invocation
 * \param files resulting list of QFileInfo
 * \param excludeFilter function to excludeFilter which files shouldn't be included (returning true means to excude)
 * \return true for success or false for failure
 */
bool collectFileListRecursively(const QString& rootDir, const QString& subDir, QFileInfoList* files, FilterFileFunction excludeFilter);
}  // namespace MMCZip
