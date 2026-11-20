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

#include "CaptureToolOutput.h"

#include <QDateTime>
#include <QDir>
#include <QFutureWatcher>
#include <QtConcurrent>

#include "FileSystem.h"
#include "game/Deployer.h"
#include "game/GameInstance.h"
#include "game/ModList.h"
#include "launch/LaunchTask.h"

namespace {

/// A few of @p paths for the log, the rest counted
QString examples(const QStringList& paths)
{
    constexpr int shown = 10;
    QStringList out;
    for (const auto& path : paths.mid(0, shown)) {
        out << "  " + QDir::toNativeSeparators(path);
    }
    if (paths.size() > shown) {
        out << "  " + QObject::tr("and %n more", nullptr, static_cast<int>(paths.size() - shown));
    }
    return out.join('\n');
}

}  // namespace

SnapshotGameFolder::SnapshotGameFolder(LaunchTask* parent, std::shared_ptr<ToolRun> run) : LaunchStep(parent), m_run(std::move(run)) {}

void SnapshotGameFolder::executeTask()
{
    emit logLine(tr("Noting the game folder's files, so what %1 writes there goes into the mod \"%2\"...").arg(m_run->tool, m_run->outputMod),
                 MessageLevel::Launcher);
    auto* watcher = new QFutureWatcher<ToolOutput::Snapshot>(this);
    connect(watcher, &QFutureWatcher<ToolOutput::Snapshot>::finished, this, [this, watcher] {
        m_run->before = watcher->result();
        watcher->deleteLater();
        emitSucceeded();
    });
    watcher->setFuture(QtConcurrent::run([gameDir = m_parent->instance()->gameRoot()] { return ToolOutput::take(gameDir); }));
}

CaptureToolOutput::CaptureToolOutput(LaunchTask* parent, std::shared_ptr<ToolRun> run) : LaunchStep(parent), m_run(std::move(run)) {}

CaptureToolOutput::~CaptureToolOutput() = default;

