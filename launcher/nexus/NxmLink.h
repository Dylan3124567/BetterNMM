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

#include <QString>
#include <QUrl>

#include "Result.h"

/// A "Mod Manager Download" link from the Nexus Mods website: nxm://<game>/mods/<mod>/files/<file>?key=..&expires=..&user_id=..
struct NxmLink {
    QString game;
    int modId = 0;
    int fileId = 0;
    QString key;
    qint64 expires = 0;
    int userId = 0;

    static Result<NxmLink> parse(const QUrl& url);
    bool expired() const;
};

namespace Nxm {
/// Whether nxm:// links currently open BetterNMM
bool isHandler();
/// Makes nxm:// links open this executable (per user, no admin rights needed)
Result<> registerHandler();
}  // namespace Nxm
