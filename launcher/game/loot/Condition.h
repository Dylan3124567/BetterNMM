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

#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <optional>
#include <vector>

#include "Result.h"

namespace Loot {

enum class GameType { Oblivion, Skyrim, SkyrimSE, Fallout3, FalloutNV, Fallout4, Starfield };

bool supportsLightPlugins(GameType game);

/// What conditions are evaluated against (loot-condition-interpreter's State). Results that touch files are cached.
struct ConditionState {
    GameType game = GameType::SkyrimSE;
    QString dataPath;
    QSet<QString> activePlugins;             ///< lower-case names
    QHash<QString, QString> pluginVersions;  ///< lower-case plugin name -> version read from its description
    QHash<QString, quint32> crcs;            ///< lower-case path as written in a condition -> CRC-32
    QHash<QString, bool> cache;              ///< function -> result
};

/**
 * A condition string of LOOT's metadata ("file(\"x.esp\") and not active(\"y.esp\")"), parsed and evaluated the way
 * loot-condition-interpreter does: the same grammar, functions, regex paths and version comparison rules.
 */
class Condition {
   public:
    static Result<Condition> parse(const QString& text);
    Result<bool> eval(ConditionState& state) const;

    struct Function {
        enum class Type {
            FilePath,
            FileRegex,
            FileSize,
            Readable,
            IsExecutable,
            ActivePath,
            ActiveRegex,
            IsMaster,
            Many,
            ManyActive,
            Checksum,
            Version,
            ProductVersion,
            FilenameVersion,
            DescriptionContains,
        };
        enum class Comparator { Equal, NotEqual, LessThan, GreaterThan, LessThanOrEqual, GreaterThanOrEqual };
        Type type = Type::FilePath;
        QString path;  ///< the path, or for regex paths the folder the regex is matched in
        QRegularExpression regex;
        quint64 number = 0;  ///< size or checksum
        QString version;
        Comparator comparator = Comparator::Equal;
        QString key;  ///< identifies the function in the result cache

        Result<bool> eval(ConditionState& state) const;
    };
    struct Term;
    using Expression = std::vector<std::vector<Term>>;  ///< "or" of "and"s
    struct Term {
        bool inverted = false;
        std::optional<Function> function;
        Expression expression;  ///< when there is no function
    };

   private:
    Expression m_expression;
};

/// A version as LOOT compares them: release and pre-release identifiers, padded with zeros, non-numeric parts lower-cased
class Version {
   public:
    explicit Version(const QString& text);
    Version(quint32 major, quint32 minor, quint32 patch, quint32 build);

    bool operator==(const Version& other) const;
    /// -1, 0 or 1
    int compare(const Version& other) const;

   private:
    struct Id {
        bool numeric = false;
        quint32 number = 0;
        QString text;  ///< lower-case
    };
    QList<Id> m_release;
    QList<Id> m_preRelease;
};

/// The File Version (fixed version info) of a Windows executable or DLL; nullopt when it has none
Result<std::optional<Version>> readFileVersion(const QString& path);
/// The ProductVersion string of a Windows executable or DLL; nullopt when it has none
Result<std::optional<Version>> readProductVersion(const QString& path);

quint32 crc32(const QByteArray& data, quint32 crc = 0);
/// CRC-32 of a whole file; nullopt when it can not be read
std::optional<quint32> fileCrc32(const QString& path);

/// Windows-1252 text as plugins store it
QString fromWindows1252(QByteArrayView bytes);

}  // namespace Loot
