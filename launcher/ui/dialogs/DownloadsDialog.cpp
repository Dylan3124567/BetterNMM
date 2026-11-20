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

#include "DownloadsDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "StringUtils.h"
#include "net/DownloadManager.h"

namespace {

enum Column { FileColumn, ProgressColumn, SpeedColumn, StatusColumn };

QString statusText(const DownloadItem& item)
{
    switch (item.state) {
        case DownloadItem::State::Queued:
            return QObject::tr("Waiting for a free slot");
        case DownloadItem::State::Running:
            return QObject::tr("Downloading");
        case DownloadItem::State::Retrying:
            return QObject::tr("Resuming shortly (%1)").arg(item.error);
        case DownloadItem::State::Paused:
            return QObject::tr("Paused");
        case DownloadItem::State::Failed:
            return QObject::tr("Stopped: %1").arg(item.error);
        case DownloadItem::State::Done:
            return item.instanceId.isEmpty() ? QObject::tr("Done") : QObject::tr("Done, installing into %1").arg(item.instanceId);
    }
    return {};
}

}  // namespace

DownloadsDialog::DownloadsDialog(DownloadManager* manager, QWidget* parent) : QDialog(parent), m_manager(manager)
{
    setWindowTitle(tr("Downloads"));
    setModal(false);
    auto* layout = new QVBoxLayout(this);
    m_summary = new QLabel;
    layout->addWidget(m_summary);
    m_list = new QTreeWidget;
    m_list->setRootIsDecorated(false);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setHeaderLabels({ tr("File"), tr("Progress"), tr("Speed"), tr("Status") });
    m_list->header()->setSectionResizeMode(FileColumn, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(StatusColumn, QHeaderView::Stretch);
    m_list->header()->setStretchLastSection(false);
    m_list->setColumnWidth(ProgressColumn, 220);
    m_list->setColumnWidth(SpeedColumn, 90);
    layout->addWidget(m_list);

    auto* buttons = new QHBoxLayout;
    m_pause = new QPushButton(tr("&Pause"));
    m_resume = new QPushButton(tr("&Resume"));
    m_cancel = new QPushButton(tr("&Cancel Download"));
    m_clear = new QPushButton(tr("C&lear Finished"));
    for (auto* button : { m_pause, m_resume, m_cancel }) {
        buttons->addWidget(button);
    }
    buttons->addStretch();
    buttons->addWidget(m_clear);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(close, &QDialogButtonBox::rejected, this, &QDialog::hide);
    connect(m_pause, &QPushButton::clicked, this, [this] {
        for (const auto& id : selectedIds()) {
            m_manager->pause(id);
        }
    });
    connect(m_resume, &QPushButton::clicked, this, [this] {
        for (const auto& id : selectedIds()) {
            m_manager->resume(id);
        }
    });
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        for (const auto& id : selectedIds()) {
            m_manager->cancel(id);
        }
    });
    connect(m_clear, &QPushButton::clicked, m_manager, &DownloadManager::clearFinished);
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, &DownloadsDialog::updateButtons);
    connect(m_manager, &DownloadManager::added, this, &DownloadsDialog::addRow);
    connect(m_manager, &DownloadManager::changed, this, &DownloadsDialog::updateRow);
    connect(m_manager, &DownloadManager::removed, this, &DownloadsDialog::removeRow);

    for (const auto& item : m_manager->items()) {
        addRow(item.id);
    }
    updateSummary();
    updateButtons();
    resize(860, 380);
}

void DownloadsDialog::addRow(const QString& id)
{
    if (m_rows.contains(id)) {
        updateRow(id);
        return;
    }
    auto* row = new QTreeWidgetItem(m_list);
    row->setData(FileColumn, Qt::UserRole, id);
    auto* bar = new QProgressBar;
    bar->setTextVisible(true);
    m_list->setItemWidget(row, ProgressColumn, bar);
    m_rows.insert(id, row);
    updateRow(id);
}

void DownloadsDialog::updateRow(const QString& id)
{
    const auto* item = m_manager->item(id);
    auto* row = m_rows.value(id);
    if (!item || !row) {
        return;
    }
    row->setText(FileColumn, item->metadata.name.isEmpty() ? item->fileName() : QString("%1 (%2)").arg(item->metadata.name, item->fileName()));
    row->setToolTip(FileColumn, item->target);
    auto* bar = qobject_cast<QProgressBar*>(m_list->itemWidget(row, ProgressColumn));
    if (bar) {
        if (item->size > 0) {
            bar->setRange(0, 1000);
            bar->setValue(static_cast<int>(item->received * 1000 / item->size));
            bar->setFormat(QString("%1 / %2").arg(StringUtils::humanReadableFileSize(item->received), StringUtils::humanReadableFileSize(item->size)));
        } else {
            bar->setRange(0, item->state == DownloadItem::State::Done ? 1 : 0);
            bar->setValue(item->state == DownloadItem::State::Done ? 1 : 0);
            bar->setFormat(StringUtils::humanReadableFileSize(item->received));
        }
    }
    row->setText(SpeedColumn, item->state == DownloadItem::State::Running ? StringUtils::humanReadableFileSize(item->speed) + "/s" : QString());
    row->setText(StatusColumn, statusText(*item));
    row->setToolTip(StatusColumn, statusText(*item));
    updateSummary();
    updateButtons();
}

void DownloadsDialog::removeRow(const QString& id)
{
    delete m_rows.take(id);
    updateSummary();
    updateButtons();
}

void DownloadsDialog::updateSummary()
{
    int running = 0;
    int waiting = 0;
    qint64 speed = 0;
    for (const auto& item : m_manager->items()) {
        if (item.state == DownloadItem::State::Running) {
            running++;
            speed += item.speed;
        } else if (item.state == DownloadItem::State::Queued || item.state == DownloadItem::State::Retrying) {
            waiting++;
        }
    }
    m_summary->setText(running + waiting == 0 ? tr("Nothing is downloading.")
                                              : tr("%1 downloading at %2/s, %3 waiting. Unfinished downloads continue after a restart.")
                                                    .arg(running)
                                                    .arg(StringUtils::humanReadableFileSize(speed))
                                                    .arg(waiting));
}

void DownloadsDialog::updateButtons()
{
    bool pausable = false;
    bool resumable = false;
    const auto ids = selectedIds();
    for (const auto& id : ids) {
        if (const auto* item = m_manager->item(id)) {
            pausable |= item->state == DownloadItem::State::Queued || item->state == DownloadItem::State::Running ||
                        item->state == DownloadItem::State::Retrying;
            resumable |= item->state == DownloadItem::State::Paused || item->state == DownloadItem::State::Failed;
        }
    }
    m_pause->setEnabled(pausable);
    m_resume->setEnabled(resumable);
    m_cancel->setEnabled(!ids.isEmpty());
}

QStringList DownloadsDialog::selectedIds() const
{
    QStringList out;
    for (const auto* row : m_list->selectedItems()) {
        out << row->data(FileColumn, Qt::UserRole).toString();
    }
    return out;
}
