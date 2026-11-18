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

#include <QTest>

#include <ui/widgets/IniEditor.h>

// no raw string literals in this file: moc skips the slots that follow one
class IniEditorTest : public QObject {
    Q_OBJECT

   private slots:
    void test_parse()
    {
        QCOMPARE(Ini::parse("").kind, Ini::Line::Kind::Blank);
        QCOMPARE(Ini::parse("  ; a comment").kind, Ini::Line::Kind::Comment);
        QCOMPARE(Ini::parse("# also a comment").kind, Ini::Line::Kind::Comment);
        const auto section = Ini::parse("[Display] ");
        QCOMPARE(section.kind, Ini::Line::Kind::Section);
        QCOMPARE(section.section, QString("Display"));
        const auto setting = Ini::parse("  fDefaultFOV = 85.0000 ");
        QCOMPARE(setting.kind, Ini::Line::Kind::Setting);
        QCOMPARE(setting.key, QString("fDefaultFOV"));
        QCOMPARE(setting.value, QString("85.0000"));
        // a key with spaces, as Skyrim writes them, and an empty value
        QCOMPARE(Ini::parse("iSize W=1920").key, QString("iSize W"));
        QCOMPARE(Ini::parse("sResourceArchiveList2=").value, QString());
        QCOMPARE(Ini::parse("just text").kind, Ini::Line::Kind::Other);
        QCOMPARE(Ini::parse("=value").kind, Ini::Line::Kind::Other);
    }

    void test_with_value_keeps_layout()
    {
        QCOMPARE(Ini::withValue("fDefaultFOV=85.0000", "100"), QString("fDefaultFOV=100"));
        QCOMPARE(Ini::withValue("  bShowCompass = 1", "0"), QString("  bShowCompass = 0"));
        QCOMPARE(Ini::withValue("sLanguage=\tENGLISH", "GERMAN"), QString("sLanguage=\tGERMAN"));
    }

    void test_check_value()
    {
        QVERIFY(Ini::checkValue("bShowCompass", "1").isEmpty());
        QVERIFY(!Ini::checkValue("bShowCompass", "yes").isEmpty());
        QVERIFY(Ini::checkValue("iSize W", "-1920").isEmpty());
        QVERIFY(!Ini::checkValue("iSize W", "19.5").isEmpty());
        QVERIFY(!Ini::checkValue("uGridsToLoad", "-5").isEmpty());
        QVERIFY(Ini::checkValue("uGridsToLoad", "5").isEmpty());
        QVERIFY(Ini::checkValue("fDefaultFOV", "85.5").isEmpty());
        QVERIFY(!Ini::checkValue("fDefaultFOV", "85,5").isEmpty());
        QVERIFY(Ini::checkValue("sLanguage", "anything at all").isEmpty());
        // names without a type prefix are taken as they are
        QVERIFY(Ini::checkValue("EnableVSync", "whatever").isEmpty());
        QVERIFY(Ini::typeDescription("EnableVSync").isEmpty());
        QVERIFY(!Ini::typeDescription("fDefaultFOV").isEmpty());
    }

    void test_round_trip_keeps_bytes()
    {
        // Windows line ends, no byte order mark: what Skyrim writes
        const QByteArray crlf = "[Display]\r\nfDefaultFOV=85\r\n";
        auto text = Ini::decode(crlf);
        QCOMPARE(text.text, QString("[Display]\nfDefaultFOV=85\n"));
        QVERIFY(text.crlf);
        QVERIFY(!text.bom);
        QCOMPARE(Ini::encode(text), crlf);

        // a UTF-8 byte order mark stays, and so do Unix line ends
        const QByteArray bom = "\xEF\xBB\xBF[General]\nsName=Caf\xC3\xA9\n";
        text = Ini::decode(bom);
        QVERIFY(text.bom);
        QVERIFY(!text.crlf);
        QCOMPARE(text.text, QString::fromUtf8("[General]\nsName=Caf\xC3\xA9\n"));
        QCOMPARE(Ini::encode(text), bom);

        // older games write the system code page; bytes that are no UTF-8 come back unchanged
        const QByteArray ansi = "[General]\r\nsName=Caf\xE9\r\n";
        text = Ini::decode(ansi);
        QCOMPARE(text.encoding, QStringConverter::System);
        QCOMPARE(Ini::encode(text), ansi);

        // a new file gets Windows line ends
        text = Ini::decode(QByteArray());
        text.text = "[Display]\nfDefaultFOV=90\n";
        QCOMPARE(Ini::encode(text), QByteArray("[Display]\r\nfDefaultFOV=90\r\n"));
    }
};

QTEST_GUILESS_MAIN(IniEditorTest)

#include "IniEditor_test.moc"
