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

#include <QUrl>
#include <QVersionNumber>

#include "Result.h"

class QWidget;

/// Updates BetterNMM from its GitHub releases: the setup program of a newer release is downloaded and run silently,
/// which replaces the installed copy and starts it again
namespace SelfUpdate {

struct Release {
    QVersionNumber version;
    QUrl page;       ///< the release on GitHub
    QUrl installer;  ///< its setup program; empty when the release has none
    qint64 size = 0;
    QString notes;
};

/// The release described by GitHub's "latest release" answer
Result<Release> parseRelease(const QByteArray& json);

/// Asks GitHub for the newest release and offers to update when it is newer than this build. @p quiet says nothing
/// when BetterNMM is up to date or GitHub can not be reached (the check at startup).
void check(QWidget* parent, bool quiet);

}  // namespace SelfUpdate
