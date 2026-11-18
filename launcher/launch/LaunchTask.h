// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Authors: Orochimarufan <orochimarufan.x3@gmail.com>
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#pragma once
#include <QDateTime>
#include <QObjectPtr.h>
#include <optional>
#include "game/GameInstance.h"
#include <QProcess>
#include "LaunchStep.h"
#include "LogModel.h"
#include "MessageLevel.h"

/// A game process that ended through an unhandled exception
struct GameCrash {
    QString program;     ///< file name of the process that crashed
    quint32 code = 0;    ///< its exception code, 0 when Windows did not say
    QDateTime started;   ///< when the launch started; crash logs written since belong to it
};

class LaunchTask : public Task {
    Q_OBJECT
   protected:
    explicit LaunchTask(GameInstance* instance);
    void init();

   public:
    enum State { NotStarted, Running, Waiting, Failed, Aborted, Finished };

   public: /* methods */
    static std::unique_ptr<LaunchTask> create(GameInstance* inst);
    virtual ~LaunchTask() = default;

    void appendStep(shared_qobject_ptr<LaunchStep> step);
    void prependStep(shared_qobject_ptr<LaunchStep> step);
    void setCensorFilter(QMap<QString, QString> filter);

    GameInstance* instance() { return m_instance; }

    void setPid(qint64 pid) { m_pid = pid; }

    qint64 pid() { return m_pid; }

    void setCrash(GameCrash crash) { m_crash = std::move(crash); }
    /// Set when the game crashed rather than being closed
    std::optional<GameCrash> crash() const { return m_crash; }

    /**
     * @brief prepare the process for launch (for multi-stage launch)
     */
    virtual void executeTask() override;

    /**
     * @brief launch the armed instance
     */
    void proceed();

    /**
     * @brief abort launch
     */
    bool abort() override;

    bool canAbort() const override;

    shared_qobject_ptr<LogModel> getLogModel();

   public:
    QStringList substituteVariables(QString& cmd, bool isLaunch = false) const;
    QString censorPrivateInfo(QString in);

   protected: /* methods */
    virtual void emitFailed(QString reason) override;
    virtual void emitSucceeded() override;

   signals:
    /**
     * @brief emitted when the launch preparations are done
     */
    void readyForLaunch();

    void requestProgress(Task* task);

    void requestLogging();

   public slots:
    void onLogLines(const QStringList& lines, MessageLevel defaultLevel = MessageLevel::Launcher);
    void onLogLine(QString line, MessageLevel defaultLevel = MessageLevel::Launcher);
    void onReadyForLaunch();
    void onStepFinished();
    void onProgressReportingRequested();

   private: /*methods */
    void finalizeSteps(bool successful, const QString& error);


   protected: /* data */
    GameInstance* m_instance;
    shared_qobject_ptr<LogModel> m_logModel;
    QList<shared_qobject_ptr<LaunchStep>> m_steps;
    QMap<QString, QString> m_censorFilter;
    int currentStep = -1;
    State state = NotStarted;
    qint64 m_pid = -1;
    std::optional<GameCrash> m_crash;
};
