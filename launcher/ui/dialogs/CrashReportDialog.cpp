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

#include "CrashReportDialog.h"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "Application.h"
#include "DesktopServices.h"
#include "MMCTime.h"
#include "game/GameDefinition.h"
#include "game/GameInstance.h"
#include "nexus/NexusApi.h"

namespace {

QString esc(const QString& text)
{
    return text.toHtmlEscaped();
}

}  // namespace

QString CrashReportDialog::toHtml(GameInstance* instance, const GameCrash& crash, const CrashReport::Report& report)
{
    const auto def = instance->definition();
    QString html;
    html += QString("<p>%1</p>")
                .arg(crash.started.isValid()
                         ? tr("<b>%1</b> stopped with %2, %3 after it started.")
                               .arg(esc(crash.program), esc(CrashReport::describeCode(crash.code)),
                                    Time::prettifyDuration(crash.started.secsTo(QDateTime::currentDateTime())))
                         : tr("<b>%1</b> stopped with %2.").arg(esc(crash.program), esc(CrashReport::describeCode(crash.code))));

    html += "<h3>" + tr("Mods the crash log names") + "</h3>";
    if (report.logPath.isEmpty()) {
        // the crash logger is part of the built-in game list, not of the instance's settings
        if (const auto known = GameRegistry::byDomain(def.domain); known.crashLoggerMod > 0) {
            html += "<p>" +
                    tr("No crash log was written. Install <a href=\"%1\">%2</a> into this instance; after the next crash this "
                       "shows which mods were involved.")
                        .arg(esc(Nexus::modPageUrl(def.domain, known.crashLoggerMod).toString()), esc(known.crashLogger)) +
                    "</p>";
        } else {
            html += "<p>" + tr("No crash log was written, so only the checks below can help.") + "</p>";
        }
    } else if (report.suspects.isEmpty()) {
        html += "<p>" + tr("The crash log names no file from your mods. The cause is more likely the game itself, a file a mod "
                           "is missing, or one of the problems below.") + "</p>";
    } else {
        html += "<p>" + tr("The more often a mod's files appear, the more likely it is involved. Disable the first one and try "
                           "again; if the game still crashes, go down the list.") + "</p><ol>";
        for (const auto& suspect : report.suspects) {
            QStringList files;
            for (auto it = suspect.files.begin(); it != suspect.files.end(); ++it) {
                files << tr("%1 (%2×)").arg(esc(it.key())).arg(it.value());
            }
            html += QString("<li><b>%1</b>: %2</li>").arg(esc(suspect.modName), files.join(", "));
        }
        html += "</ol>";
    }

    html += "<h3>" + tr("Mods made for another version of the game") + "</h3>";
    const auto gameVersion = report.gameVersion.section('.', 0, 2);
    if (report.wrongVersions.isEmpty()) {
        html += "<p>" +
                (gameVersion.isEmpty() ? tr("None of the active plugins is made for an older game.")
                                       : tr("None found: the script extender for your game (%1) loads every script extender plugin, "
                                            "and every active plugin is made for this game.")
                                             .arg(esc(gameVersion))) +
                "</p>";
    } else {
        html += "<p>" +
                tr("Your game is version %1. These files are made for another version; install the right file of their mod, or "
                   "disable it.")
                    .arg(esc(gameVersion.isEmpty() ? tr("unknown") : gameVersion)) +
                "</p><ul>";
        for (const auto& wrong : report.wrongVersions) {
            const auto from = wrong.modName.isEmpty() ? tr(" (not from a mod)") : tr(" from <b>%1</b>").arg(esc(wrong.modName));
            html += "<li><b>" + esc(QFileInfo(wrong.file).fileName()) + "</b>" + from + ": " + esc(wrong.reason) + "</li>";
        }
        html += "</ul>";
    }

    if (def.hasPlugins()) {
        html += "<h3>" + tr("Plugins missing their masters") + "</h3>";
        if (report.missingMasters.isEmpty()) {
            html += "<p>" + tr("None: every active plugin's masters are loaded.") + "</p>";
        } else {
            html += "<p>" + tr("The game crashes while loading a plugin whose masters are not loaded.") + "</p><ul>";
            for (const auto& missing : report.missingMasters) {
                const auto from = missing.pluginMod.isEmpty() ? QString() : tr(" (from %1)").arg(esc(missing.pluginMod));
                html += "<li>" +
                        (missing.presentButInactive
                             ? tr("<b>%1</b>%2 needs <b>%3</b>, which is installed but not active: activate it on the Plugins page.")
                             : tr("<b>%1</b>%2 needs <b>%3</b>, which is not installed: install the mod that has it, or deactivate %1."))
                            .arg(esc(missing.plugin), from, esc(missing.master)) +
                        "</li>";
            }
            html += "</ul>";
        }
        if (!report.tooManyPlugins.isEmpty()) {
            html += "<h3>" + tr("Too many plugins") + "</h3><ul>";
            for (const auto& problem : report.tooManyPlugins) {
                html += "<li>" + esc(problem) + "</li>";
            }
            html += "</ul>";
        }
    }

    html += "<h3>" + tr("Mods that change the same files") + "</h3>";
    if (report.conflicts.isEmpty()) {
        html += "<p>" + tr("No enabled mods share files.") + "</p>";
    } else {
        html += "<p>" + tr("The mod lower in the Mods list wins and its files are the ones the game gets. That is normal when a "
                           "mod is meant to replace another (patches do). Look at the pairs marked as named in the crash log "
                           "first: moving or disabling one of them changes what the game loads.") + "</p><ul>";
        constexpr int shown = 15;
        for (int i = 0; i < report.conflicts.size() && i < shown; i++) {
            const auto& conflict = report.conflicts[i];
            QStringList examples;
            for (const auto& example : conflict.examples) {
                examples << esc(QDir::toNativeSeparators(example));
            }
            html += "<li>" +
                    tr("<b>%1</b> overwrites <b>%2</b> in %3 file(s)%4, for example %5")
                        .arg(esc(conflict.winner), esc(conflict.loser))
                        .arg(conflict.files)
                        .arg(conflict.involvesSuspect ? tr(" — named in the crash log") : QString(), examples.join(", ")) +
                    "</li>";
        }
        html += "</ul>";
        if (report.conflicts.size() > shown) {
            html += "<p>" + tr("%1 more pairs; the Conflicts column of the Mods page shows them all.").arg(report.conflicts.size() - shown) +
                    "</p>";
        }
    }
    if (!report.logPath.isEmpty()) {
        html += "<p>" + tr("Crash log: %1").arg(esc(QDir::toNativeSeparators(report.logPath))) + "</p>";
    }
    return html;
}

