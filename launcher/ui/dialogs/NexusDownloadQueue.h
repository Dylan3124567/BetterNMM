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

#include <QList>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QUrl>

#include "game/GameInstance.h"
#include "game/ModList.h"
#include "net/NetJob.h"

class QNetworkAccessManager;
class QWidget;

/**
 * Downloads for accounts without Premium. Nexus Mods gives them files only through the "Slow download" button on its
 * website, clicked by the person, so BetterNMM's Nexus Mods window (betternmm-browser.exe, Chromium) shows each queued
 * file's download page: one click per file, the next page already loaded. Every link the window reports downloads
 * into the instance's downloads folder right away; installing waits until the window has closed, so no installer
 * dialog interrupts the clicking.
 */
class NexusDownloadQueue : public QObject {
    Q_OBJECT
   public:
    struct Request {
        QPointer<GameInstance> instance;
        QUrl page;           ///< the file's download page (or its mod's files page, when the file is not known)
        QString title;       ///< what the window shows, like "SkyUI (SkyUI_5_2_SE.7z)"
        QString downloadDir; ///< where the archive goes
        QString fileName;    ///< the archive's name on Nexus Mods; empty to use the name the download comes with
        qint64 size = 0;     ///< the archive's size, when known (an existing complete download is reused)
        ModEntry metadata;   ///< what is known about the mod; Nexus ids included
    };

    /// @p program is the download window, @p profile the folder it keeps the Nexus Mods login in
    NexusDownloadQueue(QString program, QString profile, QNetworkAccessManager* network, QObject* parent = nullptr);
    ~NexusDownloadQueue() override;

    /// Whether the download window is installed
    bool isAvailable() const;
    /// Queues files; the window opens, or takes them if it is open. Dialogs (progress, installers) appear over @p parent.
    void add(const QList<Request>& requests, QWidget* parent);

   signals:
    /// Everything queued so far was handled: installed, skipped or reported
    void finished();

   private:
    struct Item {
        enum class State { Waiting, Sent, Queued, Clicked, Downloading, Downloaded, Link, Skipped, Cancelled, Failed, Done };
        int id = 0;
        Request request;
        State state = State::Waiting;
        QString archive;
        QString nxm;     ///< a Mod Manager Download link the page gave instead of a file
        QString error;
        NetJob::Ptr download;
    };

    void startWindow();
    void send(Item& item);
    void readOutput();
    void handle(const QStringList& fields);
    void download(Item& item, const QUrl& url, const QString& suggestedName);
    void windowClosed();
    void installAll();
    Item* item(const QString& id);

    QString m_program;
    QString m_profile;
    QNetworkAccessManager* m_network;
    QProcess* m_window = nullptr;
    QByteArray m_output;
    QList<Item> m_items;
    int m_nextId = 1;
    int m_starts = 0;  ///< window starts since one last took a file
    bool m_installing = false;
    QPointer<QWidget> m_parent;
};
