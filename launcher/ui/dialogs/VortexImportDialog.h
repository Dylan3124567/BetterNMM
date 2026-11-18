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

#include <QDialog>

#include "vortex/VortexImport.h"

class QCheckBox;
class QListWidget;
class QPushButton;

/// Lists the Vortex profiles of games BetterNMM knows and asks which to bring over
class VortexImportDialog : public QDialog {
    Q_OBJECT
   public:
    /// @p preselected: profile ids ticked from the start (setup's choice); none ticks the most recently used one
    VortexImportDialog(const VortexImport::State& state, const QStringList& preselected, QWidget* parent = nullptr);

    QStringList chosen() const;
    VortexImportTask::Options options() const;

    /// Asks, then brings each chosen profile over as an instance, showing progress and finally what happened
    static void run(const QStringList& preselected, QWidget* parent);

   private:
    QListWidget* m_profiles = nullptr;
    QCheckBox* m_saves = nullptr;
    QCheckBox* m_downloads = nullptr;
    QPushButton* m_import = nullptr;
};
