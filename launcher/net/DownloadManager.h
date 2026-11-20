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

#include <QFile>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <memory>

#include "game/ModList.h"

class QNetworkAccessManager;
class QNetworkReply;

/// A file being downloaded, and what it is for
struct DownloadItem {
    enum class State { Queued, Running, Retrying, Paused, Failed, Done };

    QString id;
    QUrl url;
    QString target;   ///< where the finished file goes; until then the data is in target + ".part"
    qint64 size = 0;  ///< expected size; 0 when unknown

    QString instanceId;  ///< installed into this instance once complete; empty to only download
    ModEntry metadata;   ///< name, version and Nexus Mods ids of the mod it installs as
    QString nxmKey;      ///< the permission a Mod Manager Download link gave, to ask for a new address when this one expires
    qint64 nxmExpires = 0;

    State state = State::Queued;
    qint64 received = 0;  ///< bytes in the .part file
    qint64 speed = 0;     ///< bytes per second over the last second
    QString error;        ///< why it failed, or why it is retrying
    int failures = 0;     ///< attempts in a row that got no data

    QString fileName() const;
};

/**
 * Downloads several files at once into .part files that survive failures, pauses and restarts: a dropped or stalled
 * connection is resumed where it stopped (HTTP Range) after a short wait, and unfinished downloads continue when
 * BetterNMM starts again. An address that expired is replaced through refreshLink when it can be.
 */
class DownloadManager : public QObject {
    Q_OBJECT
   public:
    /// Gives a new address for the download, or an error to show; called when the server refuses the old one
    using LinkRefresher = std::function<void(const DownloadItem&, std::function<void(const QUrl&, const QString&)>)>;

    DownloadManager(QString stateFile, QNetworkAccessManager* network, QObject* parent = nullptr);
    ~DownloadManager() override;

    /// Starts the downloads saved by an earlier run
    void restore();
    void setMaxRunning(int count);
    void setUserAgent(const QString& agent) { m_userAgent = agent; }
    void setStallTimeout(int ms) { m_stallTimeout = ms; }
    void setRetryDelays(QList<int> ms) { m_retryDelays = std::move(ms); }
    void setLinkRefresher(LinkRefresher refresher) { m_refresh = std::move(refresher); }

    /// Adds a download, or continues the unfinished one with the same target with the new address; returns its id
    QString add(DownloadItem item);
    void pause(const QString& id);
    void resume(const QString& id);
    /// Stops the download and deletes what it got
    void cancel(const QString& id);
    /// Forgets finished, failed and cancelled downloads
    void clearFinished();

    QList<DownloadItem> items() const;
    const DownloadItem* item(const QString& id) const;
    int active() const;

   signals:
    void added(const QString& id);
    void changed(const QString& id);
    void removed(const QString& id);
    /// The file is complete at the item's target
    void completed(const QString& id);

   private:
    struct Transfer {
        QPointer<QNetworkReply> reply;
        std::shared_ptr<QFile> file;
        bool gotData = false;
        bool restartFromZero = false;
    };

    DownloadItem* find(const QString& id);
    void schedule();
    void begin(DownloadItem& item);
    void readData(const QString& id);
    void transferDone(const QString& id);
    void retryLater(DownloadItem& item, const QString& reason);
    void fail(DownloadItem& item, const QString& reason);
    void stop(const QString& id);
    void tick();
    void save() const;

    QString m_stateFile;
    QNetworkAccessManager* m_network;
    QList<DownloadItem> m_items;
    QHash<QString, Transfer> m_transfers;
    QHash<QString, qint64> m_lastReceived;
    QTimer m_ticker;
    int m_maxRunning = 6;
    int m_stallTimeout = 30000;
    QList<int> m_retryDelays{ 2000, 5000, 10000, 20000, 30000, 60000, 60000, 60000 };
    QString m_userAgent;
    LinkRefresher m_refresh;
};
