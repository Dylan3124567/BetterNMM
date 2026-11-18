#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include <tasks/Task.h>

#include <FileSystem.h>
#include <StringUtils.h>

#include <filesystem>
namespace fs = std::filesystem;

class FileSystemTest : public QObject {
    Q_OBJECT

    const QString bothSlash = "/foo/";
    const QString trailingSlash = "foo/";
    const QString leadingSlash = "/foo";

   private slots:
    void test_pathCombine()
    {
        QCOMPARE(QString("/foo/foo"), FS::PathCombine(bothSlash, bothSlash));
        QCOMPARE(QString("foo/foo"), FS::PathCombine(trailingSlash, trailingSlash));
        QCOMPARE(QString("/foo/foo"), FS::PathCombine(leadingSlash, leadingSlash));

        QCOMPARE(QString("/foo/foo/foo"), FS::PathCombine(bothSlash, bothSlash, bothSlash));
        QCOMPARE(QString("foo/foo/foo"), FS::PathCombine(trailingSlash, trailingSlash, trailingSlash));
        QCOMPARE(QString("/foo/foo/foo"), FS::PathCombine(leadingSlash, leadingSlash, leadingSlash));
    }

    void test_PathCombine1_data()
    {
        QTest::addColumn<QString>("result");
        QTest::addColumn<QString>("path1");
        QTest::addColumn<QString>("path2");

        QTest::newRow("qt 1") << "/abc/def/ghi/jkl" << "/abc/def" << "ghi/jkl";
        QTest::newRow("qt 2") << "/abc/def/ghi/jkl" << "/abc/def/" << "ghi/jkl";
#if defined(Q_OS_WIN)
        QTest::newRow("win native, from C:") << "C:/abc" << "C:" << "abc";
        QTest::newRow("win native 1") << "C:/abc/def/ghi/jkl" << "C:\\abc\\def" << "ghi\\jkl";
        QTest::newRow("win native 2") << "C:/abc/def/ghi/jkl" << "C:\\abc\\def\\" << "ghi\\jkl";
#endif
    }

    void test_PathCombine1()
    {
        QFETCH(QString, result);
        QFETCH(QString, path1);
        QFETCH(QString, path2);

        QCOMPARE(FS::PathCombine(path1, path2), result);
    }

    void test_PathCombine2_data()
    {
        QTest::addColumn<QString>("result");
        QTest::addColumn<QString>("path1");
        QTest::addColumn<QString>("path2");
        QTest::addColumn<QString>("path3");

        QTest::newRow("qt 1") << "/abc/def/ghi/jkl" << "/abc" << "def" << "ghi/jkl";
        QTest::newRow("qt 2") << "/abc/def/ghi/jkl" << "/abc/" << "def" << "ghi/jkl";
        QTest::newRow("qt 3") << "/abc/def/ghi/jkl" << "/abc" << "def/" << "ghi/jkl";
        QTest::newRow("qt 4") << "/abc/def/ghi/jkl" << "/abc/" << "def/" << "ghi/jkl";
#if defined(Q_OS_WIN)
        QTest::newRow("win 1") << "C:/abc/def/ghi/jkl" << "C:\\abc" << "def" << "ghi\\jkl";
        QTest::newRow("win 2") << "C:/abc/def/ghi/jkl" << "C:\\abc\\" << "def" << "ghi\\jkl";
        QTest::newRow("win 3") << "C:/abc/def/ghi/jkl" << "C:\\abc" << "def\\" << "ghi\\jkl";
        QTest::newRow("win 4") << "C:/abc/def/ghi/jkl" << "C:\\abc\\" << "def" << "ghi\\jkl";
#endif
    }

    void test_PathCombine2()
    {
        QFETCH(QString, result);
        QFETCH(QString, path1);
        QFETCH(QString, path2);
        QFETCH(QString, path3);

        QCOMPARE(FS::PathCombine(path1, path2, path3), result);
    }

    void test_copy()
    {
        QString folder = QFINDTESTDATA("testdata/FileSystem/test_folder");
        auto f = [&folder]() {
            QTemporaryDir tempDir;
            tempDir.setAutoRemove(true);
            qDebug() << "From:" << folder << "To:" << tempDir.path();

            QDir target_dir(FS::PathCombine(tempDir.path(), "test_folder"));
            qDebug() << tempDir.path();
            qDebug() << target_dir.path();
            FS::copy c(folder, target_dir.path());
            c();

            for (auto entry : target_dir.entryList()) {
                qDebug() << entry;
            }
            QVERIFY(target_dir.entryList().contains("modinfo.json"));
            QVERIFY(target_dir.entryList().contains("Data"));
        };

        // first try variant without trailing /
        QVERIFY(!folder.endsWith('/'));
        f();

        // then variant with trailing /
        folder.append('/');
        QVERIFY(folder.endsWith('/'));
        f();
    }

    void test_copy_with_blacklist()
    {
        QString folder = QFINDTESTDATA("testdata/FileSystem/test_folder");
        auto f = [&folder]() {
            QTemporaryDir tempDir;
            tempDir.setAutoRemove(true);
            qDebug() << "From:" << folder << "To:" << tempDir.path();

            QDir target_dir(FS::PathCombine(tempDir.path(), "test_folder"));
            qDebug() << tempDir.path();
            qDebug() << target_dir.path();
            FS::copy c(folder, target_dir.path());
            auto re = Filters::regexp(QRegularExpression("[.]?json"));
            c.matcher(re);
            c();

            for (auto entry : target_dir.entryList()) {
                qDebug() << entry;
            }
            QVERIFY(!target_dir.entryList().contains("modinfo.json"));
            QVERIFY(target_dir.entryList().contains("Data"));
        };

        // first try variant without trailing /
        QVERIFY(!folder.endsWith('/'));
        f();

        // then variant with trailing /
        folder.append('/');
        QVERIFY(folder.endsWith('/'));
        f();
    }

