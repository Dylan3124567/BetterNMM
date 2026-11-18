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

#include "InstanceTask.h"
#include "game/GameDefinition.h"
#include "game/GameInstance.h"

class QImage;

/**
 * Creates a game instance in the staging folder. An isolated instance gets its game folder linked from the installation
 * and a profile seeded with the game's INI files and load order from the user's real profile (read only; saves are not
 * copied). A Steam instance only records where the game is.
 */
class GameInstanceCreationTask : public InstanceTask {
    Q_OBJECT
   public:
    GameInstanceCreationTask(GameDefinition game, QString sourceDir, int steamAppId, GameInstance::Kind kind);

    QStringList messages() const { return m_messages; }

   protected:
    void executeTask() override;
    bool abort() override;

   private:
    GameDefinition m_game;
    QString m_sourceDir;
    int m_steamAppId;
    GameInstance::Kind m_kind;
    Task::Ptr m_sync;
    QStringList m_messages;
};

namespace GameIcons {
/// Windows draws an exe without a 256 px icon into its 256 px image as a 48 px icon in the top-left corner of an
/// otherwise transparent image, which shrinks to a dot when shown at list size
bool cornerOnly(const QImage& image);
/// Saves the icon of the game's executable into the icon folder and returns its key, or "default"
QString iconForGame(const GameDefinition& game, const QString& gameDir);
}  // namespace GameIcons

namespace InstancePlacement {
/// The instance folder an isolated instance of a game in @p gameDir should go in: the primary folder when it is on the
/// same drive, otherwise "BetterNMM Instances" next to the game's Steam library, so its files can be hard links
QString instanceDirFor(const QString& gameDir);
}  // namespace InstancePlacement
