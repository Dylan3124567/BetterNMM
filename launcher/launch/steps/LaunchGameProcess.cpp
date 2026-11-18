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

#include "LaunchGameProcess.h"

#include <QDir>
#include <QFileInfo>

#include "Application.h"
#include "Commandline.h"
#include "game/CrashReport.h"
#include "launch/LaunchTask.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

LaunchGameProcess::LaunchGameProcess(LaunchTask* parent, GameExecutable executable)
    : LaunchStep(parent), m_executable(std::move(executable))
{
    m_process.setProcessEnvironment(m_parent->instance()->createLaunchEnvironment());
    connect(&m_process, &LoggedProcess::log, this, &LaunchGameProcess::logLines);
    connect(&m_process, &LoggedProcess::stateChanged, this, &LaunchGameProcess::onState);
    m_jobPoll.setInterval(1000);
    connect(&m_jobPoll, &QTimer::timeout, this, &LaunchGameProcess::watchJob);
}

LaunchGameProcess::~LaunchGameProcess()
{
    stopReadingJobMessages();
#ifdef Q_OS_WIN
    for (const auto& process : std::as_const(m_jobProcesses)) {
        if (process.handle) {
            CloseHandle(static_cast<HANDLE>(process.handle));
        }
    }
    if (m_port) {
        CloseHandle(static_cast<HANDLE>(m_port));
    }
    if (m_job) {
        CloseHandle(static_cast<HANDLE>(m_job));
    }
#endif
}

void LaunchGameProcess::stopReadingJobMessages()
{
#ifdef Q_OS_WIN
    if (m_messages.joinable()) {
        // queued after everything Windows already reported, so those are all handled first
        PostQueuedCompletionStatus(static_cast<HANDLE>(m_port), 0, 0, nullptr);
        m_messages.join();
    }
#endif
}

void LaunchGameProcess::readJobMessages()
{
#ifdef Q_OS_WIN
    DWORD message = 0;
    ULONG_PTR key = 0;
    LPOVERLAPPED data = nullptr;
    while (GetQueuedCompletionStatus(static_cast<HANDLE>(m_port), &message, &key, &data, INFINITE)) {
        if (key == 0) {
            return;  // stopReadingJobMessages
        }
        // for these messages the "overlapped" pointer carries the process id
        const auto pid = static_cast<quint32>(reinterpret_cast<ULONG_PTR>(data));
        if (message == JOB_OBJECT_MSG_NEW_PROCESS) {
            // a handle taken now keeps the exit code readable after the process is gone
            JobProcess process;
            process.handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
            if (process.handle) {
                wchar_t path[MAX_PATH * 2];
                DWORD size = static_cast<DWORD>(std::size(path));
                if (QueryFullProcessImageNameW(static_cast<HANDLE>(process.handle), 0, path, &size)) {
                    process.name = QFileInfo(QString::fromWCharArray(path, static_cast<qsizetype>(size))).fileName();
                }
            }
            m_jobProcesses.insert(pid, process);
        } else if (message == JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS) {
            const auto process = m_jobProcesses.value(pid);
            DWORD code = 0;
            if (process.handle) {
                GetExitCodeProcess(static_cast<HANDLE>(process.handle), &code);
            }
            const std::lock_guard lock(m_mutex);
            if (!m_crash) {
                m_crash = GameCrash{ .program = process.name.isEmpty() ? tr("process %1").arg(pid) : process.name,
                                     .code = static_cast<quint32>(code),
                                     .started = m_started };
            }
        }
    }
#endif
}

void LaunchGameProcess::executeTask()
{
    m_started = QDateTime::currentDateTime();
    auto* instance = m_parent->instance();
    const auto program = instance->absoluteExecutablePath(m_executable);
    if (!QFileInfo(program).isFile()) {
        const auto error = tr("%1 does not exist.").arg(QDir::toNativeSeparators(program));
        emit logLine(error, MessageLevel::Fatal);
        emitFailed(error);
        return;
    }
    auto argsTemplate = m_executable.arguments;
    const auto args = argsTemplate.isEmpty() ? QStringList() : m_parent->substituteVariables(argsTemplate, true);
    m_process.setWorkingDirectory(m_executable.workingDir.isEmpty() ? QFileInfo(program).absolutePath() : m_executable.workingDir);

#ifdef Q_OS_WIN
    // every process the executable starts lands in this job, so the game is tracked even when a loader exits early
    m_job = CreateJobObjectW(nullptr, nullptr);
    if (m_job) {
        // Windows tells this port about every process of the job, and which of them ended through an exception (a crash)
        m_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
        JOBOBJECT_ASSOCIATE_COMPLETION_PORT association{};
        association.CompletionKey = m_job;
        association.CompletionPort = static_cast<HANDLE>(m_port);
        if (m_port && !SetInformationJobObject(static_cast<HANDLE>(m_job), JobObjectAssociateCompletionPortInformation, &association,
                                               sizeof(association))) {
            CloseHandle(static_cast<HANDLE>(m_port));
            m_port = nullptr;
        }
        if (m_port) {
            m_messages = std::thread([this] { readJobMessages(); });
        }
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        m_attributeList.resize(static_cast<qsizetype>(size));
        auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(m_attributeList.data());
        if (InitializeProcThreadAttributeList(list, 1, 0, &size) &&
            UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &m_job, sizeof(HANDLE), nullptr, nullptr)) {
            m_process.setCreateProcessArgumentsModifier([this, list](QProcess::CreateProcessArguments* cpa) {
                m_startupInfo.resize(sizeof(STARTUPINFOEXW));
                auto* info = reinterpret_cast<STARTUPINFOEXW*>(m_startupInfo.data());
                *info = {};
                info->StartupInfo = *reinterpret_cast<STARTUPINFOW*>(cpa->startupInfo);
                info->StartupInfo.cb = sizeof(STARTUPINFOEXW);
                info->lpAttributeList = list;
                cpa->startupInfo = reinterpret_cast<Q_STARTUPINFO*>(info);
                cpa->flags |= EXTENDED_STARTUPINFO_PRESENT;
            });
        } else {
            emit logLine(tr("Could not set up process tracking; the game is considered closed when %1 exits.").arg(m_executable.name),
                         MessageLevel::Warning);
            CloseHandle(static_cast<HANDLE>(m_job));
            m_job = nullptr;
        }
    }
