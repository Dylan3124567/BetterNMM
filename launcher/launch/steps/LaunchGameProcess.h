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

#include <QDateTime>
#include <QHash>
#include <QTimer>

#include <mutex>
#include <optional>
#include <thread>

#include "LoggedProcess.h"
#include "game/GameInstance.h"
#include "launch/LaunchStep.h"
#include "launch/LaunchTask.h"

/**
 * Runs the chosen executable with the instance's environment. The process goes into a Windows job object, so the step
 * keeps running while anything it started is alive: script extender loaders start the game and exit at once.
 */
class LaunchGameProcess : public LaunchStep {
    Q_OBJECT
   public:
    LaunchGameProcess(LaunchTask* parent, GameExecutable executable);
    ~LaunchGameProcess() override;

    void executeTask() override;
    bool abort() override;
    bool canAbort() const override { return true; }
    void proceed() override;

   private slots:
    void onState(LoggedProcess::State state);

   private:
    void watchJob();
    void finishGame(bool crashed);
    int activeJobProcesses() const;
    /// Handles what Windows reports about the job's processes: which started, and which ended through an exception.
    /// Runs on its own thread, which takes each new process's handle the moment it starts, before it can crash and vanish.
    void readJobMessages();
    /// Lets the message thread handle everything already reported, then ends it; m_crash is final afterwards
    void stopReadingJobMessages();

    struct JobProcess {
        void* handle = nullptr;
        QString name;
    };

    GameExecutable m_executable;
    LoggedProcess m_process;
    QTimer m_jobPoll;
    void* m_job = nullptr;
    void* m_port = nullptr;
    std::thread m_messages;
    std::mutex m_mutex;  ///< guards m_crash while the message thread runs
    QHash<quint32, JobProcess> m_jobProcesses;  ///< only touched by the message thread
    std::optional<GameCrash> m_crash;
    QDateTime m_started;
    QByteArray m_attributeList;
    QByteArray m_startupInfo;
    bool m_gameRunning = false;
    bool m_aborted = false;
};
