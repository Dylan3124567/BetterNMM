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

#include <QFutureWatcher>
#include <atomic>
#include <memory>

#include "InstanceCopyPrefs.h"
#include "InstanceTask.h"
#include "Result.h"

class GameInstance;

/**
 * Copies a game instance into a new isolated instance. Profile, saves and mods are copied as chosen; the game folder is
 * rebuilt from the Steam installation so its files stay hard links instead of becoming copies.
 */
class InstanceCopyTask : public InstanceTask {
    Q_OBJECT
   public:
    InstanceCopyTask(GameInstance* origInstance, const InstanceCopyPrefs& prefs);

   protected:
    void executeTask() override;
    bool abort() override;

   private:
    Result<> copyFiles();
    void filesCopied();

    GameInstance* m_origInstance;
    InstanceCopyPrefs m_prefs;
    QFutureWatcher<Result<>> m_watcher;
    Task::Ptr m_sync;
    std::atomic_bool m_aborted = false;  ///< read by the copying worker
};
