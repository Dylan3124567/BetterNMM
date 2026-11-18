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
#include <vector>

#include "Result.h"

namespace Loot {

/// A node of a parsed YAML document
struct YamlNode {
    enum class Kind { Null, Scalar, Sequence, Mapping };
    Kind kind = Kind::Null;
    QString scalar;
    std::vector<YamlNode> items;  ///< sequence elements; in a mapping keys and values alternate
    int line = 0;                 ///< 1-based line the node starts on

    /// The value of @p key when this is a mapping, nullptr otherwise
    const YamlNode* get(QStringView key) const;
};

/**
 * Parses one YAML document as LOOT's metadata files use it: block and flow collections, plain, quoted and block
 * scalars, comments, anchors and aliases, and "<<" merge keys applied the way libloot does (keys the mapping already
 * has win, merged keys are appended).
 */
Result<YamlNode> parseYaml(const QString& text);

}  // namespace Loot
