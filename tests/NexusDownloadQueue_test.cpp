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

#include <QAbstractButton>
#include <QApplication>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <FileSystem.h>
#include <archive/ArchiveWriter.h>
#include <game/Deployer.h>
#include <game/GameInstance.h>
#include <game/ModList.h>
#include <settings/INISettingsObject.h>
#include <ui/dialogs/ModInstallFlow.h>
#include <ui/dialogs/NexusDownloadQueue.h>

// no raw string literals in this file: moc skips the slots that follow one
namespace {

std::unique_ptr<INISettingsObject> globalSettings(const QString& dir)
{
    auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(dir, "global.cfg"));
    for (const auto& [key, value] : { std::pair<const char*, QVariant>{ "ShowGameTime", true }, { "RecordGameTime", false },
                                      { "PreLoadCommand", "" }, { "PreLaunchCommand", "" }, { "PostExitCommand", "" }, { "ShowConsole", false },
                                      { "AutoCloseConsole", false }, { "ShowConsoleOnError", false }, { "LogPrePostOutput", true },
                                      { "ConsoleMaxLines", 1000 }, { "ConsoleOverflowStop", true }, { "Env", "{}" },
                                      { "QuitAfterGameStop", false }, { "CloseAfterLaunch", false } }) {
        settings->registerSetting(key, value);
    }
    return settings;
}

QString makeArchive(const QString& path, const QString& plugin)
{
    if (!FS::ensureFilePathExists(path)) {
        return {};
    }
    MMCZip::ArchiveWriter writer(path);
    if (!writer.open() || !writer.addFile("Data/" + plugin, QByteArray("TES4")) || !writer.close()) {
        return {};
    }
    return path;
}

/// Message boxes the queue left open, closed again
QStringList takeMessages()
{
    QStringList texts;
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (auto* box = qobject_cast<QMessageBox*>(widget); box && box->isVisible()) {
            texts << box->text();
            box->close();
        }
    }
    return texts;
}

#if defined(BROWSER_EXE) && defined(Q_OS_WIN)
/// A stand-in for Nexus Mods: a download page whose "Slow download" button fills most of the page and starts the
/// file's download when clicked, like the real one after its countdown; and the files themselves
class MockNexus : public QObject {
   public:
    explicit MockNexus(QByteArray archive) : m_archive(std::move(archive))
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] { answer(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        m_server.listen(QHostAddress::LocalHost);
    }
    QUrl page(int file) const { return QUrl(QString("http://127.0.0.1:%1/mods/%2?tab=files&file_id=%2").arg(m_server.serverPort()).arg(file)); }

   private:
    void answer(QTcpSocket* socket)
    {
        const auto request = socket->readAll();
        const auto path = QString::fromLatin1(request.split(' ').value(1));
        QByteArray body;
        QByteArray headers;
        if (path.startsWith("/mods/")) {
            const auto file = path.section('/', 2, 2).section('?', 0, 0);
            body = QString("<!doctype html><html><body style='margin:0'>"
                           "<button id='slowDownloadButton' style='position:fixed;left:5%;top:5%;width:90%;height:90%'>Slow download</button>"
                           "<script>document.getElementById('slowDownloadButton').addEventListener('click', () => {"
                           " location.href = '/file/%1.zip'; });</script></body></html>")
                       .arg(file)
                       .toUtf8();
            headers = "Content-Type: text/html; charset=utf-8\r\n";
        } else if (path.startsWith("/file/")) {
            body = m_archive;
            headers = "Content-Type: application/zip\r\nContent-Disposition: attachment; filename=\"" + path.section('/', 2).toUtf8() + "\"\r\n";
        }
        socket->write((body.isEmpty() ? QByteArray("HTTP/1.1 404 Not Found\r\n") : QByteArray("HTTP/1.1 200 OK\r\n")) + headers +
                      "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QByteArray m_archive;
};

/// The download window's top-level window, once it is open
HWND browserWindow()
{
    HWND found = nullptr;
    EnumWindows(
        [](HWND window, LPARAM result) -> BOOL {
            wchar_t title[128] = {};
            GetWindowTextW(window, title, 128);
            if (IsWindowVisible(window) && std::wstring_view(title).starts_with(L"BetterNMM \u2014 Nexus Mods downloads")) {
                *reinterpret_cast<HWND*>(result) = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&found));
    return found;
}

/// Whether the window shows the page: the mock's button is light grey where the clicks go, a window that draws nothing
/// is dark there
bool pageDrawn(HWND window)
{
    RECT client;
    GetClientRect(window, &client);
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, client.right, client.bottom);
    HGDIOBJ previous = SelectObject(dc, bitmap);
    // PW_CLIENTONLY | PW_RENDERFULLCONTENT: what the window shows, GPU-drawn content included
    PrintWindow(window, dc, 0x1 | 0x2);
    const COLORREF pixel = GetPixel(dc, client.right / 2, client.bottom * 2 / 3);
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return GetRValue(pixel) + GetGValue(pixel) + GetBValue(pixel) > 3 * 150;
}

/// A click like the person's, in the middle of the page, where the mock's button is
void clickPage(HWND window)
{
    RECT client;
    GetClientRect(window, &client);
    const LPARAM point = MAKELPARAM(client.right / 2, client.bottom * 2 / 3);
    PostMessageW(window, WM_MOUSEMOVE, 0, point);
    PostMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, point);
    PostMessageW(window, WM_LBUTTONUP, 0, point);
}
#endif

}  // namespace

class NexusDownloadQueueTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<INISettingsObject> m_global;
    std::unique_ptr<GameInstance> m_instance;
    QNetworkAccessManager m_network;

    NexusDownloadQueue::Request request(const QString& title, const QUrl& page, int modId)
    {
        NexusDownloadQueue::Request request;
        request.instance = m_instance.get();
        request.page = page;
        request.title = title;
        request.downloadDir = FS::PathCombine(m_dir.path(), "downloads");
        request.fileName = QString("%1.zip").arg(title);
        request.metadata.name = title;
        request.metadata.nexusGame = "testgame";
        request.metadata.nexusModId = modId;
        request.metadata.nexusFileId = modId * 10;
        request.metadata.version = "1.0";
        return request;
    }

    /// Runs @p queue until it has handled everything
    static bool finish(NexusDownloadQueue& queue, const QList<NexusDownloadQueue::Request>& requests)
    {
        QSignalSpy finished(&queue, &NexusDownloadQueue::finished);
        queue.add(requests, nullptr);
        return !finished.isEmpty() || finished.wait(60000);
    }

   private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_global = globalSettings(m_dir.path());
        const auto steam = FS::PathCombine(m_dir.path(), "steam");
        QVERIFY(FS::ensureFolderPathExists(FS::PathCombine(steam, "Data")));
        const auto root = FS::PathCombine(m_dir.path(), "instance");
        auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(root, "instance.cfg"));
        settings->registerSetting("InstanceType", "Game");
        m_instance = std::make_unique<GameInstance>(m_global.get(), std::move(settings), root);
        GameDefinition game;
        game.domain = "testgame";
        game.name = "Test Game";
        game.executable = "Game.exe";
        game.dataPath = "Data";
        GameInstance::applyDefinition(m_instance->settings(), game);
        m_instance->settings()->set("InstanceKind", "Main");
        m_instance->settings()->set("SourceGameDir", steam);
        QVERIFY(m_instance->modList()->load().has_value());
    }

    void test_clicked_files_download_and_install()
    {
        const auto source = makeArchive(FS::PathCombine(m_dir.path(), "source/ModA.zip"), "ModA.esp");
        QVERIFY(!source.isEmpty());
        NexusDownloadQueue queue(FAKE_BROWSER_EXE, FS::PathCombine(m_dir.path(), "profile"), &m_network);
        QVERIFY(queue.isAvailable());
        QVERIFY(finish(queue, { request("Mod A", QUrl::fromLocalFile(source), 1), request("skip Mod B", QUrl::fromLocalFile(source), 2) }));

        // the link the window reported downloaded into the downloads folder under the file's name on Nexus Mods
        QVERIFY(QFileInfo::exists(FS::PathCombine(m_dir.path(), "downloads/Mod A.zip")));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(m_dir.path(), "downloads/skip Mod B.zip")));
        const auto& mods = m_instance->modList()->mods();
        QCOMPARE(mods.size(), 1);
        QCOMPARE(mods.first().name, QString("Mod A"));
        QCOMPARE(mods.first().nexusModId, 1);
        QCOMPARE(mods.first().nexusFileId, 10);
        QCOMPARE(mods.first().nexusGame, QString("testgame"));
        QVERIFY(QFileInfo::exists(FS::PathCombine(m_instance->modList()->modDir(mods.first().id), "Data/ModA.esp")));
        QVERIFY2(takeMessages().isEmpty(), "a skipped file is the person's choice, not a problem");
    }

    void test_deploys_never_overlap()
    {
        // enough files that the first deploy is still placing them when the second is asked for, the way the Mods
        // page asks while an install deploys
        const auto mod = m_instance->modList()->modDir("Many");
        for (int i = 0; i < 400; i++) {
            const auto path = FS::PathCombine(mod, QString("Data/textures/many/%1.dds").arg(i));
            QVERIFY(FS::ensureFilePathExists(path));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("dds");
        }
        m_instance->modList()->upsert(ModEntry{ .id = "Many", .name = "Many", .enabled = true, .stamp = 1 });
        // the other tests count the instance's mods, so it goes again whatever happens here
        auto cleanup = qScopeGuard([this] {
            if (const int row = m_instance->modList()->indexOf("Many"); row >= 0) {
                m_instance->modList()->remove({ row });
            }
            ModInstallFlow::deploy(m_instance.get(), nullptr);
        });

        QStringList problems;
        QTimer closer;
        connect(&closer, &QTimer::timeout, this, [&problems] { problems << takeMessages(); });
        closer.start(20);
        bool nested = false;
        QTimer::singleShot(0, this, [this, &nested] { nested = ModInstallFlow::deploy(m_instance.get(), nullptr); });
        QVERIFY(ModInstallFlow::deploy(m_instance.get(), nullptr));
        QTRY_VERIFY(nested);
        closer.stop();
        problems << takeMessages();
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(" | ")));
        int placed = 0;
        for (int i = 0; i < 400; i++) {
            placed += QFileInfo::exists(FS::PathCombine(m_instance->gameRoot(), QString("Data/textures/many/%1.dds").arg(i))) ? 1 : 0;
        }
        QCOMPARE(placed, 400);
        // overlapping deploys take each other's files for game files and set them aside
        auto manifest = DeploymentManifest::load(m_instance->deploymentManifestPath());
        QVERIFY(manifest.has_value());
        int recorded = 0;
        for (const auto& file : std::as_const(manifest->files)) {
            QVERIFY2(!file.displacedOriginal, qPrintable(file.path));
            recorded += file.mod == "Many" ? 1 : 0;
        }
        QCOMPARE(recorded, 400);
    }

    void test_reinstalling_a_file_asks_to_replace()
    {
        const auto archive = makeArchive(FS::PathCombine(m_dir.path(), "source/Local Mod.zip"), "Local.esp");
        QVERIFY(!archive.isEmpty());
        auto* mods = m_instance->modList();
        const auto before = mods->mods().size();
        auto cleanup = qScopeGuard([mods, this] {
            while (mods->indexOf("Local Mod") >= 0 || mods->indexOf("Local Mod (2)") >= 0) {
                const int row = mods->indexOf("Local Mod") >= 0 ? mods->indexOf("Local Mod") : mods->indexOf("Local Mod (2)");
                mods->remove({ row });
            }
            ModInstallFlow::deploy(m_instance.get(), nullptr);
        });
        QVERIFY(ModInstallFlow::installArchive(m_instance.get(), archive, ModEntry{}, nullptr));
        QCOMPARE(mods->mods().size(), before + 1);

        // the same file again: asked, and Yes replaces it where it is
        QString asked;
        auto answerWith = [&asked](QMessageBox::StandardButton button) {
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* box = qobject_cast<QMessageBox*>(widget); box && box->isVisible() && box->button(button)) {
                    asked = box->text();
                    box->button(button)->click();
                }
            }
        };
        QTimer yes;
        connect(&yes, &QTimer::timeout, this, [&answerWith] { answerWith(QMessageBox::Yes); });
        yes.start(20);
        QVERIFY(ModInstallFlow::installArchive(m_instance.get(), archive, ModEntry{}, nullptr));
        yes.stop();
        QVERIFY2(asked.contains("Local Mod is installed already"), qPrintable(asked));
        QCOMPARE(mods->mods().size(), before + 1);

        // No installs it next to the first one
        asked.clear();
        QTimer no;
        connect(&no, &QTimer::timeout, this, [&answerWith] { answerWith(QMessageBox::No); });
        no.start(20);
        QVERIFY(ModInstallFlow::installArchive(m_instance.get(), archive, ModEntry{}, nullptr));
        no.stop();
        QVERIFY(!asked.isEmpty());
        QCOMPARE(mods->mods().size(), before + 2);
    }

    void test_installing_while_the_game_runs_waits_for_it()
    {
        // a download finishing while the game runs: installed, nothing pops up over the game, deployed once it stops
        const auto archive = makeArchive(FS::PathCombine(m_dir.path(), "source/Played Mod.zip"), "Played.esp");
        QVERIFY(!archive.isEmpty());
        auto* mods = m_instance->modList();
        auto cleanup = qScopeGuard([mods, this] {
            m_instance->setRunning(false);
            if (const int row = mods->indexOf("Played Mod"); row >= 0) {
                mods->remove({ row });
            }
            ModInstallFlow::deploy(m_instance.get(), nullptr);
        });
        const auto deployed = FS::PathCombine(m_instance->dataDir(), "Played.esp");
        QStringList popped;
        QTimer closer;
        connect(&closer, &QTimer::timeout, this, [&popped] { popped << takeMessages(); });
        closer.start(20);

        m_instance->setRunning(true);
        QVERIFY(ModInstallFlow::installArchive(m_instance.get(), archive, ModEntry{}, nullptr));
        QVERIFY(mods->indexOf("Played Mod") >= 0);
        QVERIFY(mods->mods()[mods->indexOf("Played Mod")].enabled);
        QVERIFY(!QFileInfo::exists(deployed));
        // the Mods page deploys whenever the list changes; while the game runs that waits too
        QVERIFY(!ModInstallFlow::deploy(m_instance.get(), nullptr));
        QTest::qWait(100);
        QVERIFY2(popped.isEmpty(), qPrintable(popped.join(" | ")));
        QVERIFY(!QFileInfo::exists(deployed));

        m_instance->setRunning(false);
        QTRY_VERIFY(QFileInfo::exists(deployed));
        closer.stop();
        popped << takeMessages();
        QVERIFY2(popped.isEmpty(), qPrintable(popped.join(" | ")));
    }

    void test_problems_are_reported()
    {
        const auto missing = QUrl::fromLocalFile(FS::PathCombine(m_dir.path(), "source/missing.zip"));
        NexusDownloadQueue queue(FAKE_BROWSER_EXE, FS::PathCombine(m_dir.path(), "profile"), &m_network);
        QVERIFY(finish(queue, { request("timeout Mod C", missing, 3), request("Mod D", missing, 4), request("ignore Mod E", missing, 5) }));
        const auto messages = takeMessages();
        QCOMPARE(messages.size(), 1);
        const auto& text = messages.first();
        QVERIFY2(text.contains("timeout Mod C: Nexus Mods did not start the download"), qPrintable(text));
        QVERIFY2(text.contains("Mod D: "), qPrintable(text));  // the download itself failed
        QVERIFY2(text.contains("ignore Mod E: BetterNMM's Nexus Mods window did not take this file"), qPrintable(text));
        QVERIFY(!QFileInfo::exists(FS::PathCombine(m_dir.path(), "downloads/Mod D.zip")));
        QCOMPARE(m_instance->modList()->mods().size(), 1);
    }

    void test_real_window()
    {
#if defined(BROWSER_EXE) && defined(Q_OS_WIN)
        QVERIFY2(QFileInfo::exists(BROWSER_EXE), BROWSER_EXE);
        const auto source = makeArchive(FS::PathCombine(m_dir.path(), "source/Window.zip"), "Window.esp");
        QFile archive(source);
        QVERIFY(archive.open(QIODevice::ReadOnly));
        MockNexus nexus(archive.readAll());
        // off screen and without taking the focus from whoever is using the computer during the test
        qputenv("BETTERNMM_BROWSER_OFFSCREEN", "1");
        NexusDownloadQueue queue(BROWSER_EXE, FS::PathCombine(m_dir.path(), "browser-profile"), &m_network);
        QSignalSpy finished(&queue, &NexusDownloadQueue::finished);
        auto first = request("Window Mod 1", nexus.page(101), 101);
        auto second = request("Window Mod 2", nexus.page(102), 102);
        queue.add({ first, second }, nullptr);

        HWND window = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((window = browserWindow()) != nullptr, 30000);
        // the page has to be on screen, not just loaded: the GPU set up wrongly once left the window blank
        QTRY_VERIFY_WITH_TIMEOUT(pageDrawn(window), 15000);
        // the first page loads while the second loads behind it; a person reads, then clicks twice in quick succession
        QTest::qWait(3000);
        clickPage(window);
        QTest::qWait(700);
        clickPage(window);
        QVERIFY(finished.wait(60000));

        QStringList names;
        for (const auto& mod : m_instance->modList()->mods()) {
            names << mod.name;
        }
        QVERIFY2(names.contains("Window Mod 1") && names.contains("Window Mod 2"), qPrintable(names.join(", ")));
        QVERIFY2(takeMessages().isEmpty(), "nothing went wrong");
        QTRY_VERIFY_WITH_TIMEOUT(browserWindow() == nullptr, 10000);  // the window closed by itself
#else
        QSKIP("The download window is built on Windows only");
#endif
    }

    void test_missing_window()
    {
        NexusDownloadQueue queue(FS::PathCombine(m_dir.path(), "no-browser/betternmm-browser.exe"), FS::PathCombine(m_dir.path(), "profile"), &m_network);
        QVERIFY(!queue.isAvailable());
        QVERIFY(finish(queue, { request("Mod F", QUrl("https://www.nexusmods.com/testgame/mods/6"), 6) }));
        const auto messages = takeMessages();
        QCOMPARE(messages.size(), 1);
        QVERIFY2(messages.first().contains("Nexus Mods window is missing"), qPrintable(messages.first()));
    }
};

QTEST_MAIN(NexusDownloadQueueTest)

#include "NexusDownloadQueue_test.moc"
