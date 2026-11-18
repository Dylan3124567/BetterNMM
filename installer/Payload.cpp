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

#include "Payload.h"

#include <windows.h>

#include <compressapi.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <type_traits>

#include "Files.h"

namespace Payload {

namespace {

constexpr char k_payloadMagic[8] = { 'B', 'N', 'M', 'M', 'P', 'A', 'K', '1' };
constexpr char k_footerMagic[8] = { 'B', 'N', 'M', 'M', 'E', 'N', 'D', '1' };
constexpr uint32_t k_chunkSize = 32u * 1024 * 1024;

std::wstring lastError(const std::wstring& what)
{
    return what + L": " + Files::systemMessage(GetLastError());
}

struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE handle) : h(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle()
    {
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
    bool ok() const { return h != INVALID_HANDLE_VALUE; }
    void reset(HANDLE handle)
    {
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
        h = handle;
    }
};

bool readAt(HANDLE file, uint64_t offset, void* buffer, size_t size)
{
    LARGE_INTEGER at;
    at.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file, at, nullptr, FILE_BEGIN)) {
        return false;
    }
    auto* out = static_cast<uint8_t*>(buffer);
    while (size > 0) {
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(std::min<size_t>(size, 1u << 30));
        if (!ReadFile(file, out, want, &got, nullptr) || got == 0) {
            return false;
        }
        out += got;
        size -= got;
    }
    return true;
}

bool writeAll(HANDLE file, const void* data, size_t size)
{
    const auto* in = static_cast<const uint8_t*>(data);
    while (size > 0) {
        DWORD put = 0;
        const DWORD want = static_cast<DWORD>(std::min<size_t>(size, 1u << 30));
        if (!WriteFile(file, in, want, &put, nullptr) || put == 0) {
            return false;
        }
        in += put;
        size -= put;
    }
    return true;
}

/// A little-endian byte buffer, for the file and chunk tables
struct Buffer {
    std::string bytes;
    size_t at = 0;

    void u16(uint16_t v) { bytes.append(reinterpret_cast<const char*>(&v), 2); }
    void u32(uint32_t v) { bytes.append(reinterpret_cast<const char*>(&v), 4); }
    void u64(uint64_t v) { bytes.append(reinterpret_cast<const char*>(&v), 8); }
    void str(const std::wstring& s)
    {
        const auto utf8 = Files::utf8(s);
        u16(static_cast<uint16_t>(utf8.size()));
        bytes += utf8;
    }

    bool take(void* out, size_t n)
    {
        if (at + n > bytes.size()) {
            return false;
        }
        std::memcpy(out, bytes.data() + at, n);
        at += n;
        return true;
    }
    bool get(uint16_t& v) { return take(&v, 2); }
    bool get(uint32_t& v) { return take(&v, 4); }
    bool get(uint64_t& v) { return take(&v, 8); }
    bool get(std::wstring& s)
    {
        uint16_t n = 0;
        if (!get(n) || at + n > bytes.size()) {
            return false;
        }
        s = Files::wide(bytes.substr(at, n));
        at += n;
        return true;
    }
};

