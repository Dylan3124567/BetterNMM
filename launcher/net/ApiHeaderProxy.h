// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (C) 2023 Rachel Powers <508861+Ryex@users.noreply.github.com>
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
 *
 */

#pragma once

#include "Application.h"
#include "BuildConfig.h"
#include "net/HeaderProxy.h"
#include "nexus/NexusAccount.h"

namespace Net {

/// Adds the Nexus Mods API key and the application identification Nexus asks API clients to send
class ApiHeaderProxy : public HeaderProxy {
   public:
    ApiHeaderProxy() = default;
    ~ApiHeaderProxy() override = default;

   public:
    QList<HeaderPair> headers(const QNetworkRequest& request) const override
    {
        QList<HeaderPair> hdrs;
        if (request.url().host() != QUrl(BuildConfig.NEXUS_API_URL).host()) {
            return hdrs;
        }
        hdrs.append({ .headerName = "Application-Name", .headerValue = BuildConfig.LAUNCHER_NAME.toUtf8() });
        hdrs.append({ .headerName = "Application-Version", .headerValue = BuildConfig.printableVersionString().toUtf8() });
        if (APPLICATION->accounts()) {
            if (auto key = APPLICATION->accounts()->apiKey(); !key.isEmpty()) {
                hdrs.append({ .headerName = "apikey", .headerValue = key.toUtf8() });
            }
        }
        return hdrs;
    }
};

}  // namespace Net
