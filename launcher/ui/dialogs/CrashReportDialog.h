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

#include "game/CrashReport.h"
#include "launch/LaunchTask.h"

class GameInstance;
class QTextBrowser;

/// Shown when the game crashed: which mods the crash log names, plugins missing their masters, and mods that change the
/// same files, in plain words
class CrashReportDialog : public QDialog {
    Q_OBJECT
   public:
    CrashReportDialog(GameInstance* instance, const GameCrash& crash, QWidget* parent = nullptr);

    /// The report as HTML; separate so tests can read it without a window
    static QString toHtml(GameInstance* instance, const GameCrash& crash, const CrashReport::Report& report);

   private:
    QTextBrowser* m_text;
};
