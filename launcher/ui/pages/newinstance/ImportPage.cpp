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

#include "ImportPage.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "Application.h"
#include "InstanceImportTask.h"
#include "InstanceList.h"
#include "game/GameInstanceCreationTask.h"
#include "ui/dialogs/ModInstallFlow.h"
#include "ui/dialogs/NewInstanceDialog.h"

ImportPage::ImportPage(NewInstanceDialog* dialog, QWidget* parent) : QWidget(parent), m_dialog(dialog)
{
    auto* layout = new QVBoxLayout(this);
    auto* explain = new QLabel(tr("Import an instance exported with BetterNMM (Export > BetterNMM (zip)). Its mods, load order, settings "
                                  "and saves come along; its game folder is rebuilt from your own installation of the game."),
                               this);
    explain->setWordWrap(true);
    layout->addWidget(explain);
    auto* row = new QHBoxLayout();
    m_path = new QLineEdit(this);
    m_path->setPlaceholderText(tr("Exported instance (.zip)"));
    auto* browse = new QPushButton(tr("Browse..."), this);
    row->addWidget(m_path, 1);
    row->addWidget(browse);
    layout->addLayout(row);
    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    layout->addWidget(m_info);
    layout->addStretch(1);

    connect(browse, &QPushButton::clicked, this, [this] {
        const auto file = QFileDialog::getOpenFileName(this, tr("Import instance"), QString(), tr("Exported instances (*.zip)"));
        if (!file.isEmpty()) {
            setArchive(file);
        }
    });
    connect(m_path, &QLineEdit::editingFinished, this, &ImportPage::update);
}

void ImportPage::setArchive(const QString& path)
{
    m_path->setText(QDir::toNativeSeparators(path));
    update();
}

void ImportPage::update()
{
    const auto path = QDir::fromNativeSeparators(m_path->text().trimmed());
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        m_info->setText(tr("Choose an exported instance."));
        m_dialog->setSuggestedPack(QString(), nullptr);
        return;
    }
    if (!ModInstallFlow::isInstanceArchive(path)) {
        m_info->setText(tr("This archive is not an exported instance. Install mod archives from the instance's Mods page instead."));
        m_dialog->setSuggestedPack(QString(), nullptr);
        return;
    }
    // put the instance on the game's drive, so its game files can be hard links
    const auto gameDir = InstanceImportTask::gameDirFor(path);
    if (gameDir.isEmpty()) {
        m_info->setText(tr("The game this instance is for is not installed here. Install it with Steam first."));
        m_dialog->setSuggestedPack(QString(), nullptr);
        return;
    }
    m_info->setText(tr("The game folder will be built from %1.").arg(QDir::toNativeSeparators(gameDir)));
    m_dialog->setTargetDir(InstancePlacement::instanceDirFor(gameDir));
    m_dialog->setSuggestedPack(QFileInfo(path).completeBaseName(), new InstanceImportTask(path));
}
