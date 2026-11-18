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

// Small file and text helpers on the Windows API, shared by setup and its packer

#include <windows.h>

#include <string>
#include <vector>

namespace Files {

std::string utf8(const std::wstring& text);
std::wstring wide(const std::string& text);
std::wstring lower(std::wstring text);
/// Windows' own wording for an error code, without the trailing line break
std::wstring systemMessage(DWORD error);

bool readAll(const std::wstring& path, std::string& out);
bool writeAll(const std::wstring& path, const std::string& data);
bool exists(const std::wstring& path);
bool isDirectory(const std::wstring& path);
/// Creates every missing folder of @p dir
bool makeDirs(const std::wstring& dir);
/// Creates the folder @p file goes in
bool makeParent(const std::wstring& file);
std::wstring parent(const std::wstring& path);
/// Deletes a folder and everything in it; links and junctions are removed, never followed
bool removeTree(const std::wstring& path);
/// Removes @p dir and its parents up to @p stop while they are empty
void pruneEmpty(std::wstring dir, const std::wstring& stop);
/// Every file below @p root, relative and '\' separated
void list(const std::wstring& root, std::vector<std::wstring>& out, const std::wstring& rel = std::wstring());
/// Bytes free on the drive of @p path (or of its nearest existing parent)
unsigned long long freeSpace(const std::wstring& path);

}  // namespace Files
