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

#include <QByteArray>

const QByteArray g_libraryFolders = R"("libraryfolders"
{
	"0"
	{
		"path"		"A:\\SteamClient"
		"apps"
		{
			"489830"		"16092533099"
		}
	}
	// a comment
	"1" { "path" "D:\\Games\\Steam \"Library\"" }
})";

// Steam depot manifests are protobuf: these build the fields parseDepotManifest reads

inline QByteArray varint(quint64 value)
{
    QByteArray out;
    do {
        quint8 byte = value & 0x7F;
        value >>= 7;
        if (value) {
            byte |= 0x80;
        }
        out.append(static_cast<char>(byte));
    } while (value);
    return out;
}

inline QByteArray field(int number, int wireType)
{
    return varint(static_cast<quint64>(number << 3 | wireType));
}

inline QByteArray bytesField(int number, const QByteArray& data)
{
    return field(number, 2) + varint(static_cast<quint64>(data.size())) + data;
}

inline QByteArray fileMapping(const QByteArray& name, quint64 size, quint64 flags)
{
    QByteArray mapping = bytesField(1, name) + field(2, 0) + varint(size) + field(3, 0) + varint(flags);
    mapping += bytesField(4, QByteArray(20, 'x'));  // sha of the name: skipped
    return bytesField(1, mapping);
}

inline QByteArray section(quint32 magic, const QByteArray& payload)
{
    QByteArray out;
    for (int i = 0; i < 4; i++) {
        out.append(static_cast<char>((magic >> (8 * i)) & 0xFF));
    }
    const auto size = static_cast<quint32>(payload.size());
    for (int i = 0; i < 4; i++) {
        out.append(static_cast<char>((size >> (8 * i)) & 0xFF));
    }
    return out + payload;
}
