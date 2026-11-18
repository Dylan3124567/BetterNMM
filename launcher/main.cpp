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

#include <iostream>

#include "Application.h"

#if defined Q_OS_WIN32
#include <windows.h>

#include <shobjidl.h>

#include "BuildConfig.h"
#include "console/WindowsConsole.h"
#endif

int main(int argc, char* argv[])
{
#if defined Q_OS_WIN32
    // used on Windows to attach the standard IO streams
    console::WindowsConsoleGuard _consoleGuard;
    // the id setup gives the Start menu shortcut: the taskbar pin and these windows are then one taskbar button
    SetCurrentProcessExplicitAppUserModelID(reinterpret_cast<const wchar_t*>(BuildConfig.LAUNCHER_APPID.utf16()));
#endif

    // initialize Qt
    Application app(argc, argv);
    switch (app.status()) {
        case Application::StartingUp:
        case Application::Initialized: {
            Q_INIT_RESOURCE(legacy);
            Q_INIT_RESOURCE(backgrounds);
            Q_INIT_RESOURCE(documents);
            Q_INIT_RESOURCE(games);
            Q_INIT_RESOURCE(style);
            Q_INIT_RESOURCE(betternmm);

            Q_INIT_RESOURCE(pe_dark);
            Q_INIT_RESOURCE(pe_light);
            Q_INIT_RESOURCE(pe_blue);
            Q_INIT_RESOURCE(pe_colored);
            Q_INIT_RESOURCE(breeze_dark);
            Q_INIT_RESOURCE(breeze_light);
            Q_INIT_RESOURCE(flat);
            Q_INIT_RESOURCE(flat_white);

            return app.exec();
        }
        case Application::Failed:
            return 1;
        case Application::Succeeded:
            return 0;
        default:
            return -1;
    }
}
