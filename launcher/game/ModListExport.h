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

#include <QList>
#include <QString>
#include <QStringList>

#include "game/ModList.h"

/// A mod list to share (a support thread, a guide): the mods in priority order with versions and Nexus Mods pages, and the
/// plugins in load order
namespace ModListExport {

enum class Format { Markdown, Csv, Text };

/// The format a file name asks for by its extension: .csv, .txt, anything else Markdown
Format formatFor(const QString& fileName);

/// @p mods lowest priority first, @p plugins the active ones in load order (left out of CSV, which holds one table)
QString write(const QString& title, const QList<ModEntry>& mods, const QStringList& plugins, Format format);

}  // namespace ModListExport
