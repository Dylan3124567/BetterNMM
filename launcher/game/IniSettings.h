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

#include <QColor>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

#include "Result.h"
#include "game/Ini.h"

class GameInstance;

/// Game INI settings with names, help and fitting controls (like BethINI), described per game in :/games/settings.json
namespace IniSettings {

struct File {
    QString id;
    QString path;               ///< relative to the folder below
    bool inGameFolder = false;  ///< the game folder; otherwise the game's My Games folder
    QString overriddenBy;       ///< id of the file whose values win, like SkyrimCustom.ini
    QString missing;            ///< when set the file must exist already, and this says how to get it
};

struct Key {
    QString file;  ///< File::id
    QString section;
    QString key;
    QString defaultValue;  ///< what the game uses when no file sets it
};

struct Option {
    QString label;
    QStringList values;  ///< one per Setting::keys
};

struct Setting {
    enum class Type { Toggle, Integer, Decimal, Choice, Resolution, Color, Text };
    /// How a colour is written: one number 0xRRGGBB or 0xAABBGGRR, "r,g,b", or three keys of 0-255 or 0-1
    enum class ColorFormat { Packed, Abgr, Text, Bytes, Units };
    Type type = Type::Toggle;
    QString label;
    QString help;
    QList<Key> keys;        ///< one, except toggles, choices, resolutions and colours, which can set several together
    QList<Option> options;  ///< choices
    double minimum = 0;
    double maximum = 0;
    double step = 1;
    int decimals = 0;
    QString suffix;
    QStringList on{ "1" };  ///< toggles: the values for on and off, one per key
    QStringList off{ "0" };
    ColorFormat colorFormat = ColorFormat::Packed;
};

struct Category {
    QString name;
    QList<Setting> settings;
};

struct Catalog {
    QList<File> files;
    QList<Category> categories;
    QString presetFile;   ///< File::id the quality presets change
    QStringList presets;  ///< the game's quality preset INIs in its game folder, lowest quality first

    bool isEmpty() const { return categories.isEmpty(); }
    const File* file(const QString& id) const;
};

/// Parses settings.json into catalogs by Nexus domain; exposed for tests
QMap<QString, Catalog> parse(const QByteArray& json, QString* error = nullptr);
/// The catalog for a Nexus domain; empty when there is none
const Catalog& forGame(const QString& domain);

/// Compares INI values the way the game reads them: 1 and 1.0000 are the same number, true and TRUE the same word
bool sameValue(const QString& a, const QString& b);
/// The option of a choice the values select, or -1 when no option matches
int matchingOption(const Setting& setting, const QStringList& values);
/// Whether a toggle's values are its "on" values
bool isOn(const Setting& setting, const QStringList& values);
/// The colour a colour setting's values describe; invalid when they describe none
QColor colorOf(const Setting& setting, const QStringList& values);
/// The values that write @p color in a colour setting's format
QStringList colorValues(const Setting& setting, const QColor& color);

/// The INI files of one instance that a catalog uses; changes stay in memory until save()
class Files {
   public:
    Files(GameInstance* instance, Catalog catalog);

    void load();
    /// Files that exist but could not be read; they stay untouched
    QStringList errors() const;
    QString path(const QString& fileId) const;
    /// False for a file that has to exist already (File::missing) but does not
    bool available(const QString& fileId) const;
    /// The value the game will use
    QString value(const Key& key) const;
    QStringList values(const Setting& setting) const;
    /// Changes the file the game takes the value from: the overriding file when that sets the key, the key's own file otherwise
    void setValue(const Key& key, const QString& value);
    /// Applies every value of one of the game's preset INIs to the preset file; how many values it applied
    Result<int> applyPreset(const QString& presetPath);
    bool isModified() const;
    /// Saves the changed files and loads them again
    Result<> save();

   private:
    std::optional<QString> fileValue(const QString& fileId, const Key& key) const;

    GameInstance* m_instance;
    Catalog m_catalog;
    QMap<QString, Ini::Document> m_documents;
    QSet<QString> m_existing;
    QMap<QString, QString> m_unreadable;  ///< file id to the read error
};

}  // namespace IniSettings
