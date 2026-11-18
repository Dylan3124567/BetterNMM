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

namespace {
const QByteArray g_fixture0 = R"({"user_id":99,"key":"secret","name":"Dovahkiin","is_premium":true,"is_supporter":false,
                                                "email":"d@example.com","profile_url":"https://avatars/99.png"})";
const QByteArray g_fixture1 = R"({"message":"Please provide a valid API Key"})";
const QByteArray g_fixture2 = R"({"name":"SkyUI","summary":"UI","version":"6.9","mod_id":12604,"domain_name":"skyrimspecialedition",
                                           "author":"SkyUI Team","endorsement_count":5,"updated_timestamp":1746484881,"available":true})";
const QByteArray g_fixture3 = R"({"files":[
            {"file_id":1,"name":"Old","version":"5.2","category_name":"OLD_VERSION","file_name":"old.7z","size_in_bytes":100,"uploaded_timestamp":100},
            {"file_id":2,"name":"Main","version":"6.9","category_name":"MAIN","file_name":"main.7z","size_kb":2,"uploaded_timestamp":200},
            {"file_id":3,"name":"Patch","version":"6.9","category_name":"OPTIONAL","file_name":"patch.7z","size_in_bytes":5,"uploaded_timestamp":300}
        ],"file_updates":[]})";
const QByteArray g_fixture4 = R"([{"name":"Nexus CDN","short_name":"Nexus CDN","URI":"https://cf-files.nexusmods.com/a.7z?md5=x"}])";
const QByteArray g_fixture5 = R"({"message":"Short and stout"})";
const QByteArray g_fixture6 = R"({"data":{"mods":{"totalCount":220,"nodes":[{"modId":12604,"name":"SkyUI","summary":"s",
            "version":"6.9","author":"a","thumbnailUrl":"https://t/1.png","endorsements":508879,"downloads":26576843,
            "updatedAt":"2026-05-05T22:41:21Z","game":{"domainName":"skyrimspecialedition"}}]}}})";
const QByteArray g_fixture7 = R"({"errors":[{"message":"bad filter"}]})";
const QByteArray g_fixture8 = R"({"data":{"modFiles":[{"fileId":35169,"name":"SkyUI_5_1_SE","version":"5.1SE",
            "category":"ARCHIVED","sizeInBytes":"2778523","date":1506823286,"primary":0,"uri":"SkyUI.7z"}]}})";
const QByteArray g_fixture9 = R"({"data":{"game":{"id":130,"name":"Fallout New Vegas","domainName":"newvegas"}}})";
const QByteArray g_fixture10 = R"({"data":{"games":{"nodes":[{"id":1704,"name":"Skyrim Special Edition","domainName":"skyrimspecialedition"},
            {"id":110,"name":"Skyrim","domainName":"skyrim"}]}}})";
const QByteArray g_fixture11 = R"({"formatVersion":3,"accounts":[{"type":"other"}]})";
}  // namespace