#endif

    emit logLine(tr("Starting %1: %2 %3").arg(m_executable.name, QDir::toNativeSeparators(program), args.join(' ')), MessageLevel::Launcher);
    m_process.start(program, args);
}

void LaunchGameProcess::onState(LoggedProcess::State state)
{
    switch (state) {
        case LoggedProcess::FailedToStart: {
            const auto error = tr("Could not start %1: %2").arg(m_executable.name, m_process.errorString());
            emit logLine(error, MessageLevel::Fatal);
            emitFailed(error);
            return;
        }
        case LoggedProcess::Running:
            emit logLine(tr("Process ID: %1").arg(m_process.processId()), MessageLevel::Launcher);
            m_parent->setPid(m_process.processId());
            emit readyForLaunch();
            break;
        case LoggedProcess::Aborted:
            finishGame(true);
            break;
        case LoggedProcess::Crashed:
        case LoggedProcess::Finished:
            if (state == LoggedProcess::Crashed && !m_aborted && !m_job && !m_crash) {
                // without a job there are no exception reports; the started program's own crash is all there is
                m_crash = GameCrash{ .program = QFileInfo(m_process.program()).fileName(),
                                     .code = static_cast<quint32>(m_process.exitCode()),
                                     .started = m_started };
            }
            if (!m_aborted && activeJobProcesses() > 0) {
                emit logLine(tr("%1 exited with code %2; waiting for the processes it started.").arg(m_executable.name).arg(m_process.exitCode()),
                             MessageLevel::Launcher);
                m_jobPoll.start();
                return;
            }
            finishGame(state == LoggedProcess::Crashed || m_process.exitCode() != 0);
            break;
        default:
            break;
    }
}

void LaunchGameProcess::proceed()
{
    if (!m_gameRunning) {
        m_gameRunning = true;
        m_parent->instance()->setGameRunning(true);
    }
}

int LaunchGameProcess::activeJobProcesses() const
{
#ifdef Q_OS_WIN
    if (!m_job) {
        return 0;
    }
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
    if (!QueryInformationJobObject(static_cast<HANDLE>(m_job), JobObjectBasicAccountingInformation, &info, sizeof(info), nullptr)) {
        return 0;
    }
    return static_cast<int>(info.ActiveProcesses);
#else
    return 0;
#endif
}

void LaunchGameProcess::watchJob()
{
    if (activeJobProcesses() > 0) {
        return;
    }
    m_jobPoll.stop();
    emit logLine(tr("All game processes have exited."), MessageLevel::Launcher);
    finishGame(false);
}

void LaunchGameProcess::finishGame(bool crashed)
{
    m_jobPoll.stop();
    stopReadingJobMessages();
    m_parent->setPid(-1);
    if (m_gameRunning) {
        m_gameRunning = false;
        m_parent->instance()->setGameRunning(false);
    }
    if (m_parent->instance()->settings()->get("CloseAfterLaunch").toBool()) {
        APPLICATION->showMainWindow();
    }
    if (m_aborted) {
        emitAborted();
        return;
    }
    if (m_crash) {
        m_parent->setCrash(*m_crash);
        const auto error = tr("%1 crashed: %2.").arg(m_crash->program, CrashReport::describeCode(m_crash->code));
        emit logLine(error, MessageLevel::Fatal);
        emitFailed(error);
        return;
    }
    if (crashed) {
        emitFailed(tr("%1 exited with code %2.").arg(m_executable.name).arg(m_process.exitCode()));
        return;
    }
    emitSucceeded();
}

bool LaunchGameProcess::abort()
{
    m_aborted = true;
#ifdef Q_OS_WIN
    if (m_job) {
        TerminateJobObject(static_cast<HANDLE>(m_job), 1);
    }
#endif
    if (m_process.state() == LoggedProcess::Running || m_process.state() == LoggedProcess::Starting) {
        m_process.kill();
    } else if (m_jobPoll.isActive()) {
        m_jobPoll.stop();
        finishGame(true);
    }
    return true;
}
