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

#include "IniSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "FileSystem.h"
#include "game/GameInstance.h"

namespace IniSettings {

const File* Catalog::file(const QString& id) const
{
    const auto it = std::find_if(files.cbegin(), files.cend(), [&id](const File& file) { return file.id == id; });
    return it == files.cend() ? nullptr : &*it;
}

namespace {

/// ["file id", "Section", "key", "default"]
Result<Key> parseKey(const QJsonValue& value, const Catalog& catalog)
{
    const auto parts = value.toArray();
    if (parts.size() != 4 || !std::all_of(parts.begin(), parts.end(), [](const QJsonValue& part) { return part.isString(); })) {
        return std::unexpected(QString("a key is not [file, section, key, default]: %1").arg(QString::fromUtf8(QJsonDocument(parts).toJson(QJsonDocument::Compact))));
    }
    Key key{ .file = parts[0].toString(), .section = parts[1].toString(), .key = parts[2].toString(), .defaultValue = parts[3].toString() };
    if (!catalog.file(key.file)) {
        return std::unexpected(QString("%1 is in file \"%2\", which the game does not list").arg(key.key, key.file));
    }
    return key;
}

Result<Setting> parseSetting(const QJsonObject& object, const Catalog& catalog)
{
    static const QMap<QString, Setting::Type> types{ { "toggle", Setting::Type::Toggle },         { "integer", Setting::Type::Integer },
                                                     { "decimal", Setting::Type::Decimal },       { "choice", Setting::Type::Choice },
                                                     { "resolution", Setting::Type::Resolution }, { "color", Setting::Type::Color },
                                                     { "text", Setting::Type::Text } };
    static const QMap<QString, Setting::ColorFormat> colorFormats{ { "packed", Setting::ColorFormat::Packed },
                                                                   { "abgr", Setting::ColorFormat::Abgr },
                                                                   { "text", Setting::ColorFormat::Text },
                                                                   { "bytes", Setting::ColorFormat::Bytes },
                                                                   { "units", Setting::ColorFormat::Units } };
    Setting setting;
    setting.label = object.value("label").toString();
    setting.help = object.value("help").toString();
    const auto type = object.value("type").toString();
    if (setting.label.isEmpty() || !types.contains(type)) {
        return std::unexpected(QString("setting \"%1\" needs a label and a type (%2)").arg(setting.label, types.keys().join(", ")));
    }
    setting.type = types.value(type);
    const auto keys = object.contains("key") ? QJsonArray{ object.value("key") } : object.value("keys").toArray();
    for (const auto& value : keys) {
        TRY_INTO(const auto key, parseKey(value, catalog));
        setting.keys << key;
    }
    for (const auto& value : object.value("options").toArray()) {
        const auto parts = value.toArray();
        Option option{ .label = parts.isEmpty() ? QString() : parts.first().toString() };
        for (qsizetype i = 1; i < parts.size(); i++) {
            option.values << parts[i].toString();
        }
        if (option.label.isEmpty() || option.values.size() != setting.keys.size()) {
            return std::unexpected(QString("%1: every option needs a label and one value per key").arg(setting.label));
        }
        setting.options << option;
    }
    setting.minimum = object.value("min").toDouble();
    setting.maximum = object.value("max").toDouble();
    setting.step = object.value("step").toDouble(1);
    setting.decimals = object.value("decimals").toInt();
    setting.suffix = object.value("suffix").toString();
    // "on"/"off": a value, or one value per key
    const auto toggleValues = [&object](const char* name, const QString& fallback) {
        const auto value = object.value(name);
        if (!value.isArray()) {
            return QStringList{ value.toString(fallback) };
        }
        QStringList values;
        for (const auto& part : value.toArray()) {
            values << part.toString();
        }
        return values;
    };
    setting.on = toggleValues("on", "1");
    setting.off = toggleValues("off", "0");
    const auto format = object.value("format").toString("packed");
    setting.colorFormat = colorFormats.value(format);

    int expectedKeys = 1;
    switch (setting.type) {
        case Setting::Type::Resolution:
            expectedKeys = 2;
            break;
        case Setting::Type::Choice:
            expectedKeys = -1;
            break;
        case Setting::Type::Toggle:
            expectedKeys = static_cast<int>(setting.on.size());
            break;
        case Setting::Type::Color:
            expectedKeys = setting.colorFormat == Setting::ColorFormat::Bytes || setting.colorFormat == Setting::ColorFormat::Units ? 3 : 1;
            break;
        default:
            break;
    }
    if (setting.keys.isEmpty() || (expectedKeys > 0 && setting.keys.size() != expectedKeys)) {
        return std::unexpected(QString("%1 has %2 keys").arg(setting.label).arg(setting.keys.size()));
    }
    if (setting.type == Setting::Type::Toggle && setting.off.size() != setting.on.size()) {
        return std::unexpected(QString("%1 needs as many \"off\" values as \"on\" values").arg(setting.label));
    }
    if (setting.type == Setting::Type::Color && !colorFormats.contains(format)) {
        return std::unexpected(QString("%1: unknown colour format \"%2\" (%3)").arg(setting.label, format, colorFormats.keys().join(", ")));
    }
    if (setting.type == Setting::Type::Choice && setting.options.size() < 2) {
        return std::unexpected(QString("%1 is a choice without options").arg(setting.label));
    }
    if ((setting.type == Setting::Type::Integer || setting.type == Setting::Type::Decimal) && setting.maximum <= setting.minimum) {
        return std::unexpected(QString("%1 needs a range (min below max)").arg(setting.label));
    }
    return setting;
}

Result<Category> parseCategory(const QJsonObject& object, const Catalog& catalog)
{
    Category category{ .name = object.value("name").toString() };
    for (const auto& value : object.value("settings").toArray()) {
        TRY_INTO(const auto setting, parseSetting(value.toObject(), catalog));
        category.settings << setting;
    }
    if (category.name.isEmpty() || category.settings.isEmpty()) {
        return std::unexpected(QString("category \"%1\" needs a name and settings").arg(category.name));
    }
    return category;
}

}  // namespace

QMap<QString, Catalog> parse(const QByteArray& json, QString* error)
{
    const auto fail = [error](const QString& game, const QString& message) {
        if (error) {
            *error = game.isEmpty() ? message : QString("%1: %2").arg(game, message);
        }
        return QMap<QString, Catalog>{};
    };
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail({}, parseError.errorString());
    }
    const auto games = document.object().value("games").toObject();
    QMap<QString, Catalog> catalogs;
    for (auto game = games.begin(); game != games.end(); ++game) {
        const auto object = game->toObject();
        Catalog catalog;
        for (const auto& value : object.value("files").toArray()) {
            const auto file = value.toObject();
            catalog.files << File{ .id = file.value("id").toString(),
                                   .path = file.value("path").toString(),
                                   .inGameFolder = file.value("folder").toString() == "game",
                                   .overriddenBy = file.value("overriddenBy").toString(),
                                   .missing = file.value("missing").toString() };
        }
        for (const auto& file : catalog.files) {
            if (file.id.isEmpty() || file.path.isEmpty() || (!file.overriddenBy.isEmpty() && !catalog.file(file.overriddenBy))) {
                return fail(game.key(), QString("file \"%1\" needs an id, a path and a known overriding file").arg(file.id));
            }
        }
        const auto presets = object.value("presets").toObject();
        catalog.presetFile = presets.value("file").toString();
        for (const auto& name : presets.value("names").toArray()) {
            catalog.presets << name.toString();
        }
        if (!catalog.presets.isEmpty() && !catalog.file(catalog.presetFile)) {
            return fail(game.key(), QString("the presets change file \"%1\", which the game does not list").arg(catalog.presetFile));
        }
        for (const auto& value : object.value("categories").toArray()) {
            auto category = value.toObject();
            // "include": "domain/Category" shares a category with another game whose files have the same ids
            if (const auto include = category.value("include").toString(); !include.isEmpty()) {
                const auto parts = include.split('/');
                const auto others = games.value(parts.first()).toObject().value("categories").toArray();
                const auto found = std::find_if(others.begin(), others.end(), [&parts](const QJsonValue& other) {
                    return parts.size() == 2 && other.toObject().value("name").toString() == parts[1];
                });
                if (found == others.end() || found->toObject().contains("include")) {
                    return fail(game.key(), QString("included category %1 does not exist").arg(include));
                }
                category = found->toObject();
            }
            auto parsed = parseCategory(category, catalog);
            if (!parsed) {
                return fail(game.key(), parsed.error());
            }
            catalog.categories << *parsed;
        }
        catalogs.insert(game.key(), catalog);
    }
    return catalogs;
}

