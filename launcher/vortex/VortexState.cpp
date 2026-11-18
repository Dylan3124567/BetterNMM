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

#include "VortexState.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#endif

namespace VortexState {

namespace {

// Plain Win32 (or POSIX) and stdio rather than std::filesystem and iostreams: setup links this statically, and those
// would pull in the C++ locale machinery, about a megabyte.

void appendUtf8(std::string& out, uint32_t cp);

std::string utf8(const std::wstring& text)
{
#ifdef _WIN32
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(size, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
#else
    std::string out;
    for (const wchar_t c : text) {
        appendUtf8(out, static_cast<uint32_t>(c));
    }
    return out;
#endif
}

std::wstring join(const std::wstring& dir, const std::wstring& name)
{
#ifdef _WIN32
    constexpr wchar_t separator = L'\\';
#else
    constexpr wchar_t separator = L'/';
#endif
    return dir.empty() || dir.back() == L'/' || dir.back() == separator ? dir + name : dir + separator + name;
}

std::FILE* openFile(const std::wstring& path)
{
#ifdef _WIN32
    return _wfopen(path.c_str(), L"rb");
#else
    return std::fopen(utf8(path).c_str(), "rb");
#endif
}

bool fileExists(const std::wstring& path)
{
    if (std::FILE* file = openFile(path)) {
        std::fclose(file);
        return true;
    }
    return false;
}

/// The names of the files in @p dir ending in @p extension (".log")
std::vector<std::wstring> filesEndingIn(const std::wstring& dir, const std::wstring& extension)
{
    std::vector<std::wstring> out;
    auto keep = [&](const std::wstring& name) {
        if (name.size() > extension.size() && name.compare(name.size() - extension.size(), extension.size(), extension) == 0) {
            out.push_back(name);
        }
    };
#ifdef _WIN32
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(join(dir, L"*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return out;
    }
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            keep(data.cFileName);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
#else
    if (DIR* handle = opendir(utf8(dir).c_str())) {
        while (const dirent* entry = readdir(handle)) {
            std::wstring name;
            for (const char* c = entry->d_name; *c; c++) {
                name += static_cast<wchar_t>(static_cast<unsigned char>(*c));  // LevelDB's own names are ASCII
            }
            keep(name);
        }
        closedir(handle);
    }
#endif
    return out;
}

bool readFile(const std::wstring& path, std::string& out)
{
    std::FILE* file = openFile(path);
    if (!file) {
        return false;
    }
    out.clear();
    char buffer[65536];
    for (size_t n; (n = std::fread(buffer, 1, sizeof(buffer), file)) > 0;) {
        out.append(buffer, n);
    }
    const bool ok = !std::ferror(file);
    std::fclose(file);
    return ok;
}

uint32_t le32(const char* p)
{
    const auto* u = reinterpret_cast<const unsigned char*>(p);
    return u[0] | (u[1] << 8) | (u[2] << 16) | (static_cast<uint32_t>(u[3]) << 24);
}

uint64_t le64(const char* p)
{
    return le32(p) | (static_cast<uint64_t>(le32(p + 4)) << 32);
}

/// Reads from a byte range, failing (instead of reading past it) on malformed input
struct Reader {
    const char* p;
    const char* end;

    bool varint(uint64_t& value)
    {
        value = 0;
        for (int shift = 0; shift <= 63 && p < end; shift += 7) {
            const auto byte = static_cast<unsigned char>(*p++);
            value |= static_cast<uint64_t>(byte & 0x7f) << shift;
            if (!(byte & 0x80)) {
                return true;
            }
        }
        return false;
    }
    bool bytes(size_t n, std::string& out)
    {
        if (static_cast<size_t>(end - p) < n) {
            return false;
        }
        out.assign(p, n);
        p += n;
        return true;
    }
    bool prefixed(std::string& out)
    {
        uint64_t n = 0;
        return varint(n) && bytes(n, out);
    }
    bool done() const { return p >= end; }
};

uint32_t unmask(uint32_t masked)
{
    const uint32_t rot = masked - 0xa282ead8u;
    return (rot >> 17) | (rot << 15);
}

/// The records of a LevelDB log file (the MANIFEST and the write-ahead logs share the format)
std::vector<std::string> logRecords(const std::string& file)
{
    constexpr size_t block = 32768;
    constexpr size_t header = 7;
    enum { Zero = 0, Full = 1, First = 2, Middle = 3, Last = 4 };
    std::vector<std::string> records;
    std::string pending;
    bool inRecord = false;
    size_t pos = 0;
    while (pos < file.size()) {
        const size_t blockEnd = std::min(file.size(), (pos / block + 1) * block);
        if (blockEnd - pos < header) {
            pos = blockEnd;  // the trailer of a block is padding
            continue;
        }
        const char* h = file.data() + pos;
        const uint32_t crc = le32(h);
        const size_t length = static_cast<unsigned char>(h[4]) | (static_cast<unsigned char>(h[5]) << 8);
        const int type = static_cast<unsigned char>(h[6]);
        if (type == Zero && length == 0) {
            pos = blockEnd;  // preallocated space
            continue;
        }
        if (pos + header + length > blockEnd || crc32c(h + 6, 1 + length) != unmask(crc)) {
            // a record torn by a crash: drop it and what was being assembled, go on with the next block
            pending.clear();
            inRecord = false;
            pos = blockEnd;
            continue;
        }
        const char* data = h + header;
        pos += header + length;
        switch (type) {
            case Full:
                records.emplace_back(data, length);
                pending.clear();
                inRecord = false;
                break;
            case First:
                pending.assign(data, length);
                inRecord = true;
                break;
            case Middle:
                if (inRecord) {
                    pending.append(data, length);
                }
                break;
            case Last:
                if (inRecord) {
                    pending.append(data, length);
                    records.push_back(std::move(pending));
                    pending.clear();
                }
                inRecord = false;
                break;
            default:
                pending.clear();
                inRecord = false;
                break;
        }
    }
    return records;
}

struct Value {
    uint64_t sequence = 0;
    bool deleted = false;
    std::string value;
};
using Merged = std::map<std::string, Value>;

void put(Merged& merged, std::string key, uint64_t sequence, bool deleted, std::string value)
{
    auto it = merged.find(key);
    if (it == merged.end()) {
        merged.emplace(std::move(key), Value{ sequence, deleted, std::move(value) });
    } else if (sequence >= it->second.sequence) {
        it->second = Value{ sequence, deleted, std::move(value) };
    }
}

bool applyBatch(const std::string& batch, Merged& merged)
{
    if (batch.size() < 12) {
        return false;
    }
    uint64_t sequence = le64(batch.data());
    const uint32_t count = le32(batch.data() + 8);
    Reader in{ batch.data() + 12, batch.data() + batch.size() };
    for (uint32_t i = 0; i < count; i++, sequence++) {
        if (in.done()) {
            return false;
        }
        const char tag = *in.p++;
        std::string key;
        std::string value;
        if (tag == 1) {
            if (!in.prefixed(key) || !in.prefixed(value)) {
                return false;
            }
            put(merged, std::move(key), sequence, false, std::move(value));
        } else if (tag == 0) {
            if (!in.prefixed(key)) {
                return false;
            }
            put(merged, std::move(key), sequence, true, {});
        } else {
            return false;
        }
    }
    return true;
}

/// The entries of a table block, prefix-compressed with a restart array at the end
bool blockEntries(const std::string& block, std::vector<std::pair<std::string, std::string>>& out)
{
    if (block.size() < 4) {
        return false;
    }
    const uint32_t restarts = le32(block.data() + block.size() - 4);
    if (restarts > (block.size() - 4) / 4) {
        return false;
    }
    Reader in{ block.data(), block.data() + block.size() - 4 - 4 * static_cast<size_t>(restarts) };
    std::string key;
    while (!in.done()) {
        uint64_t shared = 0;
        uint64_t unshared = 0;
        uint64_t valueLength = 0;
        std::string tail;
        std::string value;
        if (!in.varint(shared) || !in.varint(unshared) || !in.varint(valueLength) || shared > key.size() || !in.bytes(unshared, tail) ||
            !in.bytes(valueLength, value)) {
            return false;
        }
        key.resize(shared);
        key += tail;
        out.emplace_back(key, std::move(value));
    }
    return true;
}

bool readBlock(const std::string& file, uint64_t offset, uint64_t size, std::string& out)
{
    if (offset > file.size() || size + 5 > file.size() - offset) {
        return false;
    }
    const char* data = file.data() + offset;
    if (crc32c(data, size + 1) != unmask(le32(data + size + 1))) {
        return false;
    }
    switch (data[size]) {
        case 0:
            out.assign(data, size);
            return true;
        case 1:
            return snappyUncompress(data, size, out);
        default:
            return false;  // zstd and others: LevelDB as Vortex uses it only writes Snappy
    }
}

bool readTable(const std::string& file, Merged& merged)
{
    constexpr size_t footer = 48;
    constexpr uint64_t magic = 0xdb4775248b80fb57ull;
    if (file.size() < footer || le64(file.data() + file.size() - 8) != magic) {
        return false;
    }
    Reader handles{ file.data() + file.size() - footer, file.data() + file.size() - 8 };
    uint64_t metaOffset = 0;
    uint64_t metaSize = 0;
    uint64_t indexOffset = 0;
    uint64_t indexSize = 0;
    std::string index;
    std::vector<std::pair<std::string, std::string>> indexEntries;
    if (!handles.varint(metaOffset) || !handles.varint(metaSize) || !handles.varint(indexOffset) || !handles.varint(indexSize) ||
        !readBlock(file, indexOffset, indexSize, index) || !blockEntries(index, indexEntries)) {
        return false;
    }
    for (const auto& [lastKey, handle] : indexEntries) {
        Reader h{ handle.data(), handle.data() + handle.size() };
        uint64_t offset = 0;
        uint64_t size = 0;
        std::string block;
        std::vector<std::pair<std::string, std::string>> entries;
        if (!h.varint(offset) || !h.varint(size) || !readBlock(file, offset, size, block) || !blockEntries(block, entries)) {
            return false;
        }
        for (auto& [internalKey, value] : entries) {
            if (internalKey.size() < 8) {
                return false;
            }
            const uint64_t tag = le64(internalKey.data() + internalKey.size() - 8);
            internalKey.resize(internalKey.size() - 8);
            put(merged, std::move(internalKey), tag >> 8, (tag & 0xff) == 0, std::move(value));
        }
    }
    return true;
}

struct Manifest {
    uint64_t logNumber = 0;
    uint64_t prevLogNumber = 0;
    std::set<uint64_t> tables;
};

bool readManifest(const std::string& file, Manifest& manifest)
{
    std::set<uint64_t> added;
    std::set<uint64_t> deleted;
    for (const auto& record : logRecords(file)) {
        Reader in{ record.data(), record.data() + record.size() };
        while (!in.done()) {
            uint64_t tag = 0;
            uint64_t level = 0;
            uint64_t number = 0;
            uint64_t ignored = 0;
            std::string text;
            if (!in.varint(tag)) {
                return false;
            }
            bool ok = true;
            switch (tag) {
                case 1:  // comparator
                    ok = in.prefixed(text);
                    break;
                case 2:
                    ok = in.varint(manifest.logNumber);
                    break;
                case 3:  // next file number
                case 4:  // last sequence
                    ok = in.varint(ignored);
                    break;
                case 5:  // compaction pointer
                    ok = in.varint(level) && in.prefixed(text);
                    break;
                case 6:
                    ok = in.varint(level) && in.varint(number);
                    deleted.insert(number);
                    break;
                case 7:
                    ok = in.varint(level) && in.varint(number) && in.varint(ignored) && in.prefixed(text) && in.prefixed(text);
                    added.insert(number);
                    break;
                case 9:
                    ok = in.varint(manifest.prevLogNumber);
                    break;
                default:
                    ok = false;
                    break;
            }
            if (!ok) {
                return false;
            }
        }
    }
    for (const auto number : added) {
        if (!deleted.count(number)) {
            manifest.tables.insert(number);
        }
    }
    return true;
}

std::wstring numbered(const std::wstring& dir, uint64_t number, const wchar_t* extension)
{
    // LevelDB names its files with at least six digits: 000005.ldb
    auto name = std::to_wstring(number);
    if (name.size() < 6) {
        name.insert(0, 6 - name.size(), L'0');
    }
    return join(dir, name + L'.' + extension);
}

void appendUtf8(std::string& out, uint32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xc0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xe0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    } else {
        out += static_cast<char>(0xf0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    }
}

bool hex4(const std::string& s, size_t at, uint32_t& value)
{
    if (at + 4 > s.size()) {
        return false;
    }
    value = 0;
    for (size_t i = at; i < at + 4; i++) {
        const char c = s[i];
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= static_cast<uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value |= static_cast<uint32_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            value |= static_cast<uint32_t>(c - 'A' + 10);
        } else {
            return false;
        }
    }
    return true;
}

}  // namespace

std::wstring defaultDatabase()
{
#ifdef _WIN32
    if (const wchar_t* appData = _wgetenv(L"APPDATA"); appData && *appData) {
        return join(join(appData, L"Vortex"), L"state.v2");
    }
#endif
    return {};
}

uint32_t crc32c(const char* data, size_t size, uint32_t crc)
{
    static const auto table = [] {
        std::vector<uint32_t> t(256);
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? (c >> 1) ^ 0x82f63b78u : c >> 1;
            }
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < size; i++) {
        crc = table[(crc ^ static_cast<unsigned char>(data[i])) & 0xff] ^ (crc >> 8);
    }
    return ~crc;
}

bool snappyUncompress(const char* data, size_t size, std::string& out)
{
    Reader in{ data, data + size };
    uint64_t length = 0;
    // a table block is at most a few MiB; a bigger claim is a corrupt length
    if (!in.varint(length) || length > (size_t(1) << 30)) {
        return false;
    }
    out.clear();
    out.reserve(length);
    while (!in.done()) {
        const auto tag = static_cast<unsigned char>(*in.p++);
        size_t len = 0;
        size_t offset = 0;
        switch (tag & 3) {
            case 0: {  // literal
                len = (tag >> 2) + 1;
                if (len > 60) {
                    const size_t bytes = len - 60;
                    if (static_cast<size_t>(in.end - in.p) < bytes) {
                        return false;
                    }
                    len = 0;
                    for (size_t i = 0; i < bytes; i++) {
                        len |= static_cast<size_t>(static_cast<unsigned char>(in.p[i])) << (8 * i);
                    }
                    len += 1;
                    in.p += bytes;
                }
                if (static_cast<size_t>(in.end - in.p) < len || out.size() + len > length) {
                    return false;
                }
                out.append(in.p, len);
                in.p += len;
                continue;
            }
            case 1:
                if (in.done()) {
                    return false;
                }
                len = ((tag >> 2) & 7) + 4;
                offset = (static_cast<size_t>(tag >> 5) << 8) | static_cast<unsigned char>(*in.p++);
                break;
            case 2:
                if (in.end - in.p < 2) {
                    return false;
                }
                len = (tag >> 2) + 1;
                offset = static_cast<unsigned char>(in.p[0]) | (static_cast<size_t>(static_cast<unsigned char>(in.p[1])) << 8);
                in.p += 2;
                break;
            default:
                if (in.end - in.p < 4) {
                    return false;
                }
                len = (tag >> 2) + 1;
                offset = le32(in.p);
                in.p += 4;
                break;
        }
        if (offset == 0 || offset > out.size() || out.size() + len > length) {
            return false;
        }
        // copies may overlap what they produce (runs), so byte by byte
        const size_t from = out.size() - offset;
        for (size_t i = 0; i < len; i++) {
            out += out[from + i];
        }
    }
    return out.size() == length;
}

bool readDatabase(const std::wstring& dir, std::map<std::string, std::string>& entries, std::string& error)
{
    std::string current;
    if (!readFile(join(dir, L"CURRENT"), current)) {
        error = "There is no Vortex database in " + utf8(dir) + ".";
        return false;
    }
    while (!current.empty() && (current.back() == '\n' || current.back() == '\r')) {
        current.pop_back();
    }
    std::string manifestFile;
    Manifest manifest;
    std::wstring manifestName;
    for (const char c : current) {
        manifestName += static_cast<wchar_t>(static_cast<unsigned char>(c));  // MANIFEST-000004: ASCII
    }
    if (current.find_first_of("/\\") != std::string::npos || !readFile(join(dir, manifestName), manifestFile) ||
        !readManifest(manifestFile, manifest)) {
        error = "Vortex's database in " + utf8(dir) + " is damaged (its MANIFEST can not be read).";
        return false;
    }
    Merged merged;
    for (const auto number : manifest.tables) {
        std::string table;
        auto path = numbered(dir, number, L"ldb");
        if (!fileExists(path)) {
            path = numbered(dir, number, L"sst");
        }
        if (!readFile(path, table)) {
            error = "Vortex's database file " + utf8(path) + " can not be read. If Vortex is running, close it and try again.";
            return false;
        }
        if (!readTable(table, merged)) {
            error = "Vortex's database file " + utf8(path) + " is damaged.";
            return false;
        }
    }
    // the write-ahead logs hold what was written after the tables, oldest first
    std::vector<uint64_t> logs;
    for (const auto& name : filesEndingIn(dir, L".log")) {
        wchar_t* end = nullptr;
        const auto number = std::wcstoull(name.c_str(), &end, 10);
        if (end == name.c_str() + name.size() - 4 && (number >= manifest.logNumber || number == manifest.prevLogNumber)) {
            logs.push_back(number);
        }
    }
    std::sort(logs.begin(), logs.end());
    for (const auto number : logs) {
        std::string log;
        const auto path = numbered(dir, number, L"log");
        if (!readFile(path, log)) {
            error = "Vortex's database file " + utf8(path) + " can not be read. If Vortex is running, close it and try again.";
            return false;
        }
        for (const auto& batch : logRecords(log)) {
            applyBatch(batch, merged);
        }
    }
    entries.clear();
    for (auto& [key, value] : merged) {
        if (!value.deleted) {
            entries.emplace(key, std::move(value.value));
        }
    }
    return true;
}

std::vector<std::string> splitKey(const std::string& key)
{
    std::vector<std::string> parts;
    const std::string separator = k_separator;
    size_t start = 0;
    for (size_t at = key.find(separator); at != std::string::npos; at = key.find(separator, start)) {
        parts.push_back(key.substr(start, at - start));
        start = at + separator.size();
    }
    parts.push_back(key.substr(start));
    return parts;
}

std::optional<std::string> jsonString(const std::string& json)
{
    if (json.size() < 2 || json.front() != '"' || json.back() != '"') {
        return std::nullopt;
    }
    std::string out;
    for (size_t i = 1; i + 1 < json.size(); i++) {
        const char c = json[i];
        if (c != '\\') {
            out += c;
            continue;
        }
        if (++i + 1 > json.size() - 1) {
            return std::nullopt;
        }
        switch (json[i]) {
            case '"':
            case '\\':
            case '/':
                out += json[i];
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                uint32_t cp = 0;
                if (!hex4(json, i + 1, cp)) {
                    return std::nullopt;
                }
                i += 4;
                uint32_t low = 0;
                if (cp >= 0xd800 && cp < 0xdc00 && i + 6 < json.size() && json[i + 1] == '\\' && json[i + 2] == 'u' &&
                    hex4(json, i + 3, low) && low >= 0xdc00 && low < 0xe000) {
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    i += 6;
                }
                appendUtf8(out, cp);
                break;
            }
            default:
                return std::nullopt;
        }
    }
    return out;
}

