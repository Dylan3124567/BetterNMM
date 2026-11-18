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

// The files setup installs, appended to the setup program itself:
//
//   [setup program] [payload] [footer: u64 payload offset, u64 payload size, "BNMMEND1"]
//   payload: "BNMMPAK1", version, file table (path, size, time), chunk table (raw size, packed size, CRC-32), chunks
//
// The files are one stream cut into chunks of up to 32 MiB, each packed with Windows' own LZMS compressor
// (cabinet.dll), so setup needs no library of its own to unpack them.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Payload {

struct File {
    std::wstring path;  ///< relative, '\' separated
    uint64_t size = 0;
    uint64_t time = 0;  ///< FILETIME of the last write
};

struct Chunk {
    uint32_t rawSize = 0;
    uint32_t packedSize = 0;
    uint32_t crc = 0;
    uint64_t offset = 0;  ///< of its packed bytes, from the start of the setup program
};

struct Index {
    std::wstring version;
    std::vector<File> files;
    std::vector<Chunk> chunks;
    uint64_t stubSize = 0;  ///< the setup program without the payload: what the uninstaller is
    uint64_t totalSize = 0;
};

/// Packs every file below @p stageDir onto a copy of @p stub as @p out
bool write(const std::wstring& stub,
           const std::wstring& stageDir,
           const std::wstring& out,
           const std::wstring& version,
           std::wstring& error);

/// Reads the file table of the payload appended to @p exe
bool read(const std::wstring& exe, Index& index, std::wstring& error);

/// Unpacks the payload of @p exe into @p dir, reporting bytes written; a damaged payload is an error, never half a file
bool extract(const std::wstring& exe,
             const Index& index,
             const std::wstring& dir,
             const std::function<void(uint64_t done)>& progress,
             std::wstring& error);

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0);

}  // namespace Payload
