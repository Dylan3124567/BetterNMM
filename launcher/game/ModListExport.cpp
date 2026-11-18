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

#include "ModListExport.h"

#include <QCoreApplication>

#include "nexus/NexusApi.h"

namespace ModListExport {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ModListExport", text);
}

QString pageOf(const ModEntry& mod)
{
    return mod.nexusModId > 0 && !mod.nexusGame.isEmpty() ? Nexus::modPageUrl(mod.nexusGame, mod.nexusModId).toString() : QString();
}

/// A table cell: a '|' would end it, a line break the row
QString cell(QString text)
{
    return text.replace('|', "\\|").replace('\n', ' ');
}

/// RFC 4180: quoted when it holds a separator, a quote or a line break
QString csvField(QString text)
{
    if (text.contains(',') || text.contains('"') || text.contains('\n') || text.contains('\r')) {
        return '"' + text.replace('"', "\"\"") + '"';
    }
    return text;
}

}  // namespace

Format formatFor(const QString& fileName)
{
    if (fileName.endsWith(".csv", Qt::CaseInsensitive)) {
        return Format::Csv;
    }
    if (fileName.endsWith(".txt", Qt::CaseInsensitive)) {
        return Format::Text;
    }
    return Format::Markdown;
}

QString write(const QString& title, const QList<ModEntry>& mods, const QStringList& plugins, Format format)
{
    int enabled = 0;
    for (const auto& mod : mods) {
        enabled += mod.enabled ? 1 : 0;
    }
    QStringList lines;
    switch (format) {
        case Format::Csv:
            lines << "Priority,Enabled,Name,Version,Nexus Mods page";
            for (int i = 0; i < mods.size(); i++) {
                const auto& mod = mods[i];
                lines << QString("%1,%2,%3,%4,%5")
                             .arg(i + 1)
                             .arg(mod.enabled ? QStringLiteral("yes") : QStringLiteral("no"))
                             .arg(csvField(mod.name), csvField(mod.version), csvField(pageOf(mod)));
            }
            return lines.join("\r\n") + "\r\n";
        case Format::Text:
            lines << title << QString(title.size(), '=') << "";
            lines << tr("%1 mods, %2 enabled, %3 active plugins").arg(mods.size()).arg(enabled).arg(plugins.size()) << "";
            lines << tr("Mods (lowest priority first: a later mod's files win)");
            for (int i = 0; i < mods.size(); i++) {
                const auto& mod = mods[i];
                auto line = QString("%1. [%2] %3").arg(i + 1, 3).arg(mod.enabled ? 'x' : ' ').arg(mod.name);
                if (!mod.version.isEmpty()) {
                    line += ' ' + mod.version;
                }
                if (const auto page = pageOf(mod); !page.isEmpty()) {
                    line += "  " + page;
                }
                lines << line;
            }
            lines << "" << tr("Load order");
            for (int i = 0; i < plugins.size(); i++) {
                lines << QString("%1. %2").arg(i + 1, 3).arg(plugins[i]);
            }
            return lines.join("\r\n") + "\r\n";
        case Format::Markdown:
            break;
    }
    lines << "# " + title << "";
    lines << tr("%1 mods, %2 enabled, %3 active plugins.").arg(mods.size()).arg(enabled).arg(plugins.size()) << "";
    lines << "## " + tr("Mods") << "" << tr("Lowest priority first: a later mod's files win.") << "";
    lines << "| # | " + tr("On") + " | " + tr("Mod") + " | " + tr("Version") + " | " + tr("Nexus Mods") + " |";
    lines << "|---|---|---|---|---|";
    for (int i = 0; i < mods.size(); i++) {
        const auto& mod = mods[i];
        const auto page = pageOf(mod);
        lines << QString("| %1 | %2 | %3 | %4 | %5 |")
                     .arg(i + 1)
                     .arg(mod.enabled ? QStringLiteral("✓") : QStringLiteral(" "))
                     .arg(cell(mod.name), cell(mod.version), page.isEmpty() ? QString(" ") : QString("[%1](%2)").arg(mod.nexusModId).arg(page));
    }
    if (!plugins.isEmpty()) {
        lines << "" << "## " + tr("Load order") << "";
        for (int i = 0; i < plugins.size(); i++) {
            lines << QString("%1. %2").arg(i + 1).arg(cell(plugins[i]));
        }
    }
    return lines.join('\n') + '\n';
}

}  // namespace ModListExport
