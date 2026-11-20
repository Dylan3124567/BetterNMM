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

#include "DownloadManager.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUuid>
#include <algorithm>

#include "FileSystem.h"

namespace {

QString partPath(const DownloadItem& item)
{
    return item.target + ".part";
}

QString stateName(DownloadItem::State state)
{
    switch (state) {
        case DownloadItem::State::Paused:
            return "paused";
        case DownloadItem::State::Failed:
            return "failed";
        default:
            return "queued";
    }
}

}  // namespace

QString DownloadItem::fileName() const
{
    return QFileInfo(target).fileName();
}

DownloadManager::DownloadManager(QString stateFile, QNetworkAccessManager* network, QObject* parent)
    : QObject(parent), m_stateFile(std::move(stateFile)), m_network(network)
{
    m_ticker.setInterval(1000);
    connect(&m_ticker, &QTimer::timeout, this, &DownloadManager::tick);
}

DownloadManager::~DownloadManager()
{
    // the .part files stay; the next start continues them
    for (auto it = m_transfers.begin(); it != m_transfers.end(); ++it) {
        if (it->reply) {
            it->reply->disconnect(this);
            it->reply->abort();
            it->reply->deleteLater();
        }
    }
    m_transfers.clear();
}

void DownloadManager::restore()
{
    QFile file(m_stateFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    for (const auto& value : QJsonDocument::fromJson(file.readAll()).array()) {
        const auto obj = value.toObject();
        DownloadItem item;
        item.id = obj.value("id").toString();
        item.url = QUrl(obj.value("url").toString());
        item.target = obj.value("target").toString();
        item.size = obj.value("size").toInteger();
        item.instanceId = obj.value("instance").toString();
        const auto mod = obj.value("mod").toObject();
        item.metadata.name = mod.value("name").toString();
        item.metadata.version = mod.value("version").toString();
        item.metadata.latestVersion = mod.value("latestVersion").toString();
        item.metadata.nexusGame = mod.value("game").toString();
        item.metadata.nexusModId = mod.value("modId").toInt();
        item.metadata.nexusFileId = mod.value("fileId").toInt();
        item.nxmKey = obj.value("nxmKey").toString();
        item.nxmExpires = obj.value("nxmExpires").toInteger();
        const auto state = obj.value("state").toString();
        item.state = state == "paused" ? DownloadItem::State::Paused : state == "failed" ? DownloadItem::State::Failed : DownloadItem::State::Queued;
        item.error = obj.value("error").toString();
        if (item.id.isEmpty() || item.target.isEmpty() || find(item.id)) {
            continue;
        }
        item.received = QFileInfo(partPath(item)).size();
        m_items << item;
        emit added(item.id);
    }
    schedule();
}

void DownloadManager::setMaxRunning(int count)
{
    m_maxRunning = qMax(1, count);
    schedule();
}

DownloadItem* DownloadManager::find(const QString& id)
{
    for (auto& item : m_items) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

const DownloadItem* DownloadManager::item(const QString& id) const
{
    return const_cast<DownloadManager*>(this)->find(id);
}

QList<DownloadItem> DownloadManager::items() const
{
    return m_items;
}

int DownloadManager::active() const
{
    return static_cast<int>(std::count_if(m_items.begin(), m_items.end(), [](const DownloadItem& item) {
        return item.state == DownloadItem::State::Queued || item.state == DownloadItem::State::Running || item.state == DownloadItem::State::Retrying;
    }));
}

QString DownloadManager::add(DownloadItem item)
{
    // a second link for a file still on its way (another click, a fresh address) continues it
    for (auto& existing : m_items) {
        if (existing.state != DownloadItem::State::Done && existing.target.compare(item.target, Qt::CaseInsensitive) == 0) {
            existing.url = item.url;
            if (item.size > 0) {
                existing.size = item.size;
            }
            if (!item.instanceId.isEmpty()) {
                existing.instanceId = item.instanceId;
                existing.metadata = item.metadata;
            }
            if (!item.nxmKey.isEmpty()) {
                existing.nxmKey = item.nxmKey;
                existing.nxmExpires = item.nxmExpires;
            }
            existing.failures = 0;
            if (existing.state == DownloadItem::State::Paused || existing.state == DownloadItem::State::Failed) {
                existing.state = DownloadItem::State::Queued;
                existing.error.clear();
            }
            save();
            emit changed(existing.id);
            schedule();
            return existing.id;
        }
    }
    if (item.id.isEmpty()) {
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    const auto id = item.id;
    if (item.size > 0 && QFileInfo(item.target).size() == item.size) {
        item.state = DownloadItem::State::Done;
        item.received = item.size;
        m_items << item;
        emit added(id);
        emit completed(id);
        return id;
    }
    item.state = DownloadItem::State::Queued;
    item.received = QFileInfo(partPath(item)).size();
    m_items << item;
    save();
    emit added(id);
    schedule();
    return id;
}

void DownloadManager::schedule()
{
    for (auto& item : m_items) {
        if (m_transfers.size() >= m_maxRunning) {
            break;
        }
        if (item.state == DownloadItem::State::Queued) {
            begin(item);
        }
    }
}

void DownloadManager::begin(DownloadItem& item)
{
    const auto part = partPath(item);
    auto from = QFileInfo(part).size();
    if (item.size > 0 && from > item.size) {
        QFile::remove(part);
        from = 0;
    }
    if (!FS::ensureFilePathExists(part)) {
        fail(item, tr("Could not create the folder for %1.").arg(item.target));
        return;
    }
    Transfer transfer;
    transfer.file = std::make_shared<QFile>(part);
    if (!transfer.file->open(QIODevice::WriteOnly | QIODevice::Append)) {
        fail(item, tr("Could not write %1: %2").arg(part, transfer.file->errorString()));
        return;
    }
    item.received = from;
    item.state = DownloadItem::State::Running;

    QNetworkRequest request(item.url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    // a connection that stops delivering is dropped and resumed, rather than waited on
    request.setTransferTimeout(m_stallTimeout);
    if (!m_userAgent.isEmpty()) {
        request.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    }
    if (from > 0) {
        request.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(from) + '-');
    }
    transfer.reply = m_network->get(request);
    const auto id = item.id;
    connect(transfer.reply, &QNetworkReply::readyRead, this, [this, id] { readData(id); });
    connect(transfer.reply, &QNetworkReply::finished, this, [this, id] { transferDone(id); });
    m_transfers.insert(id, std::move(transfer));
    m_lastReceived.insert(id, from);
    if (!m_ticker.isActive()) {
        m_ticker.start();
    }
    emit changed(id);
}

void DownloadManager::readData(const QString& id)
{
    auto* item = find(id);
    auto it = m_transfers.find(id);
    if (!item || it == m_transfers.end() || !it->reply) {
        return;
    }
    auto& transfer = *it;
    // 0 for a link that is not HTTP (a local file), which sends the whole file like a server without ranges
    const int status = transfer.reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto data = transfer.reply->readAll();
    // a connection that failed has neither data nor a status; an error page is not part of the file
    if (data.isEmpty() || (status != 0 && status != 200 && status != 206)) {
        return;
    }
    if (!transfer.gotData && status != 206 && transfer.file->size() > 0) {
        // the server sends the whole file instead of the rest
        transfer.file->resize(0);
        item->received = 0;
    }
    if (transfer.file->write(data) != data.size()) {
        const auto error = transfer.file->errorString();
        stop(id);
        fail(*item, tr("Could not write %1: %2").arg(partPath(*item), error));
        schedule();
        return;
    }
    item->received += data.size();
    transfer.gotData = true;
}

void DownloadManager::transferDone(const QString& id)
{
    readData(id);
    auto* item = find(id);
    auto it = m_transfers.find(id);
    if (!item || it == m_transfers.end()) {
        return;  // a write error ended it already
    }
    auto transfer = std::move(*it);
    m_transfers.erase(it);
    transfer.file->close();
    auto* reply = transfer.reply.data();
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto error = reply->error();
    if (transfer.gotData) {
        item->failures = 0;
    }
    if (status == 416) {
        // asked for more than there is: either complete already, or the .part is not this file
        if (item->size > 0 && item->received == item->size) {
            item->error.clear();
        } else {
            QFile::remove(partPath(*item));
            item->received = 0;
            retryLater(*item, tr("The server did not accept resuming; starting over"));
            schedule();
            return;
        }
    } else if (status == 401 || status == 403 || status == 404 || status == 410) {
        // Nexus Mods download addresses expire
        if (!m_refresh) {
            fail(*item, tr("The download address no longer works (HTTP %1).").arg(status));
            schedule();
            return;
        }
        item->state = DownloadItem::State::Retrying;
        item->error = tr("Getting a new download address");
        emit changed(id);
        m_refresh(*item, [this, id](const QUrl& url, const QString& why) {
            auto* again = find(id);
            if (!again || again->state != DownloadItem::State::Retrying) {
                return;
            }
            if (url.isEmpty()) {
                fail(*again, why);
            } else {
                again->url = url;
                again->state = DownloadItem::State::Queued;
                again->error.clear();
                save();
            }
            schedule();
        });
        schedule();
        return;
    } else if (error != QNetworkReply::NoError) {
        retryLater(*item, reply->errorString());
        schedule();
        return;
    }
    if (item->size > 0 && item->received != item->size) {
        if (item->received > item->size) {
            QFile::remove(partPath(*item));
            item->received = 0;
        }
        retryLater(*item, tr("The connection ended early (%1 of %2 bytes)").arg(item->received).arg(item->size));
        schedule();
        return;
    }
    if (QFileInfo::exists(item->target) && !QFile::remove(item->target)) {
        fail(*item, tr("Could not replace %1.").arg(item->target));
        schedule();
        return;
    }
    if (!QFile::rename(partPath(*item), item->target)) {
        fail(*item, tr("Could not rename %1 to %2.").arg(partPath(*item), item->target));
        schedule();
        return;
    }
    item->state = DownloadItem::State::Done;
    item->received = QFileInfo(item->target).size();
    item->error.clear();
    item->speed = 0;
    save();
    emit changed(id);
    emit completed(id);
    schedule();
}

void DownloadManager::retryLater(DownloadItem& item, const QString& reason)
{
    if (item.failures >= m_retryDelays.size()) {
        fail(item, reason);
        return;
    }
    const int delay = m_retryDelays.at(item.failures);
    item.failures++;
    item.state = DownloadItem::State::Retrying;
    item.error = reason;
    item.speed = 0;
    qDebug() << "Download" << item.fileName() << "stopped:" << reason << "- resuming at" << item.received << "in" << delay << "ms";
    emit changed(item.id);
    QTimer::singleShot(delay, this, [this, id = item.id] {
        if (auto* again = find(id); again && again->state == DownloadItem::State::Retrying) {
            again->state = DownloadItem::State::Queued;
            schedule();
        }
    });
}

void DownloadManager::fail(DownloadItem& item, const QString& reason)
{
    qWarning() << "Download" << item.fileName() << "failed:" << reason;
    item.state = DownloadItem::State::Failed;
    item.error = reason;
    item.speed = 0;
    save();
    emit changed(item.id);
}

void DownloadManager::stop(const QString& id)
{
    auto it = m_transfers.find(id);
    if (it == m_transfers.end()) {
        return;
    }
    if (it->reply) {
        it->reply->disconnect(this);
        it->reply->abort();
        it->reply->deleteLater();
    }
    it->file->close();
    m_transfers.erase(it);
}

void DownloadManager::pause(const QString& id)
{
    auto* item = find(id);
    if (!item || item->state == DownloadItem::State::Done || item->state == DownloadItem::State::Failed) {
        return;
    }
    stop(id);
    item->state = DownloadItem::State::Paused;
    item->received = QFileInfo(partPath(*item)).size();
    item->speed = 0;
    save();
    emit changed(id);
    schedule();
}

void DownloadManager::resume(const QString& id)
{
    auto* item = find(id);
    if (!item || (item->state != DownloadItem::State::Paused && item->state != DownloadItem::State::Failed)) {
        return;
    }
    item->state = DownloadItem::State::Queued;
    item->failures = 0;
    item->error.clear();
    save();
    emit changed(id);
    schedule();
}

void DownloadManager::cancel(const QString& id)
{
    auto* item = find(id);
    if (!item) {
        return;
    }
    stop(id);
    if (item->state != DownloadItem::State::Done) {
        QFile::remove(partPath(*item));
    }
    m_items.removeIf([&id](const DownloadItem& candidate) { return candidate.id == id; });
    m_lastReceived.remove(id);
    save();
    emit removed(id);
    schedule();
}

void DownloadManager::clearFinished()
{
    QStringList gone;
    for (const auto& item : m_items) {
        if (item.state == DownloadItem::State::Done) {
            gone << item.id;
        }
    }
    m_items.removeIf([](const DownloadItem& item) { return item.state == DownloadItem::State::Done; });
    for (const auto& id : gone) {
        m_lastReceived.remove(id);
        emit removed(id);
    }
}

void DownloadManager::tick()
{
    if (m_transfers.isEmpty()) {
        m_ticker.stop();
    }
    for (auto& item : m_items) {
        if (item.state != DownloadItem::State::Running) {
            continue;
        }
        const auto last = m_lastReceived.value(item.id, item.received);
        item.speed = qMax<qint64>(0, item.received - last);
        m_lastReceived.insert(item.id, item.received);
        emit changed(item.id);
    }
}

void DownloadManager::save() const
{
    if (m_stateFile.isEmpty()) {
        return;
    }
    QJsonArray array;
    for (const auto& item : m_items) {
        if (item.state == DownloadItem::State::Done) {
            continue;
        }
        const QJsonObject mod{ { "name", item.metadata.name },       { "version", item.metadata.version },
                               { "latestVersion", item.metadata.latestVersion }, { "game", item.metadata.nexusGame },
                               { "modId", item.metadata.nexusModId }, { "fileId", item.metadata.nexusFileId } };
        array.append(QJsonObject{ { "id", item.id },
                                  { "url", item.url.toString(QUrl::FullyEncoded) },
                                  { "target", item.target },
                                  { "size", item.size },
                                  { "instance", item.instanceId },
                                  { "mod", mod },
                                  { "nxmKey", item.nxmKey },
                                  { "nxmExpires", item.nxmExpires },
                                  { "state", stateName(item.state) },
                                  { "error", item.error } });
    }
    if (auto written = FS::write(m_stateFile, QJsonDocument(array).toJson(QJsonDocument::Indented)); !written) {
        qWarning() << "Could not save the download list:" << written.error();
    }
}