const std::vector<KnownGame>& knownGames()
{
    // Vortex's own game ids differ from the Nexus Mods domains for a few games
    static const std::vector<KnownGame> games = {
        { "skyrimse", "skyrimspecialedition", "Skyrim Special Edition" },
        { "skyrim", "skyrim", "Skyrim" },
        { "fallout4", "fallout4", "Fallout 4" },
        { "falloutnv", "newvegas", "Fallout: New Vegas" },
        { "fallout3", "fallout3", "Fallout 3" },
        { "oblivion", "oblivion", "Oblivion" },
        { "morrowind", "morrowind", "Morrowind" },
        { "starfield", "starfield", "Starfield" },
        { "cyberpunk2077", "cyberpunk2077", "Cyberpunk 2077" },
        { "witcher3", "witcher3", "The Witcher 3" },
        { "stardewvalley", "stardewvalley", "Stardew Valley" },
        { "mountandblade2bannerlord", "mountandblade2bannerlord", "Mount & Blade II: Bannerlord" },
        { "darksouls3", "darksouls3", "Dark Souls III" },
        { "eldenring", "eldenring", "Elden Ring" },
    };
    return games;
}

const KnownGame* gameByVortexId(const std::string& id)
{
    for (const auto& game : knownGames()) {
        if (id == game.vortexId) {
            return &game;
        }
    }
    return nullptr;
}

