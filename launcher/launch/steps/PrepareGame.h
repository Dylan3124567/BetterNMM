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

#include <QElapsedTimer>
#include <QTimer>

#include <memory>

#include "launch/LaunchStep.h"

class DeployTask;

/// Gets an instance ready to run: profile folders, deployed mods, the game's plugin files and the Steam app id
class PrepareGame : public LaunchStep {
    Q_OBJECT
   public:
    explicit PrepareGame(LaunchTask* parent);
    ~PrepareGame() override;

    void executeTask() override;
    /// Only while it waits for Steam: the rest is quick, or a deploy that must not stop halfway
    bool canAbort() const override { return m_steamWait.isActive(); }
    bool abort() override;

    /// How long a launch waits for Steam to start and log in; tests shorten it
    static void setSteamTimeout(int ms);

   private:
    /// An isolated instance's game that finds Steam closed starts it, with the instance's profile, and has it run the game
    /// from the Steam installation instead; so Steam is started first, as itself, and the launch waits for it
    void startSteamFirst();
    void checkSteam();
    void deploy();
    void afterDeploy();

    std::unique_ptr<DeployTask> m_deploy;
    QTimer m_steamWait;
    QElapsedTimer m_steamSince;
};
