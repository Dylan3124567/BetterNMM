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

#include "Files.h"

#include <algorithm>
#include <cwctype>

namespace Files {

std::string utf8(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring wide(const std::string& text)
{
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::wstring lower(std::wstring text)
{
    CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
    return text;
}

std::wstring systemMessage(DWORD error)
{
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
                   reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring out = buffer ? buffer : L"error " + std::to_wstring(error);
    LocalFree(buffer);
    while (!out.empty() && (out.back() == L'\n' || out.back() == L'\r' || out.back() == L' ' || out.back() == L'.')) {
        out.pop_back();
    }
    return out;
}

bool readAll(const std::wstring& path, std::string& out)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(file, &size) != 0;
    if (ok) {
        out.resize(static_cast<size_t>(size.QuadPart));
        size_t at = 0;
        while (ok && at < out.size()) {
            DWORD got = 0;
            ok = ReadFile(file, out.data() + at, static_cast<DWORD>(std::min<size_t>(out.size() - at, 1u << 30)), &got, nullptr) && got > 0;
            at += got;
        }
    }
    CloseHandle(file);
    return ok;
}

bool writeAll(const std::wstring& path, const std::string& data)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    size_t at = 0;
    bool ok = true;
    while (ok && at < data.size()) {
        DWORD put = 0;
        ok = WriteFile(file, data.data() + at, static_cast<DWORD>(std::min<size_t>(data.size() - at, 1u << 30)), &put, nullptr) && put > 0;
        at += put;
    }
    CloseHandle(file);
    return ok;
}

bool exists(const std::wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool isDirectory(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring parent(const std::wstring& path)
{
    const auto at = path.find_last_of(L"\\/");
    return at == std::wstring::npos ? std::wstring() : path.substr(0, at);
}

bool makeDirs(const std::wstring& dir)
{
    if (dir.empty() || isDirectory(dir)) {
        return true;
    }
    const auto up = parent(dir);
    // a drive root ("C:") has no parent to make
    if (!up.empty() && up != dir && !(up.size() == 2 && up[1] == L':') && !makeDirs(up)) {
        return false;
    }
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool makeParent(const std::wstring& file)
{
    return makeDirs(parent(file));
}

bool removeTree(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        if (attributes & FILE_ATTRIBUTE_READONLY) {
            SetFileAttributesW(path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
        }
        return DeleteFileW(path.c_str()) != 0;
    }
    bool ok = true;
    if (!(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileW((path + L"\\*").c_str(), &data);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring name = data.cFileName;
                if (name != L"." && name != L"..") {
                    ok = removeTree(path + L"\\" + name) && ok;
                }
            } while (FindNextFileW(find, &data));
            FindClose(find);
        }
    }
    return RemoveDirectoryW(path.c_str()) != 0 && ok;
}

void pruneEmpty(std::wstring dir, const std::wstring& stop)
{
    while (!dir.empty() && lower(dir) != lower(stop) && dir.size() > stop.size()) {
        if (!RemoveDirectoryW(dir.c_str())) {
            return;
        }
        dir = parent(dir);
    }
}

void list(const std::wstring& root, std::vector<std::wstring>& out, const std::wstring& rel)
{
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((rel.empty() ? root + L"\\*" : root + L"\\" + rel + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        const auto path = rel.empty() ? name : rel + L"\\" + name;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            list(root, out, path);
        } else {
            out.push_back(path);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

unsigned long long freeSpace(const std::wstring& path)
{
    std::wstring dir = path;
    while (!dir.empty() && !isDirectory(dir)) {
        dir = parent(dir);
    }
    ULARGE_INTEGER available;
    if (dir.empty() || !GetDiskFreeSpaceExW((dir + L"\\").c_str(), &available, nullptr, nullptr)) {
        return 0;
    }
    return available.QuadPart;
}

}  // namespace Files
