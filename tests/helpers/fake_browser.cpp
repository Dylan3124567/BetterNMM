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

// Stands in for betternmm-browser.exe in tests. It answers the download window's protocol at once, as if the person had
// clicked "Slow download" on every page: the page address comes back as the download link. A title starting with
// "skip" is skipped, "timeout" is clicked but never starts, "ignore" is never taken. It closes once no more files come.

#include <windows.h>

#include <string>
#include <vector>

namespace {

void say(const std::string& line)
{
    const auto text = line + "\n";
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text.data(), DWORD(text.size()), &written, nullptr);
}

bool startsWith(const std::string& text, const char* prefix)
{
    return text.rfind(prefix, 0) == 0;
}

void answer(const std::string& line)
{
    std::vector<std::string> fields;
    for (size_t start = 0;;) {
        const auto tab = line.find('\t', start);
        fields.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) {
            break;
        }
        start = tab + 1;
    }
    if (fields.size() < 4 || fields[0] != "item" || startsWith(fields[3], "ignore")) {
        return;
    }
    const auto& id = fields[1];
    say("queued\t" + id);
    say("shown\t" + id);
    if (startsWith(fields[3], "skip")) {
        say("skipped\t" + id);
        return;
    }
    say("clicked\t" + id);
    if (startsWith(fields[3], "timeout")) {
        say("timeout\t" + id);
        return;
    }
    say("download\t" + id + "\t" + fields[2] + "\tfrom-window.zip");
}

}  // namespace

int main()
{
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    std::string buffer;
    // done after half a second without input, like the real window once its queue is empty
    for (int idle = 0; idle < 10;) {
        DWORD available = 0;
        if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr)) {
            break;
        }
        if (available == 0) {
            Sleep(50);
            idle++;
            continue;
        }
        idle = 0;
        std::vector<char> chunk(available);
        DWORD read = 0;
        if (!ReadFile(input, chunk.data(), available, &read, nullptr)) {
            break;
        }
        buffer.append(chunk.data(), read);
        for (auto end = buffer.find('\n'); end != std::string::npos; end = buffer.find('\n')) {
            auto line = buffer.substr(0, end);
            buffer.erase(0, end + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            answer(line);
        }
    }
    return 0;
}
