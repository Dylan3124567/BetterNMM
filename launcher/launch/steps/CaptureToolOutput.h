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

#include <memory>
#include <optional>

#include "game/ToolOutput.h"
#include "launch/LaunchStep.h"

class DeployTask;

/// What the two steps around a tool share: the game folder before it ran
struct ToolRun {
    QString tool;       ///< the tool's name, for the log
    QString outputMod;  ///< name of the mod its files go into
    ToolOutput::Filter filter;
    std::optional<ToolOutput::Snapshot> before;
};

/// Notes the files of the game folder right before a tool starts
class SnapshotGameFolder : public LaunchStep {
    Q_OBJECT
   public:
    SnapshotGameFolder(LaunchTask* parent, std::shared_ptr<ToolRun> run);

    void executeTask() override;
    bool canAbort() const override { return false; }

   private:
    std::shared_ptr<ToolRun> m_run;
};

/// Once the tool has exited (also when it failed or was stopped), moves what it wrote into the game folder into its
/// output mod and deploys again
class CaptureToolOutput : public LaunchStep {
    Q_OBJECT
   public:
    CaptureToolOutput(LaunchTask* parent, std::shared_ptr<ToolRun> run);
    ~CaptureToolOutput() override;

    void executeTask() override;
    bool canAbort() const override { return false; }
    bool runsAfterFailure() const override { return true; }

   private:
    void collect(const ToolOutput::Changes& changes);
    void afterDeploy();

    std::shared_ptr<ToolRun> m_run;
    std::unique_ptr<DeployTask> m_deploy;
    QString m_problem;  ///< why collecting stopped part way, reported once deployment has put the game folder right
};
