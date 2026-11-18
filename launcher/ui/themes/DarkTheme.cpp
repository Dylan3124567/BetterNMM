// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (C) 2024 Tayou <git@tayou.org>
 *  Copyright (C) 2024 TheKodeToad <TheKodeToad@proton.me>
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
#include "DarkTheme.h"

#include <QObject>

QString DarkTheme::id()
{
    return "dark";
}

QString DarkTheme::name()
{
    return QObject::tr("Dark");
}

QPalette DarkTheme::colorScheme()
{
    // slate with an amber accent: the logo's colours, and not Prism's grey and green
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(30, 34, 43));
    darkPalette.setColor(QPalette::WindowText, QColor(232, 234, 240));
    darkPalette.setColor(QPalette::Base, QColor(22, 25, 32));
    darkPalette.setColor(QPalette::AlternateBase, QColor(27, 31, 39));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(42, 48, 61));
    darkPalette.setColor(QPalette::ToolTipText, QColor(232, 234, 240));
    darkPalette.setColor(QPalette::Text, QColor(232, 234, 240));
    darkPalette.setColor(QPalette::Button, QColor(40, 45, 57));
    darkPalette.setColor(QPalette::ButtonText, QColor(232, 234, 240));
    darkPalette.setColor(QPalette::BrightText, QColor(255, 92, 92));
    darkPalette.setColor(QPalette::Link, QColor(255, 190, 92));
    darkPalette.setColor(QPalette::LinkVisited, QColor(230, 160, 70));
    darkPalette.setColor(QPalette::Highlight, QColor(242, 161, 46));
    darkPalette.setColor(QPalette::HighlightedText, QColor(26, 18, 6));
    darkPalette.setColor(QPalette::PlaceholderText, QColor(125, 132, 148));
    // frames and bevels from the same slate
    darkPalette.setColor(QPalette::Light, QColor(62, 69, 85));
    darkPalette.setColor(QPalette::Midlight, QColor(48, 54, 67));
    darkPalette.setColor(QPalette::Mid, QColor(38, 43, 54));
    darkPalette.setColor(QPalette::Dark, QColor(18, 21, 27));
    darkPalette.setColor(QPalette::Shadow, QColor(10, 12, 15));
    return fadeInactive(darkPalette, fadeAmount(), fadeColor());
}

double DarkTheme::fadeAmount()
{
    return 0.5;
}

QColor DarkTheme::fadeColor()
{
    return QColor(30, 34, 43);
}

bool DarkTheme::hasStyleSheet()
{
    return true;
}

QString DarkTheme::appStyleSheet()
{
    return sleekStyleSheet(colorScheme());
}

QString DarkTheme::tooltip()
{
    return "";
}
