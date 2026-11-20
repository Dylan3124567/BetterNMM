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

#include "NexusDownloadQueue.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>

#include "FileSystem.h"
#include "net/DownloadManager.h"
#include "nexus/NxmLink.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ModInstallFlow.h"

NexusDownloadQueue::NexusDownloadQueue(QString program, QString profile, DownloadManager* downloads, QObject* parent)
    : QObject(parent), m_program(std::move(program)), m_profile(std::move(profile)), m_downloads(downloads)
{}

NexusDownloadQueue::~NexusDownloadQueue()
{
    if (m_window) {
        m_window->disconnect(this);
        // without input the window closes, like BetterNMM itself is doing
        m_window->closeWriteChannel();
        if (!m_window->waitForFinished(5000)) {
            m_window->kill();
            m_window->waitForFinished(5000);
        }
    }
}

bool NexusDownloadQueue::isAvailable() const
{
    return QFileInfo(m_program).isFile();
}

void NexusDownloadQueue::add(const QList<Request>& requests, QWidget* parent)
{
    m_parent = parent;
    for (const auto& request : requests) {
        Item item;
        item.id = m_nextId++;
        item.request = request;
        m_items.append(item);
    }
    if (m_installing) {
        return;  // the window opens again once the installs are done
    }
    if (!m_window) {
        startWindow();
        return;
    }
    for (auto& item : m_items) {
        if (item.state == Item::State::Waiting) {
            send(item);
        }
    }
}

void NexusDownloadQueue::startWindow()
{
    if (!isAvailable()) {
        for (auto& item : m_items) {
            if (item.state == Item::State::Waiting) {
                item.state = Item::State::Failed;
                item.error = tr("BetterNMM's Nexus Mods window is missing (%1). Install BetterNMM again.").arg(QDir::toNativeSeparators(m_program));
            }
        }
        installAll();
        return;
    }
    FS::ensureFolderPathExists(m_profile);
    // Chromium's errors go to the profile's browser.log; its other output is dropped below
    QStringList arguments{ "--profile=" + QDir::toNativeSeparators(m_profile), "--enable-logging=file", "--log-level=2",
                           "--log-file=" + QDir::toNativeSeparators(FS::PathCombine(m_profile, "browser.log")) };
    // written every time: a copy kept from an older version would show its logo
    if (const auto icon = FS::PathCombine(m_profile, "icon.png"); QGuiApplication::windowIcon().pixmap(64, 64).save(icon, "PNG")) {
        arguments << "--icon=" + QDir::toNativeSeparators(icon);
    }
    m_window = new QProcess(this);
    m_window->setProgram(m_program);
    m_window->setArguments(arguments);
    m_window->setStandardErrorFile(QProcess::nullDevice());
    connect(m_window, &QProcess::readyReadStandardOutput, this, &NexusDownloadQueue::readOutput);
    connect(m_window, &QProcess::finished, this, &NexusDownloadQueue::windowClosed);
    connect(m_window, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        for (auto& item : m_items) {
            if (item.state == Item::State::Sent) {
                item.state = Item::State::Failed;
                item.error = tr("BetterNMM's Nexus Mods window could not start: %1").arg(m_window->errorString());
            }
        }
        m_window->deleteLater();
        m_window = nullptr;
        installAll();
    });
    m_starts++;
    m_window->start();
    for (auto& item : m_items) {
        if (item.state == Item::State::Waiting) {
            send(item);
        }
    }
}

void NexusDownloadQueue::send(Item& item)
{
    auto title = item.request.title;
    title.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ');
    m_window->write(QString("item\t%1\t%2\t%3\n").arg(item.id).arg(item.request.page.toString(QUrl::FullyEncoded), title).toUtf8());
    item.state = Item::State::Sent;
}

NexusDownloadQueue::Item* NexusDownloadQueue::item(const QString& id)
{
    for (auto& candidate : m_items) {
        if (QString::number(candidate.id) == id) {
            return &candidate;
        }
    }
    return nullptr;
}

void NexusDownloadQueue::readOutput()
{
    m_output += m_window->readAllStandardOutput();
    for (auto end = m_output.indexOf('\n'); end >= 0; end = m_output.indexOf('\n')) {
        const auto line = QString::fromUtf8(m_output.left(end)).trimmed();
        m_output.remove(0, end + 1);
        handle(line.split('\t'));
    }
}

