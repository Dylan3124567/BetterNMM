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

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace Setup {

constexpr const wchar_t* k_appName = L"BetterNMM";
constexpr const wchar_t* k_publisher = L"BetterNMM Contributors";
/// The app's AppUserModelID: its Start menu shortcut carries it and the app sets it, so pins and windows match
constexpr const wchar_t* k_appId = L"io.github.betternmm.BetterNMM";
constexpr const wchar_t* k_exe = L"betternmm.exe";
constexpr const wchar_t* k_uninstaller = L"uninstall.exe";
/// The list of files setup put in the install folder; only those are ever removed
constexpr const wchar_t* k_fileList = L".betternmm-files";

enum class Scope { User, Machine };

/// Where setup puts things outside the install folder. A test root redirects every one of them (registry keys under
/// HKCU\Software\BetterNMM-SetupTest, shortcuts below the root, the Vortex profiles left for BetterNMM in <root>\BetterNMM)
/// and turns off elevation, pinning and launching. Vortex's database is read from <root>\Vortex\state.v2 when there is one.
struct Locations {
    std::wstring testRoot;

    HKEY uninstallRoot(Scope scope) const;
    std::wstring uninstallKey(Scope scope) const;
    std::wstring startMenuDir(Scope scope) const;
    std::wstring desktopDir(Scope scope) const;
    std::wstring classesKey() const;  ///< below HKCU
    bool testing() const { return !testRoot.empty(); }
};

struct Options {
    bool desktop = true;
    bool startMenu = true;
    bool pin = true;
    bool nxm = false;
    bool launch = true;

    std::wstring toString() const;
    static Options fromString(const std::wstring& text, Options defaults);
};

struct Existing {
    bool found = false;
    Scope scope = Scope::User;
    std::wstring dir;
    std::wstring version;
    Options options;
};

/// The installed BetterNMM, per-user first
Existing findExisting(const Locations& where);
/// Where a new install goes: below the test root when testing
std::wstring defaultDir(Scope scope, const Locations& where);

struct Plan {
    std::wstring dir;
    Scope scope = Scope::User;
    Options options;
    Locations where;
    std::wstring setupExe;  ///< the program carrying the files
};

using Progress = std::function<void(double fraction, const std::wstring& status)>;

/// What needs the rights of the scope: the files (staged, swapped in, rolled back on failure), the uninstaller, the Apps
/// & Features entry, the Start menu and desktop shortcuts
bool installFiles(const Plan& plan, const Progress& progress, std::wstring& error);
/// What belongs to the person running setup: the nxm:// link handler
bool installForUser(const Plan& plan, std::wstring& error);
/// Asks Windows to pin BetterNMM to the taskbar; Windows asks the person. @p outcome says what happened.
bool requestPin(const Plan& plan, std::wstring& outcome);
bool launch(const Plan& plan);

bool uninstallFiles(const Existing& existing, const Locations& where, const Progress& progress, std::wstring& error);
bool uninstallForUser(const Existing& existing, const Locations& where);

/// Programs running from @p dir (file names), besides this one and @p ignore
std::vector<std::wstring> runningFrom(const std::wstring& dir, DWORD ignore = 0);
/// Deletes @p file and then the folder @p dir if it is empty, a few seconds after this program has exited
void deleteLater(const std::wstring& file, const std::wstring& dir = std::wstring());
bool isElevated();
std::wstring formatSize(unsigned long long bytes);
/// %TEMP%\BetterNMM-Setup.log
void log(const std::wstring& line);

}  // namespace Setup