const Catalog& forGame(const QString& domain)
{
    static const QMap<QString, Catalog> catalogs = [] {
        QFile file(":/games/settings.json");
        if (!file.open(QIODevice::ReadOnly)) {
            qCritical() << "Built-in INI settings are missing:" << file.errorString();
            return QMap<QString, Catalog>{};
        }
        QString error;
        auto parsed = parse(file.readAll(), &error);
        if (!error.isEmpty()) {
            qCritical() << "Built-in INI settings are invalid:" << error;
        }
        return parsed;
    }();
    static const Catalog none;
    const auto it = catalogs.constFind(domain);
    return it == catalogs.cend() ? none : *it;
}

bool sameValue(const QString& a, const QString& b)
{
    if (a.compare(b, Qt::CaseInsensitive) == 0) {
        return true;
    }
    bool numberA = false;
    bool numberB = false;
    const auto x = a.toDouble(&numberA);
    const auto y = b.toDouble(&numberB);
    return numberA && numberB && x == y;
}

int matchingOption(const Setting& setting, const QStringList& values)
{
    for (qsizetype i = 0; i < setting.options.size(); i++) {
        const auto& option = setting.options[i].values;
        bool matches = option.size() == values.size();
        for (qsizetype j = 0; matches && j < values.size(); j++) {
            matches = sameValue(option[j], values[j]);
        }
        if (matches) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool isOn(const Setting& setting, const QStringList& values)
{
    if (values.size() != setting.on.size()) {
        return false;
    }
    for (qsizetype i = 0; i < values.size(); i++) {
        if (!sameValue(values[i], setting.on[i])) {
            return false;
        }
    }
    return true;
}

QColor colorOf(const Setting& setting, const QStringList& values)
{
    const auto component = [](const QString& text, double scale) -> std::optional<int> {
        bool ok = false;
        const auto value = text.trimmed().toDouble(&ok) * scale;
        if (!ok || value < 0 || value > 255.5) {
            return std::nullopt;
        }
        return static_cast<int>(std::lround(value));
    };
    const auto fromParts = [&component](const QStringList& parts, double scale) {
        if (parts.size() != 3) {
            return QColor();
        }
        const auto r = component(parts[0], scale);
        const auto g = component(parts[1], scale);
        const auto b = component(parts[2], scale);
        return r && g && b ? QColor(*r, *g, *b) : QColor();
    };
    switch (setting.colorFormat) {
        case Setting::ColorFormat::Packed:
        case Setting::ColorFormat::Abgr: {
            bool ok = false;
            const auto number = values.value(0).trimmed().toLongLong(&ok);
            if (!ok || number < std::numeric_limits<int32_t>::min() || number > std::numeric_limits<uint32_t>::max()) {
                return {};
            }
            const auto bits = static_cast<uint32_t>(number);  // negative numbers are the same bits written as a signed int
            return setting.colorFormat == Setting::ColorFormat::Packed ? QColor((bits >> 16) & 0xFF, (bits >> 8) & 0xFF, bits & 0xFF)
                                                                         : QColor(bits & 0xFF, (bits >> 8) & 0xFF, (bits >> 16) & 0xFF);
        }
        case Setting::ColorFormat::Text:
            return fromParts(values.value(0).split(','), 1);
        case Setting::ColorFormat::Bytes:
            return fromParts(values, 1);
        case Setting::ColorFormat::Units:
            return fromParts(values, 255);
    }
    return {};
}

QStringList colorValues(const Setting& setting, const QColor& color)
{
    const auto unit = [](int component) { return QString::number(component / 255.0, 'f', 4); };
    switch (setting.colorFormat) {
        case Setting::ColorFormat::Packed:
            return { QString::number((color.red() << 16) | (color.green() << 8) | color.blue()) };
        case Setting::ColorFormat::Abgr: {
            // opaque, and written as a signed 32-bit number: the game reads "i" settings as one
            const auto bits = 0xFF000000u | (static_cast<uint32_t>(color.blue()) << 16) | (static_cast<uint32_t>(color.green()) << 8) |
                              static_cast<uint32_t>(color.red());
            return { QString::number(static_cast<int32_t>(bits)) };
        }
        case Setting::ColorFormat::Text:
            return { QString("%1,%2,%3").arg(color.red()).arg(color.green()).arg(color.blue()) };
        case Setting::ColorFormat::Bytes:
            return { QString::number(color.red()), QString::number(color.green()), QString::number(color.blue()) };
        case Setting::ColorFormat::Units:
            return { unit(color.red()), unit(color.green()), unit(color.blue()) };
    }
    return {};
}

Files::Files(GameInstance* instance, Catalog catalog) : m_instance(instance), m_catalog(std::move(catalog))
{
    load();
}

void Files::load()
{
    m_documents.clear();
    m_existing.clear();
    m_unreadable.clear();
    for (const auto& file : m_catalog.files) {
        const auto path = this->path(file.id);
        QByteArray data;
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            QFile in(path);
            if (!in.open(QIODevice::ReadOnly)) {
                // never edit a file that could not be read: saving would replace it with just the changed lines
                m_unreadable.insert(file.id, QObject::tr("Could not read %1: %2").arg(QDir::toNativeSeparators(path), in.errorString()));
                continue;
            }
            data = in.readAll();
            m_existing.insert(file.id);
        }
        m_documents.insert(file.id, Ini::Document::fromBytes(data));
    }
}

QStringList Files::errors() const
{
    return m_unreadable.values();
}

QString Files::path(const QString& fileId) const
{
    const auto* file = m_catalog.file(fileId);
    if (!file) {
        return {};
    }
    const auto folder = file->inGameFolder ? m_instance->gameRoot() : m_instance->myGamesDir();
    return folder.isEmpty() ? QString() : FS::PathCombine(folder, file->path);
}

bool Files::available(const QString& fileId) const
{
    const auto* file = m_catalog.file(fileId);
    return file && m_documents.contains(fileId) && !path(fileId).isEmpty() && (file->missing.isEmpty() || m_existing.contains(fileId));
}

std::optional<QString> Files::fileValue(const QString& fileId, const Key& key) const
{
    const auto it = m_documents.constFind(fileId);
    return it == m_documents.cend() ? std::nullopt : it->value(key.section, key.key);
}

QString Files::value(const Key& key) const
{
    const auto* file = m_catalog.file(key.file);
    if (file && !file->overriddenBy.isEmpty()) {
        if (const auto value = fileValue(file->overriddenBy, key)) {
            return *value;
        }
    }
    return fileValue(key.file, key).value_or(key.defaultValue);
}

QStringList Files::values(const Setting& setting) const
{
    QStringList out;
    for (const auto& key : setting.keys) {
        out << value(key);
    }
    return out;
}

void Files::setValue(const Key& key, const QString& value)
{
    auto target = key.file;
    if (const auto* file = m_catalog.file(key.file); file && !file->overriddenBy.isEmpty() && fileValue(file->overriddenBy, key)) {
        target = file->overriddenBy;
    }
    if (available(target)) {
        m_documents[target].setValue(key.section, key.key, value);
    }
}

Result<int> Files::applyPreset(const QString& presetPath)
{
    QFile in(presetPath);
    if (!in.open(QIODevice::ReadOnly)) {
        return std::unexpected(QObject::tr("Could not read %1: %2").arg(QDir::toNativeSeparators(presetPath), in.errorString()));
    }
    int applied = 0;
    QString section;
    for (const auto& text : Ini::decode(in.readAll()).text.split('\n')) {
        const auto line = Ini::parse(text);
        if (line.kind == Ini::Line::Kind::Section) {
            section = line.section;
        } else if (line.kind == Ini::Line::Kind::Setting && !section.isEmpty()) {
            setValue({ .file = m_catalog.presetFile, .section = section, .key = line.key }, line.value);
            applied++;
        }
    }
    return applied;
}

bool Files::isModified() const
{
    return std::any_of(m_documents.cbegin(), m_documents.cend(), [](const Ini::Document& document) { return document.isModified(); });
}

Result<> Files::save()
{
    for (auto it = m_documents.cbegin(); it != m_documents.cend(); ++it) {
        if (it->isModified()) {
            TRY(Ini::save(m_instance, path(it.key()), it->toBytes()));
        }
    }
    load();
    return {};
}

}  // namespace IniSettings
