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

#include <QFile>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <FileSystem.h>
#include <net/DownloadManager.h>

namespace {

/// A file server on localhost that can drop, stall, refuse or ignore ranges, to break downloads on purpose
class FileServer : public QObject {
   public:
    struct Rule {
        int dropAfter = -1;  ///< close the connection after this many body bytes...
        int dropTimes = 0;   ///< ...on this many requests
        int stallAfter = -1; ///< stop sending (connection left open) after this many body bytes...
        int stallTimes = 0;  ///< ...on this many requests
        int refuseTimes = 0; ///< answer 403 to this many requests
        bool ignoreRange = false;
        int chunk = 16 * 1024;
        int interval = 0;  ///< ms between chunks
    };

    QByteArray data;
    QHash<QString, Rule> rules;
    QHash<QString, int> requests;
    QHash<QString, QList<qint64>> ranges;  ///< start offset each request asked for (-1: no Range)
    int running = 0;
    int maxRunning = 0;

    FileServer()
    {
        auto* random = QRandomGenerator::global();
        data.resize(1024 * 1024 + 123);
        for (auto& byte : data) {
            byte = static_cast<char>(random->bounded(256));
        }
        QVERIFY(m_server.listen(QHostAddress::LocalHost));
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = m_server.nextPendingConnection()) {
                serve(socket);
            }
        });
    }

    QUrl url(const QString& path) const { return QUrl(QString("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path)); }

   private:
    QTcpServer m_server;

    void serve(QTcpSocket* socket)
    {
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
            *buffer += socket->readAll();
            if (!buffer->contains("\r\n\r\n")) {
                return;
            }
            socket->disconnect(socket, &QTcpSocket::readyRead, nullptr, nullptr);
            respond(socket, *buffer);
        });
    }

    void respond(QTcpSocket* socket, const QByteArray& head)
    {
        const auto lines = head.split('\n');
        const auto path = QString::fromLatin1(lines.first().split(' ').value(1));
        qint64 start = -1;
        for (const auto& raw : lines) {
            const auto line = raw.trimmed();
            if (line.toLower().startsWith("range: bytes=")) {
                start = line.mid(13).split('-').first().toLongLong();
            }
        }
        const int index = requests[path]++;
        ranges[path] << start;
        const auto rule = rules.value(path);
        if (index < rule.refuseTimes) {
            socket->write("HTTP/1.1 403 Forbidden\r\nContent-Length: 9\r\nConnection: close\r\n\r\nforbidden");
            socket->disconnectFromHost();
            return;
        }
        const qint64 from = rule.ignoreRange || start < 0 ? 0 : start;
        if (from > data.size()) {
            socket->write("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        QByteArray header = from > 0 ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
        if (from > 0) {
            header += QString("Content-Range: bytes %1-%2/%3\r\n").arg(from).arg(data.size() - 1).arg(data.size()).toLatin1();
        }
        header += QString("Content-Length: %1\r\nConnection: close\r\n\r\n").arg(data.size() - from).toLatin1();
        socket->write(header);
        running++;
        maxRunning = qMax(maxRunning, running);
        const int dropAt = index < rule.dropTimes ? rule.dropAfter : -1;
        const int stallAt = index < rule.stallTimes ? rule.stallAfter : -1;
        auto sent = std::make_shared<qint64>(0);
        auto* timer = new QTimer(socket);
        timer->setInterval(rule.interval);
        QPointer<QTcpSocket> guard(socket);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            running--;
            socket->deleteLater();
        });
        connect(timer, &QTimer::timeout, socket, [this, guard, timer, sent, from, dropAt, stallAt, rule] {
            if (!guard) {
                return;
            }
            qint64 limit = data.size() - from;
            if (dropAt >= 0) {
                limit = qMin<qint64>(limit, dropAt);
            }
            if (stallAt >= 0) {
                limit = qMin<qint64>(limit, stallAt);
            }
            const auto count = qMin<qint64>(rule.chunk, limit - *sent);
            if (count > 0) {
                guard->write(data.mid(from + *sent, count));
                *sent += count;
                return;
            }
            timer->stop();
            if (dropAt >= 0 && *sent >= dropAt) {
                guard->abort();
            } else if (stallAt < 0) {
                guard->disconnectFromHost();
            }  // a stall keeps the connection open, sending nothing
        });
        timer->start();
    }
};

QByteArray readAll(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

}  // namespace

class DownloadManagerTest : public QObject {
    Q_OBJECT

    QNetworkAccessManager m_network;

    DownloadItem item(const FileServer& server, const QString& path, const QString& target, bool withSize = true)
    {
        DownloadItem out;
        out.url = server.url(path);
        out.target = target;
        out.size = withSize ? server.data.size() : 0;
        return out;
    }

