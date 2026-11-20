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

#include <QSet>
#include <QStringList>

#include "Result.h"

/**
 * Nemesis Unlimited Behavior Engine, which generates the behavior files animation mods need. It lives in the game's
 * Data folder (Nemesis_Engine) and other mods add their patches to it (Nemesis_Engine/mod/<code>).
 */
namespace Nemesis {

/// Its program, relative to the Data folder
inline const QString k_engine = QStringLiteral("Nemesis_Engine/Nemesis Unlimited Behavior Engine.exe");

struct Setup {
    QStringList ticked;           ///< patches ticked now because mods brought them, by name
    bool engineOutdated = false;  ///< its engine needs updating for the patches it has now ("-update")
};

/// The patch codes in a Nemesis_Engine/mod folder
QSet<QString> patchCodes(const QString& modFolder);

/**
 * Makes the Nemesis in @p dataDir ready to run for that Data folder:
 * - it reads and writes this Data folder, not the one of the game installation the registry names
 * - its cache folder exists (its archive has it empty, and the engine update fails without it)
 * - it opens in English without a warning
 * - patches mods brought (codes not in @p ownPatches, the ones Nemesis comes with) are ticked, each once: unticking
 *   one in Nemesis sticks
 */
Result<Setup> prepare(const QString& dataDir, const QSet<QString>& ownPatches);

/// Remembers that the engine was updated for the patches @p dataDir has now
Result<> engineUpdated(const QString& dataDir);

}  // namespace Nemesis
