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

// Stands in for a game in the tests. It asks Windows for the folders a game asks for (the way Bethesda games and script
// extenders do) and writes them to a file. "--spawn" behaves like a script extender loader: it starts the real "game"
// (itself with --print) and exits at once.

#include <windows.h>

#include <shlobj.h>

#include <cstdio>
#include <string>

static std::wstring knownFolder(REFKNOWNFOLDERID id)
{
    PWSTR path = nullptr;
    std::wstring out = L"<error>";
    if (SHGetKnownFolderPath(id, 0, nullptr, &path) == S_OK) {
        out = path;
    }
    CoTaskMemFree(path);
    return out;
}

static std::wstring folderPath(int csidl)
{
    wchar_t buffer[MAX_PATH] = {};
    return SHGetFolderPathW(nullptr, csidl, nullptr, 0, buffer) == S_OK ? std::wstring(buffer) : std::wstring(L"<error>");
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) {
        return 2;
    }
    const std::wstring mode = argv[1];
    const std::wstring out = argv[2];
    if (mode == L"--crash") {
        // crash the way a game does, without Windows' "stopped working" window
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        Sleep(500);
        *static_cast<volatile int*>(nullptr) = 1;
        return 5;
    }
    if (mode == L"--spawn" || mode == L"--spawn-crash") {
        const std::wstring child = mode == L"--spawn" ? L" --print \"" : L" --crash \"";
        std::wstring command = L"\"" + std::wstring(argv[0]) + L"\"" + child + out + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
            return 3;
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 0;
    }
    // stay alive a while, like a game would, so the launcher has to wait for this child
    Sleep(1500);
    FILE* file = _wfopen(out.c_str(), L"w, ccs=UTF-8");
    if (!file) {
        return 4;
    }
    fwprintf(file, L"documents=%ls\n", knownFolder(FOLDERID_Documents).c_str());
    fwprintf(file, L"localappdata=%ls\n", knownFolder(FOLDERID_LocalAppData).c_str());
    fwprintf(file, L"roamingappdata=%ls\n", knownFolder(FOLDERID_RoamingAppData).c_str());
    fwprintf(file, L"savedgames=%ls\n", knownFolder(FOLDERID_SavedGames).c_str());
    fwprintf(file, L"personal=%ls\n", folderPath(CSIDL_PERSONAL).c_str());
    fwprintf(file, L"steamappid=%ls\n", _wgetenv(L"SteamAppId") ? _wgetenv(L"SteamAppId") : L"");
    fclose(file);
    return 0;
}
