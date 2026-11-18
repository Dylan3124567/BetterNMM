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
#include <QStringConverter>
#include <QStringList>

#include <optional>

#include "Result.h"

class GameInstance;

/// Game INI files: reading and changing them without disturbing anything else in them, and saving them safely
namespace Ini {

/// The pieces of an INI line, for highlighting and the settings views
struct Line {
    enum class Kind { Blank, Comment, Section, Setting, Other };
    Kind kind = Kind::Other;
    QString section;  ///< for Section lines
    QString key;      ///< for Setting lines
    QString value;    ///< for Setting lines
};

Line parse(const QString& line);

/// @p line with its value replaced, spacing and key kept as they are
QString withValue(const QString& line, const QString& value);

/// Checks a value against the type Bethesda's setting names announce (bFoo on/off, iFoo whole number, fFoo decimal, ...);
/// an empty string when it fits
QString checkValue(const QString& key, const QString& value);

/// What the setting name's type prefix means, for tooltips; empty when it has none
QString typeDescription(const QString& key);

/// File contents as text, remembering how to write them back the same way
struct Text {
    QString text;
    QStringConverter::Encoding encoding = QStringConverter::Utf8;
    bool bom = false;
    bool crlf = true;
};
Text decode(const QByteArray& data);
QByteArray encode(const Text& text);

/// An INI file whose values are read and changed by section and key; every other line stays exactly as it was
class Document {
   public:
    static Document fromBytes(const QByteArray& data);
    QByteArray toBytes() const;

    /// The value, or nothing when the file does not set it (sections and keys ignore case)
    std::optional<QString> value(const QString& section, const QString& key) const;
    /// Changes the value in place, adds the key at the end of its section, or adds the section at the end of the file
    void setValue(const QString& section, const QString& key, const QString& value);
    bool isModified() const { return m_modified; }

   private:
    Text m_format;
    QStringList m_lines;
    bool m_modified = false;
};

/**
 * Saves @p data as the INI file @p path of @p instance. The file is replaced rather than written into, so a hard link (to
 * the Steam installation or a mod's staging folder) never changes anywhere else. A file a mod deployed is saved into the
 * mod and linked into the game folder again, so redeploying keeps the change.
 */
Result<> save(GameInstance* instance, const QString& path, const QByteArray& data);

}  // namespace Ini