std::vector<Profile> profiles(const std::map<std::string, std::string>& entries)
{
    std::map<std::string, Profile> byId;
    const std::string prefix = std::string("persistent") + k_separator + "profiles" + k_separator;
    for (auto it = entries.lower_bound(prefix); it != entries.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it) {
        const auto parts = splitKey(it->first);
        if (parts.size() < 4) {
            continue;
        }
        auto& profile = byId[parts[2]];
        profile.id = parts[2];
        const auto& field = parts[3];
        if (parts.size() == 4 && field == "name") {
            profile.name = jsonString(it->second).value_or(std::string());
        } else if (parts.size() == 4 && field == "gameId") {
            profile.gameId = jsonString(it->second).value_or(std::string());
        } else if (parts.size() == 4 && field == "lastActivated") {
            profile.lastActivated = std::strtod(it->second.c_str(), nullptr);
        } else if (parts.size() == 6 && field == "modState" && parts[5] == "enabled" && it->second == "true") {
            profile.enabledMods++;
        }
    }
    std::vector<Profile> out;
    for (auto& [id, profile] : byId) {
        if (gameByVortexId(profile.gameId)) {
            out.push_back(std::move(profile));
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Profile& a, const Profile& b) { return a.lastActivated > b.lastActivated; });
    return out;
}

}  // namespace VortexState
