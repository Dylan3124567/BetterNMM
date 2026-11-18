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

#include "VortexImportDialog.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "Application.h"
#include "BuildConfig.h"
#include "InstanceList.h"
#include "QObjectPtr.h"
#include "game/GameInstanceCreationTask.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ProgressDialog.h"

VortexImportDialog::VortexImportDialog(const VortexImport::State& state, const QStringList& preselected, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Import from Vortex"));
    auto* layout = new QVBoxLayout(this);
    auto* explain = new QLabel(tr("Each profile you tick becomes an instance with its own copy of the game, holding the profile's mods "
                                  "(enabled as in Vortex, in Vortex's order), its plugin load order, settings and tools. Vortex, its "
                                  "mods and your saves are only read; nothing of them changes, and Vortex keeps working as before."),
                               this);
    explain->setWordWrap(true);
    layout->addWidget(explain);

    m_profiles = new QListWidget(this);
    for (const auto& profile : state.profiles) {
        const auto* game = state.game(profile.gameId);
        auto* item = new QListWidgetItem(m_profiles);
        const auto used =
            profile.lastActivated > 0
                ? tr(", last used %1")
                      .arg(QLocale().toString(QDateTime::fromMSecsSinceEpoch(profile.lastActivated).date(), QLocale::ShortFormat))
                : QString();
        item->setText(tr("%1: %2  (%n mod(s) enabled%3)", nullptr, static_cast<int>(profile.enabledMods.size()))
                          .arg(game->definition.name, profile.name, used));
        item->setData(Qt::UserRole, profile.id);
        if (game->gameDir.isEmpty() || !QFileInfo(game->gameDir).isDir()) {
            item->setFlags(Qt::NoItemFlags);
            item->setToolTip(
                tr("Vortex's folder of the game, %1, does not exist on this PC.").arg(QDir::toNativeSeparators(game->gameDir)));
            item->setCheckState(Qt::Unchecked);
            continue;
        }
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setToolTip(QDir::toNativeSeparators(game->gameDir));
        item->setCheckState(preselected.contains(profile.id) ? Qt::Checked : Qt::Unchecked);
    }
    if (preselected.isEmpty()) {
        // the profile used last is the likeliest one to want
        for (int i = 0; i < m_profiles->count(); i++) {
            if (m_profiles->item(i)->flags() & Qt::ItemIsUserCheckable) {
                m_profiles->item(i)->setCheckState(Qt::Checked);
                break;
            }
        }
    }
    layout->addWidget(m_profiles);

    m_saves = new QCheckBox(tr("Copy the saves (each instance gets its own copies; the originals stay as they are)"), this);
    m_saves->setChecked(true);
    layout->addWidget(m_saves);
    m_downloads = new QCheckBox(tr("Bring over the archives Vortex downloaded for these games, so mods can be reinstalled"), this);
    m_downloads->setChecked(true);
    layout->addWidget(m_downloads);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_import = buttons->button(QDialogButtonBox::Ok);
    m_import->setText(tr("&Import"));
    auto update = [this] { m_import->setEnabled(!chosen().isEmpty()); };
    connect(m_profiles, &QListWidget::itemChanged, this, update);
    update();
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(620, 420);
}

QStringList VortexImportDialog::chosen() const
{
    QStringList ids;
    for (int i = 0; i < m_profiles->count(); i++) {
        if (m_profiles->item(i)->checkState() == Qt::Checked) {
            ids << m_profiles->item(i)->data(Qt::UserRole).toString();
        }
    }
    return ids;
}

VortexImportTask::Options VortexImportDialog::options() const
{
    return { .saves = m_saves->isChecked(),
             .downloads = m_downloads->isChecked(),
             .downloadsRoot = QDir(APPLICATION->settings()->get("DownloadsDir").toString()).absolutePath() };
}

void VortexImportDialog::run(const QStringList& preselected, QWidget* parent)
{
    const auto title = tr("Import from Vortex");
    auto state = VortexImport::load();
    if (!state) {
        CustomMessageBox::selectable(parent, title, state.error(), QMessageBox::Information)->exec();
        return;
    }
    if (state->profiles.isEmpty()) {
        CustomMessageBox::selectable(parent, title,
                                     tr("Vortex has no profiles of games %1 supports.").arg(BuildConfig.LAUNCHER_DISPLAYNAME),
                                     QMessageBox::Information)
            ->exec();
        return;
    }
    VortexImportDialog dialog(*state, preselected, parent);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QStringList report;
    bool failed = false;
    for (const auto& id : dialog.chosen()) {
        const auto* profile = state->profile(id);
        const auto* game = profile ? state->game(profile->gameId) : nullptr;
        if (!game) {
            continue;
        }
        const auto name = QString("%1 (%2)").arg(game->definition.name, profile->name);
        // on the game's drive, so the instance's game files can be hard links
        const auto targetDir = APPLICATION->instances()->addInstanceDir(InstancePlacement::instanceDirFor(game->gameDir));
        if (targetDir.isEmpty()) {
            report << tr("%1: could not create a folder for it next to the game files.").arg(name);
            failed = true;
            continue;
        }
        QStringList others;
        for (const auto& other : state->profiles) {
            if (other.gameId == profile->gameId && other.id != profile->id) {
                others << other.id;
            }
        }
        auto* task = new VortexImportTask(*game, *profile, others, dialog.options());
        task->setName(name);
        task->setIcon(GameIcons::iconForGame(game->definition, game->gameDir));
        task->setTargetDir(targetDir);
        unique_qobject_ptr<Task> wrapped(APPLICATION->instances()->wrapInstanceTask(task));
        ProgressDialog progress(parent);
        progress.execWithTask(wrapped.get());
        if (!wrapped->wasSuccessful()) {
            failed = true;
            report << tr("%1: %2").arg(name, wrapped->failReason().isEmpty() ? tr("canceled.") : wrapped->failReason());
            continue;
        }
        QStringList lines{ name };
        for (const auto& line : task->messages()) {
            lines << "  " + line;
        }
        for (const auto& warning : wrapped->warnings()) {
            lines << "  " + tr("Note: %1").arg(warning);
        }
        report << lines.join('\n');
    }
    if (!report.isEmpty()) {
        CustomMessageBox::selectable(parent, title, report.join("\n\n"), failed ? QMessageBox::Warning : QMessageBox::Information)->exec();
    }
}