   private slots:
    void test_downloads_run_in_parallel_up_to_the_limit()
    {
        FileServer server;
        server.rules["/a"].interval = 2;
        QTemporaryDir dir;
        DownloadManager manager({}, &m_network);
        manager.setMaxRunning(2);
        QSignalSpy done(&manager, &DownloadManager::completed);
        for (int i = 0; i < 4; i++) {
            manager.add(item(server, "/a", dir.filePath(QString("file%1.7z").arg(i))));
        }
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 4, 30000);
        QCOMPARE(server.maxRunning, 2);
        for (int i = 0; i < 4; i++) {
            QCOMPARE(readAll(dir.filePath(QString("file%1.7z").arg(i))), server.data);
            QVERIFY(!QFile::exists(dir.filePath(QString("file%1.7z.part").arg(i))));
        }
    }

    void test_dropped_connection_resumes_where_it_stopped()
    {
        FileServer server;
        server.rules["/f"] = FileServer::Rule{ .dropAfter = 300000, .dropTimes = 2 };
        QTemporaryDir dir;
        DownloadManager manager({}, &m_network);
        manager.setRetryDelays({ 10, 10, 10 });
        QSignalSpy done(&manager, &DownloadManager::completed);
        const auto target = dir.filePath("mod.7z");
        manager.add(item(server, "/f", target));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
        QCOMPARE(readAll(target), server.data);
        // each attempt continues from what arrived before the connection was cut (an abort loses what was in flight)
        const auto starts = server.ranges["/f"];
        QCOMPARE(starts.size(), 3);
        QCOMPARE(starts[0], qint64(-1));
        QVERIFY(starts[1] > 0 && starts[1] <= 300000);
        QVERIFY(starts[2] > starts[1] && starts[2] <= starts[1] + 300000);
    }

    void test_stalled_connection_is_dropped_and_resumed()
    {
        FileServer server;
        server.rules["/s"] = FileServer::Rule{ .stallAfter = 200000, .stallTimes = 1 };
        QTemporaryDir dir;
        DownloadManager manager({}, &m_network);
        manager.setStallTimeout(300);
        manager.setRetryDelays({ 10 });
        QSignalSpy done(&manager, &DownloadManager::completed);
        const auto target = dir.filePath("mod.7z");
        manager.add(item(server, "/s", target));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
        QCOMPARE(readAll(target), server.data);
        QCOMPARE(server.ranges["/s"], (QList<qint64>{ -1, 200000 }));
    }

    void test_pause_keeps_the_data_and_resume_continues()
    {
        FileServer server;
        server.rules["/p"] = FileServer::Rule{ .chunk = 8192, .interval = 5 };
        QTemporaryDir dir;
        DownloadManager manager({}, &m_network);
        QSignalSpy done(&manager, &DownloadManager::completed);
        const auto target = dir.filePath("mod.7z");
        const auto id = manager.add(item(server, "/p", target));
        QTRY_VERIFY_WITH_TIMEOUT(manager.item(id)->received > 100000, 30000);
        manager.pause(id);
        QCOMPARE(manager.item(id)->state, DownloadItem::State::Paused);
        const auto kept = QFileInfo(target + ".part").size();
        QVERIFY(kept > 100000);
        QTest::qWait(200);
        QCOMPARE(QFileInfo(target + ".part").size(), kept);  // nothing arrives while paused
        server.rules["/p"].interval = 0;
        manager.resume(id);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
        QCOMPARE(readAll(target), server.data);
        QCOMPARE(server.ranges["/p"].last(), kept);
    }

    void test_unfinished_downloads_continue_after_a_restart()
    {
        FileServer server;
        server.rules["/r"] = FileServer::Rule{ .stallAfter = 400000, .stallTimes = 1 };
        QTemporaryDir dir;
        const auto state = dir.filePath("downloads.json");
        const auto target = dir.filePath("mod.7z");
        {
            DownloadManager first(state, &m_network);
            auto download = item(server, "/r", target);
            download.instanceId = "Skyrim Ultra";
            download.metadata.name = "Some Mod";
            download.metadata.nexusModId = 42;
            const auto id = first.add(download);
            QTRY_COMPARE_WITH_TIMEOUT(first.item(id)->received, qint64(400000), 30000);
        }  // BetterNMM closes mid-download
        QCOMPARE(QFileInfo(target + ".part").size(), qint64(400000));

        DownloadManager second(state, &m_network);
        QSignalSpy done(&second, &DownloadManager::completed);
        second.restore();
        QCOMPARE(second.items().size(), 1);
        QCOMPARE(second.items().first().instanceId, QString("Skyrim Ultra"));
        QCOMPARE(second.items().first().metadata.nexusModId, 42);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
        QCOMPARE(readAll(target), server.data);
        QCOMPARE(server.ranges["/r"].last(), qint64(400000));
    }

    void test_expired_address_is_replaced()
    {
        FileServer server;
        server.rules["/old"].refuseTimes = 100;
        QTemporaryDir dir;
        DownloadManager manager({}, &m_network);
        int asked = 0;
        manager.setLinkRefresher([&](const DownloadItem&, std::function<void(const QUrl&, const QString&)> answer) {
            asked++;
            answer(server.url("/new"), {});
        });
        QSignalSpy done(&manager, &DownloadManager::completed);
        const auto target = dir.filePath("mod.7z");
        manager.add(item(server, "/old", target));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
        QCOMPARE(asked, 1);
        QCOMPARE(readAll(target), server.data);  // the refusal page is not part of the file

        // without a way to get a new one, it fails with the reason and keeps what it has
        DownloadManager plain({}, &m_network);
        const auto id = plain.add(item(server, "/old", dir.filePath("other.7z")));
        QTRY_COMPARE_WITH_TIMEOUT(plain.item(id)->state, DownloadItem::State::Failed, 30000);
        QVERIFY(plain.item(id)->error.contains("403"));
    }

    void test_server_without_ranges_starts_over()
    {
        FileServer server;
        server.rules["/n"] = FileServer::Rule{ .dropAfter = 250000, .dropTimes = 1, .ignoreRange = true };
        QTemporaryDir dir;
        DownloadManager manager({}, &m_network);
        manager.setRetryDelays({ 10 });
        QSignalSpy done(&manager, &DownloadManager::completed);
        const auto target = dir.filePath("mod.7z");
        manager.add(item(server, "/n", target));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
        QCOMPARE(readAll(target), server.data);
    }

    void test_unreachable_server_keeps_what_was_downloaded()
    {
        QTemporaryDir dir;
        const auto target = dir.filePath("mod.7z");
        QFile part(target + ".part");
        QVERIFY(part.open(QIODevice::WriteOnly));
        part.write(QByteArray(123456, 'x'));
        part.close();
        // nothing listens on this port: the connection is refused, without any answer
        QTcpServer closed;
        QVERIFY(closed.listen(QHostAddress::LocalHost));
        const auto port = closed.serverPort();
        closed.close();
        DownloadManager manager({}, &m_network);
        manager.setRetryDelays({});
        DownloadItem download;
        download.url = QUrl(QString("http://127.0.0.1:%1/mod.7z").arg(port));
        download.target = target;
        download.size = 1000000;
        const auto id = manager.add(download);
        QTRY_COMPARE_WITH_TIMEOUT(manager.item(id)->state, DownloadItem::State::Failed, 30000);
        QCOMPARE(QFileInfo(target + ".part").size(), qint64(123456));
        QCOMPARE(manager.item(id)->received, qint64(123456));
    }

    void test_cancel_deletes_the_partial_file()
    {
        FileServer server;
        server.rules["/c"] = FileServer::Rule{ .chunk = 8192, .interval = 5 };
        QTemporaryDir dir;
        DownloadManager manager({}, &m_network);
        const auto target = dir.filePath("mod.7z");
        const auto id = manager.add(item(server, "/c", target));
        QTRY_VERIFY_WITH_TIMEOUT(manager.item(id)->received > 50000, 30000);
        manager.cancel(id);
        QVERIFY(!manager.item(id));
        QVERIFY(!QFile::exists(target + ".part"));
        QVERIFY(!QFile::exists(target));
    }

    void test_new_link_for_a_failed_download_continues_it()
    {
        FileServer server;
        server.rules["/old"].refuseTimes = 100;
        server.rules["/x"] = FileServer::Rule{ .dropAfter = 300000, .dropTimes = 1 };
        QTemporaryDir dir;
        DownloadManager manager({}, &m_network);
        manager.setRetryDelays({});
        const auto target = dir.filePath("mod.7z");
        const auto id = manager.add(item(server, "/x", target));
        QTRY_COMPARE_WITH_TIMEOUT(manager.item(id)->state, DownloadItem::State::Failed, 30000);
        const auto kept = QFileInfo(target + ".part").size();
        QVERIFY(kept > 0 && kept <= 300000);
        // clicking Mod Manager Download again gives the same file with a new address
        QSignalSpy done(&manager, &DownloadManager::completed);
        QCOMPARE(manager.add(item(server, "/x", target)), id);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
        QCOMPARE(readAll(target), server.data);
        QCOMPARE(server.ranges["/x"].last(), kept);
    }
};

QTEST_GUILESS_MAIN(DownloadManagerTest)

#include "DownloadManager_test.moc"
