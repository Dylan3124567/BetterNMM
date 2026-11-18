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

#include <QList>
#include <QRegularExpression>
#include <QStringList>

/// Case-insensitive wildcard patterns compiled once. Patterns with a '/' match the whole relative path ('*' crosses
/// folders), others only the file name.
class PatternMatcher {
   public:
    explicit PatternMatcher(const QStringList& patterns)
    {
        for (const auto& pattern : patterns) {
            const QRegularExpression re(QRegularExpression::wildcardToRegularExpression(pattern, QRegularExpression::NonPathWildcardConversion),
                                        QRegularExpression::CaseInsensitiveOption);
            (pattern.contains('/') ? m_pathPatterns : m_namePatterns) << re;
        }
    }

    bool matches(const QString& relPath) const
    {
        const auto name = relPath.mid(relPath.lastIndexOf('/') + 1);
        for (const auto& re : m_namePatterns) {
            if (re.match(name).hasMatch()) {
                return true;
            }
        }
        for (const auto& re : m_pathPatterns) {
            if (re.match(relPath).hasMatch()) {
                return true;
            }
        }
        return false;
    }

   private:
    QList<QRegularExpression> m_namePatterns;
    QList<QRegularExpression> m_pathPatterns;
};