void listFiles(const std::wstring& root, const std::wstring& rel, std::vector<File>& out)
{
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((root + L"\\" + rel + (rel.empty() ? L"*" : L"\\*")).c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        const auto path = rel.empty() ? name : rel + L"\\" + name;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            listFiles(root, path, out);
        } else {
            File file;
            file.path = path;
            file.size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            file.time = (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
            out.push_back(file);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc)
{
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < size; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

bool write(const std::wstring& stub,
           const std::wstring& stageDir,
           const std::wstring& out,
           const std::wstring& version,
           std::wstring& error)
{
    std::string stubBytes;
    if (!Files::readAll(stub, stubBytes)) {
        error = lastError(L"Could not read " + stub);
        return false;
    }
    Index index;
    index.version = version;
    listFiles(stageDir, L"", index.files);
    if (index.files.empty()) {
        error = L"There are no files in " + stageDir;
        return false;
    }
    std::sort(index.files.begin(), index.files.end(),
              [](const File& a, const File& b) { return Files::lower(a.path) < Files::lower(b.path); });

    Handle file(CreateFileW(out.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.ok()) {
        error = lastError(L"Could not create " + out);
        return false;
    }
    uint64_t indexOffset = 0;
    if (!writeAll(file.h, stubBytes.data(), stubBytes.size()) || !writeAll(file.h, k_payloadMagic, 8) ||
        !writeAll(file.h, &indexOffset, 8)) {
        error = lastError(L"Could not write " + out);
        return false;
    }
    uint64_t position = stubBytes.size() + 16;

    COMPRESSOR_HANDLE compressor = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &compressor)) {
        error = lastError(L"Windows' compressor is not available");
        return false;
    }
    std::unique_ptr<std::remove_pointer_t<COMPRESSOR_HANDLE>, decltype(&CloseCompressor)> closeCompressor(compressor, &CloseCompressor);
    std::vector<uint8_t> raw;
    raw.reserve(k_chunkSize);
    std::vector<uint8_t> packed;
    auto flush = [&]() -> bool {
        if (raw.empty()) {
            return true;
        }
        SIZE_T needed = 0;
        Compress(compressor, raw.data(), raw.size(), nullptr, 0, &needed);
        packed.resize(needed);
        SIZE_T packedSize = 0;
        if (!Compress(compressor, raw.data(), raw.size(), packed.data(), packed.size(), &packedSize)) {
            error = lastError(L"Compressing failed");
            return false;
        }
        Chunk chunk;
        chunk.rawSize = static_cast<uint32_t>(raw.size());
        chunk.packedSize = static_cast<uint32_t>(packedSize);
        chunk.crc = crc32(raw.data(), raw.size());
        chunk.offset = position;
        if (!writeAll(file.h, packed.data(), packedSize)) {
            error = lastError(L"Could not write " + out);
            return false;
        }
        position += packedSize;
        index.chunks.push_back(chunk);
        raw.clear();
        return true;
    };

    std::vector<uint8_t> buffer(1 << 20);
    for (const auto& entry : index.files) {
        Handle in(CreateFileW((stageDir + L"\\" + entry.path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!in.ok()) {
            error = lastError(L"Could not read " + entry.path);
            return false;
        }
        uint64_t left = entry.size;
        while (left > 0) {
            const DWORD want = static_cast<DWORD>(std::min<uint64_t>({ left, buffer.size(), k_chunkSize - raw.size() }));
            DWORD got = 0;
            if (!ReadFile(in.h, buffer.data(), want, &got, nullptr) || got != want) {
                error = lastError(L"Could not read " + entry.path);
                return false;
            }
            raw.insert(raw.end(), buffer.begin(), buffer.begin() + got);
            left -= got;
            if (raw.size() == k_chunkSize && !flush()) {
                return false;
            }
        }
        index.totalSize += entry.size;
    }
    if (!flush()) {
        return false;
    }

    Buffer table;
    table.str(version);
    table.u32(static_cast<uint32_t>(index.files.size()));
    for (const auto& entry : index.files) {
        table.str(entry.path);
        table.u64(entry.size);
        table.u64(entry.time);
    }
    table.u32(static_cast<uint32_t>(index.chunks.size()));
    for (const auto& chunk : index.chunks) {
        table.u32(chunk.rawSize);
        table.u32(chunk.packedSize);
        table.u32(chunk.crc);
        table.u64(chunk.offset);
    }
    indexOffset = position;
    const uint64_t payloadOffset = stubBytes.size();
    const uint64_t payloadSize = position + table.bytes.size() - payloadOffset;
    if (!writeAll(file.h, table.bytes.data(), table.bytes.size()) || !writeAll(file.h, &payloadOffset, 8) ||
        !writeAll(file.h, &payloadSize, 8) || !writeAll(file.h, k_footerMagic, 8)) {
        error = lastError(L"Could not write " + out);
        return false;
    }
    // the table's place, now that it is known
    LARGE_INTEGER at;
    at.QuadPart = static_cast<LONGLONG>(payloadOffset + 8);
    if (!SetFilePointerEx(file.h, at, nullptr, FILE_BEGIN) || !writeAll(file.h, &indexOffset, 8)) {
        error = lastError(L"Could not write " + out);
        return false;
    }
    return true;
}

bool read(const std::wstring& exe, Index& index, std::wstring& error)
{
    const std::wstring damaged = L"The setup file is damaged or incomplete; download it again.";
    Handle file(CreateFileW(exe.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    LARGE_INTEGER size;
    if (!file.ok() || !GetFileSizeEx(file.h, &size) || size.QuadPart < 24) {
        error = damaged;
        return false;
    }
    uint64_t payloadOffset = 0;
    uint64_t payloadSize = 0;
    char magic[8];
    const uint64_t end = static_cast<uint64_t>(size.QuadPart);
    if (!readAt(file.h, end - 24, &payloadOffset, 8) || !readAt(file.h, end - 16, &payloadSize, 8) || !readAt(file.h, end - 8, magic, 8) ||
        std::memcmp(magic, k_footerMagic, 8) != 0) {
        error = L"This program has no BetterNMM files in it.";
        return false;
    }
    uint64_t indexOffset = 0;
    if (payloadOffset + payloadSize + 24 != end || !readAt(file.h, payloadOffset, magic, 8) || std::memcmp(magic, k_payloadMagic, 8) != 0 ||
        !readAt(file.h, payloadOffset + 8, &indexOffset, 8) || indexOffset < payloadOffset + 16 || indexOffset > end - 24) {
        error = damaged;
        return false;
    }
    Buffer table;
    table.bytes.resize(end - 24 - indexOffset);
    if (!readAt(file.h, indexOffset, table.bytes.data(), table.bytes.size())) {
        error = damaged;
        return false;
    }
    uint32_t count = 0;
    if (!table.get(index.version) || !table.get(count)) {
        error = damaged;
        return false;
    }
    index.files.resize(count);
    index.totalSize = 0;
    for (auto& entry : index.files) {
        if (!table.get(entry.path) || !table.get(entry.size) || !table.get(entry.time)) {
            error = damaged;
            return false;
        }
        index.totalSize += entry.size;
    }
    if (!table.get(count)) {
        error = damaged;
        return false;
    }
    index.chunks.resize(count);
    uint64_t raw = 0;
    for (auto& chunk : index.chunks) {
        if (!table.get(chunk.rawSize) || !table.get(chunk.packedSize) || !table.get(chunk.crc) || !table.get(chunk.offset) ||
            chunk.offset + chunk.packedSize > indexOffset) {
            error = damaged;
            return false;
        }
        raw += chunk.rawSize;
    }
    if (raw != index.totalSize) {
        error = damaged;
        return false;
    }
    index.stubSize = payloadOffset;
    return true;
}

bool extract(const std::wstring& exe,
             const Index& index,
             const std::wstring& dir,
             const std::function<void(uint64_t)>& progress,
             std::wstring& error)
{
    const std::wstring damaged = L"The setup file is damaged or incomplete; download it again.";
    Handle file(CreateFileW(exe.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!file.ok()) {
        error = lastError(L"Could not read " + exe);
        return false;
    }
    DECOMPRESSOR_HANDLE decompressor = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &decompressor)) {
        error = lastError(L"Windows' decompressor is not available");
        return false;
    }
    std::unique_ptr<std::remove_pointer_t<DECOMPRESSOR_HANDLE>, decltype(&CloseDecompressor)> closeDecompressor(decompressor,
                                                                                                                &CloseDecompressor);

    size_t current = 0;    // the file being written
    uint64_t written = 0;  // of it
    uint64_t done = 0;
    Handle out;
    auto openNext = [&]() -> bool {
        // empty files have no bytes in any chunk: they are made as they come up
        while (current < index.files.size()) {
            const auto& entry = index.files[current];
            const auto path = dir + L"\\" + entry.path;
            if (!Files::makeParent(path)) {
                error = lastError(L"Could not create the folder for " + path);
                return false;
            }
            out.reset(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (!out.ok()) {
                error = lastError(L"Could not create " + path);
                return false;
            }
            written = 0;
            if (entry.size > 0) {
                return true;
            }
            FILETIME time{ static_cast<DWORD>(entry.time), static_cast<DWORD>(entry.time >> 32) };
            SetFileTime(out.h, nullptr, nullptr, &time);
            current++;
        }
        return true;
    };
    if (!openNext()) {
        return false;
    }

    std::vector<uint8_t> packed;
    std::vector<uint8_t> raw;
    for (const auto& chunk : index.chunks) {
        packed.resize(chunk.packedSize);
        raw.resize(chunk.rawSize);
        SIZE_T rawSize = 0;
        if (!readAt(file.h, chunk.offset, packed.data(), packed.size()) ||
            !Decompress(decompressor, packed.data(), packed.size(), raw.data(), raw.size(), &rawSize) || rawSize != chunk.rawSize ||
            crc32(raw.data(), raw.size()) != chunk.crc) {
            error = damaged;
            return false;
        }
        size_t at = 0;
        while (at < raw.size()) {
            if (current >= index.files.size()) {
                error = damaged;
                return false;
            }
            const auto& entry = index.files[current];
            const size_t piece = static_cast<size_t>(std::min<uint64_t>(entry.size - written, raw.size() - at));
            if (!writeAll(out.h, raw.data() + at, piece)) {
                error = lastError(L"Could not write " + entry.path);
                return false;
            }
            at += piece;
            written += piece;
            done += piece;
            if (written == entry.size) {
                FILETIME time{ static_cast<DWORD>(entry.time), static_cast<DWORD>(entry.time >> 32) };
                SetFileTime(out.h, nullptr, nullptr, &time);
                current++;
                if (!openNext()) {
                    return false;
                }
            }
        }
        progress(done);
    }
    if (current != index.files.size()) {
        error = damaged;
        return false;
    }
    return true;
}

}  // namespace Payload