CrashReportDialog::CrashReportDialog(GameInstance* instance, const GameCrash& crash, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("%1 crashed").arg(instance->name()));
    setAttribute(Qt::WA_DeleteOnClose);
    const auto report = CrashReport::analyze(instance, crash.started);

    auto* layout = new QVBoxLayout(this);
    m_text = new QTextBrowser(this);
    m_text->setOpenExternalLinks(true);
    m_text->setHtml(toHtml(instance, crash, report));
    layout->addWidget(m_text, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* openLog = buttons->addButton(tr("Open Crash Log"), QDialogButtonBox::ActionRole);
    openLog->setEnabled(!report.logPath.isEmpty());
    auto* showMods = buttons->addButton(tr("Show Mods"), QDialogButtonBox::ActionRole);
    auto* copy = buttons->addButton(tr("Copy Report"), QDialogButtonBox::ActionRole);
    copy->setToolTip(tr("Copy the report as text, for a bug report or a forum post."));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(openLog, &QPushButton::clicked, this, [path = report.logPath] { DesktopServices::openPath(path); });
    connect(showMods, &QPushButton::clicked, this, [instance] { APPLICATION->showInstanceWindow(instance, "mods"); });
    connect(copy, &QPushButton::clicked, this, [this] { QGuiApplication::clipboard()->setText(m_text->toPlainText()); });
    resize(720, 560);
}
