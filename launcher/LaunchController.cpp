// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2023 TheKodeToad <TheKodeToad@proton.me>
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

#include "LaunchController.h"
#include "Application.h"

#include "ui/InstanceWindow.h"
#include "ui/dialogs/CrashReportDialog.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ProgressDialog.h"

#include <QMessageBox>
#include <QPushButton>
#include <utility>

#include "BuildConfig.h"
#include "launch/LaunchTask.h"
#include "launch/steps/TextPrint.h"
#include "game/PluginList.h"

LaunchController::LaunchController() = default;

void LaunchController::executeTask()
{
    if (!m_instance) {
        emitFailed(tr("No instance specified!"));
        return;
    }
    launchInstance();
}

void LaunchController::launchInstance()
{
    Q_ASSERT(m_instance != nullptr);

    if (!m_instance->reloadSettings()) {
        QMessageBox::critical(m_parentWidget, tr("Error!"), tr("Couldn't load the instance settings."));
        emitFailed(tr("Couldn't load the instance settings."));
        return;
    }
    if (!loadOrderAccepted()) {
        emitAborted();
        return;
    }

    m_launcher = m_instance->createLaunchTask(m_executable);
    if (!m_launcher) {
        emitFailed(tr("Couldn't instantiate a launcher."));
        return;
    }

    const auto* console = qobject_cast<InstanceWindow*>(m_parentWidget);
    const auto showConsole = m_instance->settings()->get("ShowConsole").toBool();
    if (!console && showConsole) {
        APPLICATION->showInstanceWindow(m_instance);
    }
    connect(m_launcher, &LaunchTask::readyForLaunch, this, &LaunchController::readyForLaunch);
    connect(m_launcher, &LaunchTask::succeeded, this, &LaunchController::onSucceeded);
    connect(m_launcher, &LaunchTask::failed, this, &LaunchController::onFailed);
    connect(m_launcher, &LaunchTask::aborted, this, [this] { emitAborted(); });
    connect(m_launcher, &LaunchTask::requestProgress, this, &LaunchController::onProgressRequested);

    auto versionString =
        QString("%1 version: %2 (%3)").arg(BuildConfig.LAUNCHER_DISPLAYNAME, BuildConfig.printableVersionString(), BuildConfig.BUILD_PLATFORM);
    m_launcher->prependStep(makeShared<TextPrint>(m_launcher, versionString + QChar::LineFeed, MessageLevel::Launcher));
    m_launcher->start();
}

QStringList LaunchController::loadOrderProblems(GameInstance* instance, const QString& executableName)
{
    const auto name = executableName.isEmpty() ? instance->defaultExecutableName() : executableName;
    if (!instance->executable(name).builtIn || !instance->definition().hasPlugins()) {
        return {};
    }
    auto* plugins = instance->pluginList();
    if (!plugins->refresh()) {
        return {};  // PrepareGame reports what is wrong with the load order files
    }
    return plugins->loadProblems();
}

bool LaunchController::loadOrderAccepted()
{
    const auto problems = loadOrderProblems(m_instance, m_executable);
    if (problems.isEmpty()) {
        return true;
    }
    constexpr int shown = 12;
    auto listed = problems.mid(0, shown);
    if (problems.size() > shown) {
        listed << tr("and %n more", nullptr, static_cast<int>(problems.size() - shown));
    }
    QMessageBox box(QMessageBox::Warning, tr("Load order problems"),
                    tr("%1 will most likely crash while loading, or leave plugins out:").arg(m_instance->definition().name),
                    QMessageBox::NoButton, m_parentWidget);
    box.setInformativeText(listed.join("\n") + "\n\n" + tr("The Plugins page marks them in red."));
    auto* launch = box.addButton(tr("Launch Anyway"), QMessageBox::AcceptRole);
    box.setDefaultButton(box.addButton(QMessageBox::Cancel));
    box.exec();
    return box.clickedButton() == launch;
}

void LaunchController::readyForLaunch()
{
    if (m_instance->settings()->get("CloseAfterLaunch").toBool()) {
        // only hide a main window that exists; the game's exit brings it back
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* window = qobject_cast<MainWindow*>(widget)) {
                window->hide();
            }
        }
    }
    m_launcher->proceed();
}

void LaunchController::onSucceeded()
{
    emitSucceeded();
}

void LaunchController::onFailed(QString reason)
{
    if (m_instance->settings()->get("ShowConsoleOnError").toBool()) {
        APPLICATION->showInstanceWindow(m_instance, "console");
    }
    if (const auto crash = m_launcher->crash()) {
        // a window of its own, so it stays after the launch is done
        auto* report = new CrashReportDialog(m_instance, *crash, m_parentWidget);
        report->show();
        report->raise();
    }
    emitFailed(std::move(reason));
}

void LaunchController::onProgressRequested(Task* task) const
{
    ProgressDialog progDialog(m_parentWidget);
    progDialog.showSkipButton();
    m_launcher->proceed();
    progDialog.execWithTask(task);
}

bool LaunchController::abort()
{
    if (!m_launcher) {
        return true;
    }
    if (!m_launcher->canAbort()) {
        return false;
    }
    auto response = CustomMessageBox::selectable(m_parentWidget, tr("Kill the game?"),
                                                 tr("This closes the game and everything it started without saving. Use it only when "
                                                    "the game is frozen."),
                                                 QMessageBox::Question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                        ->exec();
    if (response == QMessageBox::Yes) {
        return m_launcher->abort();
    }
    return false;
}
