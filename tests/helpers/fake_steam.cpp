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

// Stands in for steam.exe in the tests: after a moment it reports itself running and logged in where the tests' Steam
// key says (HKEY_CURRENT_USER\Software\BetterNMM-tests-steam\ActiveProcess), then stays alive for a while like Steam
// does. A copy whose name has "nologin" in it reports itself running but never logs in.

#include <windows.h>

#include <cwchar>

int wmain(int argc, wchar_t** argv)
{
    const bool login = argc < 1 || std::wcsstr(argv[0], L"nologin") == nullptr;
    Sleep(700);
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\BetterNMM-tests-steam\\ActiveProcess", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return 2;
    }
    const DWORD pid = GetCurrentProcessId();
    const DWORD user = login ? 12345 : 0;
    RegSetValueExW(key, L"pid", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&pid), sizeof(pid));
    RegSetValueExW(key, L"ActiveUser", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&user), sizeof(user));
    RegCloseKey(key);
    Sleep(20000);
    return 0;
}
