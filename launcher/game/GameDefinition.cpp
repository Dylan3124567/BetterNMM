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

#include "GameDefinition.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

PluginFormat GameDefinition::pluginFormatFromString(const QString& value)
{
    if (value == "asterisk") {
        return PluginFormat::Asterisk;
    }
    if (value == "textfile") {
        return PluginFormat::Textfile;
    }
    if (value == "timestamp") {
        return PluginFormat::Timestamp;
    }
    return PluginFormat::None;
}

QString GameDefinition::pluginFormatToString(PluginFormat format)
{
    switch (format) {
        case PluginFormat::Asterisk:
            return "asterisk";
        case PluginFormat::Textfile:
            return "textfile";
        case PluginFormat::Timestamp:
            return "timestamp";
        case PluginFormat::None:
            break;
    }
    return "none";
}

namespace GameRegistry {

static QStringList toStringList(const QJsonValue& value)
{
    QStringList out;
    for (const auto& item : value.toArray()) {
        out << item.toString();
    }
    return out;
}

QList<GameDefinition> parse(const QByteArray& json, QString* error)
{
    QJsonParseError parseError;
    auto doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = parseError.errorString();
        }
        return {};
    }
    QList<GameDefinition> games;
    for (const auto& entry : doc.object().value("games").toArray()) {
        auto obj = entry.toObject();
        GameDefinition def;
        def.domain = obj.value("domain").toString();
        def.name = obj.value("name").toString();
        for (const auto& id : obj.value("steamAppIds").toArray()) {
            def.steamAppIds << id.toInt();
        }
        def.executable = obj.value("executable").toString();
        def.scriptExtender = obj.value("scriptExtender").toString();
        def.scriptExtenderMod = obj.value("scriptExtenderMod").toInt();
        def.crashLogger = obj.value("crashLogger").toObject().value("name").toString();
        def.crashLoggerMod = obj.value("crashLogger").toObject().value("mod").toInt();
        def.myGamesFolder = obj.value("myGames").toString();
        def.appDataFolder = obj.value("appData").toString();
        def.iniFiles = toStringList(obj.value("iniFiles"));
        def.saveFolders = toStringList(obj.value("saves"));
        def.dataPath = obj.value("dataPath").toString();
        def.pluginFormat = GameDefinition::pluginFormatFromString(obj.value("pluginFormat").toString());
        def.implicitPlugins = toStringList(obj.value("implicitPlugins"));
        def.cccFile = obj.value("cccFile").toString();
        def.fullPluginSlots = obj.value("pluginSlots").toObject().value("full").toInt();
        def.lightPluginSlots = obj.value("pluginSlots").toObject().value("light").toInt();
        if (def.domain.isEmpty() || def.name.isEmpty() || def.executable.isEmpty()) {
            if (error) {
                *error = QString("Game entry without domain, name or executable: %1").arg(QString::fromUtf8(QJsonDocument(obj).toJson()));
            }
            return {};
        }
        games << def;
    }
    return games;
}

const QList<GameDefinition>& all()
{
    static const QList<GameDefinition> games = [] {
        QFile file(":/games/games.json");
        if (!file.open(QIODevice::ReadOnly)) {
            qCritical() << "Built-in game list is missing:" << file.errorString();
            return QList<GameDefinition>{};
        }
        QString error;
        auto parsed = parse(file.readAll(), &error);
        if (!error.isEmpty()) {
            qCritical() << "Built-in game list is invalid:" << error;
        }
        return parsed;
    }();
    return games;
}

GameDefinition byDomain(const QString& domain)
{
    for (const auto& game : all()) {
        if (game.domain == domain) {
            return game;
        }
    }
    return {};
}

GameDefinition bySteamAppId(int appId)
{
    for (const auto& game : all()) {
        if (game.steamAppIds.contains(appId)) {
            return game;
        }
    }
    return {};
}

}  // namespace GameRegistry
