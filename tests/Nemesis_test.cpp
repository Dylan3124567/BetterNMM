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

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <filesystem>

#include <FileSystem.h>
#include <StringUtils.h>
#include <game/Nemesis.h>

namespace {

void writeFile(const QString& path, const QByteArray& content)
{
    QVERIFY(FS::ensureFilePathExists(path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void addPatch(const QString& engine, const QString& code, const QByteArray& name, bool hidden = false)
{
    writeFile(FS::PathCombine(engine, "mod", code, "info.ini"),
              "name=" + name + "\r\nauthor=Someone\r\nsite=https://example.com/" + code.toUtf8() + "\r\nauto=\r\n" + (hidden ? "hidden=true\r\n" : ""));
}

}  // namespace

class NemesisTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_data;
    QString m_engine;
    QString m_staged;  ///< the Nemesis mod's own nemesis.ini, which deployment linked into the Data folder

   private slots:
    void init()
    {
        QDir(m_dir.path()).removeRecursively();
        m_data = FS::PathCombine(m_dir.path(), "game/Data");
        m_engine = FS::PathCombine(m_data, "Nemesis_Engine");
        writeFile(FS::PathCombine(m_engine, "Nemesis Unlimited Behavior Engine.exe"), "program");
        m_staged = FS::PathCombine(m_dir.path(), "mods/Nemesis/Data/Nemesis_Engine/nemesis.ini");
        writeFile(m_staged, "SkyrimDataDirectory=auto\r\nMaxAnimation=20000\r\nfirst=false\r\n");
        std::error_code error;
        std::filesystem::create_hard_link(StringUtils::toStdString(m_staged), StringUtils::toStdString(FS::PathCombine(m_engine, "nemesis.ini")), error);
        QVERIFY2(!error, error.message().c_str());
        addPatch(m_engine, "gender", "Gender Specific Animations");  // comes with Nemesis
        addPatch(m_engine, "tdmv", "True Directional Movement");     // came with another mod
        addPatch(m_engine, "base", "Hidden Base", true);
    }

    void test_points_nemesis_at_this_data_folder()
    {
        const auto setup = Nemesis::prepare(m_data, { "gender", "base" });
        QVERIFY2(setup.has_value(), qPrintable(setup ? QString() : setup.error()));
        const auto expected = "SkyrimDataDirectory=" + QDir::toNativeSeparators(QDir(m_data).absolutePath()).toUtf8() + "\\\r\nMaxAnimation=20000\r\nfirst=false\r\n";
        QCOMPARE(readFile(FS::PathCombine(m_engine, "nemesis.ini")), expected);
        // written in place: the deployed file is still the mod's own
        QCOMPARE(readFile(m_staged), expected);
        QVERIFY(QFileInfo(FS::PathCombine(m_engine, "cache")).isDir());
        QCOMPARE(readFile(FS::PathCombine(m_engine, "cache/language setting")), QByteArray("english\n"));
    }

    void test_ticks_patches_other_mods_brought_once()
    {
        auto setup = Nemesis::prepare(m_data, { "gender", "base" });
        QVERIFY(setup.has_value());
        QCOMPARE(setup->ticked, QStringList{ "True Directional Movement" });
        const auto settings = FS::PathCombine(m_engine, "cache/mod settings");
        QCOMPARE(readFile(settings), QByteArray("True Directional Movement (https://example.com/tdmv)\n"));

        // unticked in Nemesis: it stays that way
        writeFile(settings, "Gender Specific Animations (https://example.com/gender)\n");
        setup = Nemesis::prepare(m_data, { "gender", "base" });
        QVERIFY(setup->ticked.isEmpty());
        QCOMPARE(readFile(settings), QByteArray("Gender Specific Animations (https://example.com/gender)\n"));

        // a mod installed later brings another patch: that one is ticked, next to what was picked
        addPatch(m_engine, "colis", "Precision");
        setup = Nemesis::prepare(m_data, { "gender", "base" });
        QCOMPARE(setup->ticked, QStringList{ "Precision" });
        QCOMPARE(readFile(settings),
                 QByteArray("Gender Specific Animations (https://example.com/gender)\nPrecision (https://example.com/colis)\n"));
    }

    void test_engine_update_follows_the_patches()
    {
        QVERIFY(Nemesis::prepare(m_data, {})->engineOutdated);
        writeFile(FS::PathCombine(m_engine, "cache/engine_update"), "v0.84\n");
        QVERIFY(Nemesis::prepare(m_data, {})->engineOutdated);  // updated by someone, but not for what BetterNMM knows
        QVERIFY(Nemesis::engineUpdated(m_data).has_value());
        QVERIFY(!Nemesis::prepare(m_data, {})->engineOutdated);
        addPatch(m_engine, "tkuc", "TK Dodge");
        QVERIFY(Nemesis::prepare(m_data, {})->engineOutdated);
        QVERIFY(Nemesis::engineUpdated(m_data).has_value());
        QVERIFY(!Nemesis::prepare(m_data, {})->engineOutdated);
        QVERIFY(QDir(FS::PathCombine(m_engine, "mod/tkuc")).removeRecursively());
        QVERIFY(Nemesis::prepare(m_data, {})->engineOutdated);

        // the instance moved: the engine's records name the old place
        QVERIFY(Nemesis::engineUpdated(m_data).has_value());
        const auto moved = FS::PathCombine(m_dir.path(), "moved/Data");
        QVERIFY(QDir().mkpath(FS::PathCombine(m_dir.path(), "moved")));
        QVERIFY(QDir().rename(m_data, moved));
        QVERIFY(Nemesis::prepare(moved, {})->engineOutdated);
        QVERIFY(QDir().rename(moved, m_data));
    }

    void test_missing_mod_settings_is_created_empty()
    {
        const auto setup = Nemesis::prepare(m_data, { "gender", "base", "tdmv" });
        QVERIFY(setup.has_value());
        QVERIFY(setup->ticked.isEmpty());
        QVERIFY(QFileInfo::exists(FS::PathCombine(m_engine, "cache/mod settings")));
    }
};

QTEST_GUILESS_MAIN(NemesisTest)

#include "Nemesis_test.moc"
