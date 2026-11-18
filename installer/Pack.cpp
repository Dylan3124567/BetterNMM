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

// Build tool: bnmm-pack <setup stub.exe> <installed files folder> <out setup.exe> <version>

#include <cstdio>

#include "Files.h"
#include "Payload.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 5) {
        std::fwprintf(stderr, L"usage: bnmm-pack <setup stub.exe> <installed files folder> <out setup.exe> <version>\n");
        return 2;
    }
    const DWORD started = GetTickCount();
    std::wstring error;
    if (!Payload::write(argv[1], argv[2], argv[3], argv[4], error)) {
        std::fwprintf(stderr, L"bnmm-pack: %ls\n", error.c_str());
        return 1;
    }
    Payload::Index index;
    if (!Payload::read(argv[3], index, error)) {
        std::fwprintf(stderr, L"bnmm-pack: the written setup does not read back: %ls\n", error.c_str());
        return 1;
    }
    std::string out;
    Files::readAll(argv[3], out);
    std::fwprintf(stdout, L"%ls: %zu files, %.1f MiB packed into %.1f MiB (setup program %.0f KiB) in %.0f s\n", argv[3],
                  index.files.size(), index.totalSize / 1048576.0, out.size() / 1048576.0, index.stubSize / 1024.0,
                  (GetTickCount() - started) / 1000.0);
    return 0;
}