void CaptureToolOutput::executeTask()
{
    if (!m_run->before) {
        emitSucceeded();  // the tool never started
        return;
    }
    auto* instance = m_parent->instance();
    auto manifest = DeploymentManifest::load(instance->deploymentManifestPath());
    if (!manifest) {
        const auto error = tr("Could not tell %1's files from the mods' files: %2").arg(m_run->tool, manifest.error());
        emit logLine(error, MessageLevel::Error);
        emitFailed(error);
        return;
    }
    QString outputId;
    for (const auto& mod : instance->modList()->mods()) {
        if (!mod.separator && mod.name == m_run->outputMod) {
            outputId = mod.id;
            break;
        }
    }
    auto* watcher = new QFutureWatcher<ToolOutput::Changes>(this);
    connect(watcher, &QFutureWatcher<ToolOutput::Changes>::finished, this, [this, watcher] {
        watcher->deleteLater();
        collect(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([gameDir = instance->gameRoot(), run = m_run, manifest = *manifest, outputId] {
        return ToolOutput::find(gameDir, *run->before, manifest, run->filter, outputId);
    }));
}

void CaptureToolOutput::collect(const ToolOutput::Changes& changes)
{
    auto* instance = m_parent->instance();
    if (!changes.changedGameFiles.isEmpty()) {
        emit logLine(tr("%1 changed %n file(s) of the game itself; they stay in the game folder:", nullptr,
                        static_cast<int>(changes.changedGameFiles.size()))
                             .arg(m_run->tool) +
                         '\n' + examples(changes.changedGameFiles),
                     MessageLevel::Warning);
    }
    if (changes.isEmpty()) {
        emit logLine(tr("%1 wrote no new files into the game folder.").arg(m_run->tool), MessageLevel::Launcher);
        m_run->before.reset();
        emitSucceeded();
        return;
    }

    // the output mod: made at the end of the list, so its files win, the first time; enabled, so they are used
    auto* mods = instance->modList();
    int row = -1;
    for (int i = 0; i < mods->mods().size(); i++) {
        if (!mods->mods()[i].separator && mods->mods()[i].name == m_run->outputMod) {
            row = i;
            break;
        }
    }
    QString id;
    if (row < 0) {
        id = mods->uniqueId(m_run->outputMod);
        if (!FS::ensureFolderPathExists(mods->modDir(id))) {
            const auto error = tr("Could not create the folder of the mod \"%1\": %2").arg(m_run->outputMod, QDir::toNativeSeparators(mods->modDir(id)));
            emit logLine(error, MessageLevel::Error);
            emitFailed(error);
            return;
        }
        mods->upsert(ModEntry{ .id = id, .name = m_run->outputMod, .enabled = true, .stamp = QDateTime::currentMSecsSinceEpoch() });
        emit logLine(tr("Made the mod \"%1\" for what %2 writes.").arg(m_run->outputMod, m_run->tool), MessageLevel::Launcher);
    } else {
        id = mods->mods()[row].id;
        if (!mods->mods()[row].enabled) {
            mods->setEnabled({ row }, true);
            emit logLine(tr("Enabled the mod \"%1\" so the game uses what %2 wrote.").arg(m_run->outputMod, m_run->tool), MessageLevel::Launcher);
        }
    }

    if (!changes.changedInPlace.isEmpty()) {
        QStringList lines;
        for (const auto& path : changes.changedInPlace) {
            const int owner = mods->indexOf(changes.modOf.value(path));
            lines << QString("  %1 (%2)").arg(QDir::toNativeSeparators(path), owner >= 0 ? mods->mods()[owner].name : changes.modOf.value(path));
        }
        emit logLine(tr("%1 wrote into %n file(s) of other mods; those mods' own copies have the change too. A copy is in \"%2\" now:",
                        nullptr, static_cast<int>(lines.size()))
                             .arg(m_run->tool, m_run->outputMod) +
                         '\n' + lines.mid(0, 10).join('\n'),
                     MessageLevel::Warning);
    }

    auto* watcher = new QFutureWatcher<Result<>>(this);
    connect(watcher, &QFutureWatcher<Result<>>::finished, this, [this, watcher, changes, instance] {
        watcher->deleteLater();
        if (const auto done = watcher->result(); !done) {
            m_problem = tr("Moving %1's files into \"%2\" stopped: %3").arg(m_run->tool, m_run->outputMod, done.error());
            emit logLine(m_problem, MessageLevel::Error);
        } else {
            const auto moved = changes.created + changes.replaced;
            if (!moved.isEmpty()) {
                emit logLine(tr("Moved %n file(s) %1 wrote into the mod \"%2\":", nullptr, static_cast<int>(moved.size())).arg(m_run->tool, m_run->outputMod) +
                                 '\n' + examples(moved),
                             MessageLevel::Launcher);
            }
        }
        m_run->before.reset();
        m_deploy = instance->createDeployTask();
        connect(m_deploy.get(), &Task::finished, this, &CaptureToolOutput::afterDeploy);
        m_deploy->start();
    });
    watcher->setFuture(QtConcurrent::run([gameDir = instance->gameRoot(), run = m_run, changes, modDir = mods->modDir(id)] {
        return ToolOutput::collect(gameDir, *run->before, changes, modDir);
    }));
}

void CaptureToolOutput::afterDeploy()
{
    if (!m_deploy->wasSuccessful()) {
        const auto error = tr("Deploying mods after %1 failed: %2").arg(m_run->tool, m_deploy->failReason());
        emit logLine(error, MessageLevel::Fatal);
        emitFailed(error);
        return;
    }
    emit logLine(tr("Mods deployed again (%1 files linked, %2 removed).").arg(m_deploy->added()).arg(m_deploy->removed()), MessageLevel::Launcher);
    for (const auto& warning : m_deploy->warnings()) {
        emit logLine(warning, MessageLevel::Warning);
    }
    if (!m_problem.isEmpty()) {
        emitFailed(m_problem);
        return;
    }
    emitSucceeded();
}
