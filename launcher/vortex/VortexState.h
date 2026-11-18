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

// Plain C++ without Qt: setup links this too, to list what there is to bring over from Vortex.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace VortexState {

/// Vortex keeps its state in a LevelDB database: %APPDATA%\Vortex\state.v2
std::wstring defaultDatabase();

/**
 * Every live key of a LevelDB database with its newest value: the tables the MANIFEST names and the write-ahead logs
 * after them, merged by sequence number, deleted keys left out. Only reads (shared), so Vortex is never changed; a
 * record torn by a crash is skipped as LevelDB does.
 */
bool readDatabase(const std::wstring& dir, std::map<std::string, std::string>& entries, std::string& error);

/// Vortex stores each leaf of its state under its path joined by "###"
constexpr const char* k_separator = "###";
std::vector<std::string> splitKey(const std::string& key);

/// A JSON string value as UTF-8, or nothing when @p json is not a string
std::optional<std::string> jsonString(const std::string& json);

/// A game Vortex manages that BetterNMM knows: Vortex's game id, the Nexus Mods domain BetterNMM uses, its name
struct KnownGame {
    const char* vortexId;
    const char* domain;
    const char* name;
};
const std::vector<KnownGame>& knownGames();
const KnownGame* gameByVortexId(const std::string& id);

/// A Vortex profile: one set of enabled mods (and with its own load order, and optionally own saves and settings)
struct Profile {
    std::string id;
    std::string name;
    std::string gameId;  ///< Vortex's game id
    int enabledMods = 0;
    double lastActivated = 0;  ///< ms since epoch, 0 = never
};
/// The profiles of games BetterNMM knows, most recently used first
std::vector<Profile> profiles(const std::map<std::string, std::string>& entries);

// building blocks, exposed for tests
bool snappyUncompress(const char* data, size_t size, std::string& out);
uint32_t crc32c(const char* data, size_t size, uint32_t crc = 0);

}  // namespace VortexState
