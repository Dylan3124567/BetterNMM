// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (C) 2024 Tayou <git@tayou.org>
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
#include "BrightTheme.h"

#include <QObject>

QString BrightTheme::id()
{
    return "bright";
}

QString BrightTheme::name()
{
    return QObject::tr("Bright");
}

QPalette BrightTheme::colorScheme()
{
    // cool grey with the amber accent of the logo
    QPalette brightPalette;
    brightPalette.setColor(QPalette::Window, QColor(243, 244, 247));
    brightPalette.setColor(QPalette::WindowText, QColor(27, 30, 36));
    brightPalette.setColor(QPalette::Base, QColor(255, 255, 255));
    brightPalette.setColor(QPalette::AlternateBase, QColor(246, 247, 250));
    brightPalette.setColor(QPalette::ToolTipBase, QColor(30, 34, 43));
    brightPalette.setColor(QPalette::ToolTipText, QColor(243, 244, 247));
    brightPalette.setColor(QPalette::Text, QColor(27, 30, 36));
    brightPalette.setColor(QPalette::Button, QColor(250, 251, 252));
    brightPalette.setColor(QPalette::ButtonText, QColor(27, 30, 36));
    brightPalette.setColor(QPalette::BrightText, QColor(214, 40, 40));
    brightPalette.setColor(QPalette::Link, QColor(178, 94, 0));
    brightPalette.setColor(QPalette::LinkVisited, QColor(140, 74, 0));
    brightPalette.setColor(QPalette::Highlight, QColor(242, 161, 46));
    brightPalette.setColor(QPalette::HighlightedText, QColor(26, 18, 6));
    brightPalette.setColor(QPalette::PlaceholderText, QColor(128, 134, 148));
    brightPalette.setColor(QPalette::Light, QColor(255, 255, 255));
    brightPalette.setColor(QPalette::Midlight, QColor(233, 235, 240));
    brightPalette.setColor(QPalette::Mid, QColor(201, 205, 214));
    brightPalette.setColor(QPalette::Dark, QColor(154, 160, 173));
    brightPalette.setColor(QPalette::Shadow, QColor(107, 113, 128));
    return fadeInactive(brightPalette, fadeAmount(), fadeColor());
}

double BrightTheme::fadeAmount()
{
    return 0.5;
}

QColor BrightTheme::fadeColor()
{
    return QColor(243, 244, 247);
}

bool BrightTheme::hasStyleSheet()
{
    return true;
}

QString BrightTheme::appStyleSheet()
{
    return sleekStyleSheet(colorScheme());
}
QString BrightTheme::tooltip()
{
    return QString();
}
