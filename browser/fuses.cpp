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

// Sets Electron's fuses, the switches built into the Electron program that turn features on or off
// (https://www.electronjs.org/docs/latest/tutorial/fuses). After a fixed marker come a version byte (1), the number of
// fuses, then one byte per fuse: '0' off, '1' on, 'r' removed.
//
// Usage: electron_fuses <program> <index>=<0|1>...   Prints the fuses as they end up.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <program> <index>=<0|1>...\n", argv[0]);
        return 2;
    }
    static const char marker[] = "dL7pKGdnNz796PbbjQWNKmHXBZaB9tsX";
    const size_t markerSize = sizeof marker - 1;
    std::fstream file(argv[1], std::ios::in | std::ios::out | std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    // read in blocks that overlap by the marker's size, so a marker across two blocks is found too
    std::vector<char> block(1 << 20);
    long long base = 0;
    long long found = -1;
    size_t kept = 0;
    while (found < 0) {
        file.read(block.data() + kept, std::streamsize(block.size() - kept));
        const size_t size = kept + size_t(file.gcount());
        if (size < markerSize) {
            break;
        }
        for (size_t i = 0; i + markerSize <= size; i++) {
            if (std::memcmp(block.data() + i, marker, markerSize) == 0) {
                found = base + (long long)i;
                break;
            }
        }
        if (!file) {
            break;  // end of file
        }
        kept = markerSize - 1;
        std::memmove(block.data(), block.data() + size - kept, kept);
        base += (long long)(size - kept);
    }
    if (found < 0) {
        std::fprintf(stderr, "%s has no Electron fuses\n", argv[1]);
        return 1;
    }
    file.clear();
    file.seekg(found + (long long)markerSize);
    char header[2] = {};
    file.read(header, 2);
    if (header[0] != 1) {
        std::fprintf(stderr, "unknown Electron fuse version %d\n", header[0]);
        return 1;
    }
    std::string wires(size_t((unsigned char)header[1]), '\0');
    file.read(wires.data(), std::streamsize(wires.size()));
    for (int i = 2; i < argc; i++) {
        unsigned index = 0;
        char value = 0;
        if (std::sscanf(argv[i], "%u=%c", &index, &value) != 2 || (value != '0' && value != '1') || index >= wires.size()) {
            std::fprintf(stderr, "bad fuse setting %s (%zu fuses)\n", argv[i], wires.size());
            return 2;
        }
        if (wires[index] != '0' && wires[index] != '1') {
            std::fprintf(stderr, "fuse %u was removed from this Electron\n", index);
            return 1;
        }
        wires[index] = value;
    }
    file.seekp(found + (long long)markerSize + 2);
    file.write(wires.data(), std::streamsize(wires.size()));
    file.flush();
    if (!file) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("fuses: %s\n", wires.c_str());
    return 0;
}