    void test_copy_with_whitelist()
    {
        QString folder = QFINDTESTDATA("testdata/FileSystem/test_folder");
        auto f = [&folder]() {
            QTemporaryDir tempDir;
            tempDir.setAutoRemove(true);
            qDebug() << "From:" << folder << "To:" << tempDir.path();

            QDir target_dir(FS::PathCombine(tempDir.path(), "test_folder"));
            qDebug() << tempDir.path();
            qDebug() << target_dir.path();
            FS::copy c(folder, target_dir.path());
            auto re = Filters::regexp(QRegularExpression("[.]?json"));
            c.matcher(re);
            c.whitelist(true);
            c();

            for (auto entry : target_dir.entryList()) {
                qDebug() << entry;
            }
            QVERIFY(target_dir.entryList().contains("modinfo.json"));
            QVERIFY(!target_dir.entryList().contains("Data"));
        };

        // first try variant without trailing /
        QVERIFY(!folder.endsWith('/'));
        f();

        // then variant with trailing /
        folder.append('/');
        QVERIFY(folder.endsWith('/'));
        f();
    }

    void test_copy_with_dot_hidden()
    {
        QString folder = QFINDTESTDATA("testdata/FileSystem/test_folder");
        auto f = [&folder]() {
            QTemporaryDir tempDir;
            tempDir.setAutoRemove(true);
            qDebug() << "From:" << folder << "To:" << tempDir.path();

            QDir target_dir(FS::PathCombine(tempDir.path(), "test_folder"));
            qDebug() << tempDir.path();
            qDebug() << target_dir.path();
            FS::copy c(folder, target_dir.path());
            c();

            auto filter = QDir::Filter::Files | QDir::Filter::Dirs | QDir::Filter::Hidden;

            for (auto entry : target_dir.entryList(filter)) {
                qDebug() << entry;
            }

            QVERIFY(target_dir.entryList(filter).contains(".secret_folder"));
            target_dir.cd(".secret_folder");
            QVERIFY(target_dir.entryList(filter).contains(".secret_file.txt"));
        };

        // first try variant without trailing /
        QVERIFY(!folder.endsWith('/'));
        f();

        // then variant with trailing /
        folder.append('/');
        QVERIFY(folder.endsWith('/'));
        f();
    }

    void test_copy_single_file()
    {
        QTemporaryDir tempDir;
        tempDir.setAutoRemove(true);

        {
            QString file = QFINDTESTDATA("testdata/FileSystem/test_folder/modinfo.json");

            qDebug() << "From:" << file << "To:" << tempDir.path();

            QDir target_dir(FS::PathCombine(tempDir.path(), "modinfo.json"));
            qDebug() << tempDir.path();
            qDebug() << target_dir.path();
            FS::copy c(file, target_dir.filePath("modinfo.json"));
            c();

            auto filter = QDir::Filter::Files;

            for (auto entry : target_dir.entryList(filter)) {
                qDebug() << entry;
            }

            QVERIFY(target_dir.entryList(filter).contains("modinfo.json"));
        }
    }

    void test_getDesktop() { QCOMPARE(FS::getDesktopDir(), QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)); }

    void test_path_depth()
    {
        QCOMPARE(FS::pathDepth(""), 0);
        QCOMPARE(FS::pathDepth("."), 0);
        QCOMPARE(FS::pathDepth("foo.txt"), 0);
        QCOMPARE(FS::pathDepth("./foo.txt"), 0);
        QCOMPARE(FS::pathDepth("./bar/foo.txt"), 1);
        QCOMPARE(FS::pathDepth("../bar/foo.txt"), 0);
        QCOMPARE(FS::pathDepth("/bar/foo.txt"), 1);
        QCOMPARE(FS::pathDepth("baz/bar/foo.txt"), 2);
        QCOMPARE(FS::pathDepth("/baz/bar/foo.txt"), 2);
        QCOMPARE(FS::pathDepth("./baz/bar/foo.txt"), 2);
        QCOMPARE(FS::pathDepth("/baz/../bar/foo.txt"), 1);
    }

    void test_path_trunc()
    {
        QCOMPARE(FS::pathTruncate("", 0), QDir::toNativeSeparators(""));
        QCOMPARE(FS::pathTruncate("foo.txt", 0), QDir::toNativeSeparators(""));
        QCOMPARE(FS::pathTruncate("foo.txt", 1), QDir::toNativeSeparators(""));
        QCOMPARE(FS::pathTruncate("./bar/foo.txt", 0), QDir::toNativeSeparators("./bar"));
        QCOMPARE(FS::pathTruncate("./bar/foo.txt", 1), QDir::toNativeSeparators("./bar"));
        QCOMPARE(FS::pathTruncate("/bar/foo.txt", 1), QDir::toNativeSeparators("/bar"));
        QCOMPARE(FS::pathTruncate("bar/foo.txt", 1), QDir::toNativeSeparators("bar"));
        QCOMPARE(FS::pathTruncate("baz/bar/foo.txt", 2), QDir::toNativeSeparators("baz/bar"));
#if defined(Q_OS_WIN)
        QCOMPARE(FS::pathTruncate("C:\\bar\\foo.txt", 1), QDir::toNativeSeparators("C:\\bar"));
#endif
    }
};

QTEST_GUILESS_MAIN(FileSystemTest)

#include "FileSystem_test.moc"