void NexusDownloadQueue::handle(const QStringList& fields)
{
    if (fields.size() < 2) {
        return;
    }
    const auto& kind = fields[0];
    if (kind == "timing") {
        qDebug() << "Nexus Mods window:" << fields.mid(1).join(' ') << "ms";
        return;
    }
    auto* entry = item(fields[1]);
    if (!entry) {
        return;
    }
    if (kind == "queued") {
        entry->state = Item::State::Queued;
        m_starts = 0;  // the window works
    } else if (kind == "clicked") {
        entry->state = Item::State::Clicked;
    } else if (kind == "download" && fields.size() >= 4) {
        download(*entry, QUrl(fields[2]), fields[3]);
    } else if (kind == "nxm" && fields.size() >= 3) {
        entry->nxm = fields[2];
        entry->state = Item::State::Link;
    } else if (kind == "skipped") {
        entry->state = Item::State::Skipped;
    } else if (kind == "cancelled") {
        entry->state = Item::State::Cancelled;
    } else if (kind == "timeout") {
        entry->state = Item::State::Failed;
        entry->error = tr("Nexus Mods did not start the download within a minute of the click.");
    }
}

void NexusDownloadQueue::download(Item& item, const QUrl& url, const QString& suggestedName)
{
    auto name = FS::RemoveInvalidFilenameChars(item.request.fileName.isEmpty() ? suggestedName : item.request.fileName);
    if (name.isEmpty()) {
        name = FS::RemoveInvalidFilenameChars(QFileInfo(url.path()).fileName());
    }
    if (name.isEmpty()) {
        name = QString("nexus-file-%1").arg(item.request.metadata.nexusFileId);
    }
    item.archive = FS::PathCombine(item.request.downloadDir, name);
    // handed on: the download runs beside the others (a complete earlier one is used as it is) and installs once this
    // window has closed
    item.state = Item::State::Downloaded;
    const auto* instance = item.request.instance.data();
    DownloadItem download;
    download.url = url;
    download.target = item.archive;
    download.size = item.request.size;
    download.instanceId = instance ? instance->id() : QString();
    download.metadata = item.request.metadata;
    m_downloads->add(download);
}

void NexusDownloadQueue::windowClosed()
{
    readOutput();
    m_window->deleteLater();
    m_window = nullptr;
    m_output.clear();
    bool resend = false;
    for (auto& item : m_items) {
        if (item.state == Item::State::Sent) {
            // written as the window was closing, never taken: the next window gets it
            item.state = Item::State::Waiting;
            resend = true;
        } else if (item.state == Item::State::Queued || item.state == Item::State::Clicked) {
            item.state = Item::State::Failed;
            item.error = tr("The Nexus Mods window closed before this file's download started.");
        }
    }
    const bool waiting = std::any_of(m_items.begin(), m_items.end(), [](const Item& item) { return item.state == Item::State::Waiting; });
    // a window that takes nothing twice in a row does not work; better to say so than to keep starting it
    if ((resend || waiting) && m_starts < 2) {
        startWindow();
        return;
    }
    for (auto& item : m_items) {
        if (item.state == Item::State::Waiting) {
            item.state = Item::State::Failed;
            item.error = tr("BetterNMM's Nexus Mods window did not take this file.");
        }
    }
    installAll();
}

void NexusDownloadQueue::installAll()
{
    if (m_installing) {
        return;
    }
    m_installing = true;
    QStringList problems;
    for (auto& item : m_items) {
        auto* instance = item.request.instance.data();
        switch (item.state) {
            case Item::State::Downloaded:
                break;  // the download list has it, and installs it when it is complete
            case Item::State::Link:
                if (const auto link = NxmLink::parse(QUrl(item.nxm)); link && instance) {
                    ModInstallFlow::installNexusFile(instance, *link, m_parent);
                } else {
                    problems << tr("%1: Nexus Mods gave a link BetterNMM could not use (%2)").arg(item.request.title, item.nxm);
                }
                break;
            case Item::State::Failed:
                problems << tr("%1: %2").arg(item.request.title, item.error);
                break;
            case Item::State::Skipped:
            case Item::State::Cancelled:
                break;
            default:
                continue;  // still on its way
        }
        item.state = Item::State::Done;
    }
    m_items.removeIf([](const Item& item) { return item.state == Item::State::Done; });
    m_installing = false;
    if (!problems.isEmpty()) {
        CustomMessageBox::selectable(m_parent, tr("Some Nexus Mods downloads did not finish"), problems.join("\n\n"), QMessageBox::Warning)->show();
    }
    emit closed();
    if (std::any_of(m_items.begin(), m_items.end(), [](const Item& item) { return item.state == Item::State::Waiting; })) {
        startWindow();  // queued while installing
    } else if (m_items.isEmpty()) {
        emit finished();
    }
}
