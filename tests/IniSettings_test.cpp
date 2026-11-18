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

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <FileSystem.h>
#include <game/Deployer.h>
#include <game/GameDefinition.h>
#include <game/GameFiles.h>
#include <game/GameInstance.h>
#include <game/Ini.h>
#include <game/IniSettings.h>
#include <settings/INISettingsObject.h>
#include <ui/widgets/IniSettingsView.h>

// no raw string literals in this file: moc skips the slots that follow one
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
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<missing>");
}

/// JSON written with single quotes, to keep it readable here
QByteArray json(const char* text)
{
    return QByteArray(text).replace('\'', '"');
}

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

const char* g_catalog =
    "{ 'games': { 'testgame': {"
    "  'files': [ { 'id': 'base', 'path': 'Test.ini', 'overriddenBy': 'custom' }, { 'id': 'custom', 'path': 'TestCustom.ini' },"
    "             { 'id': 'enb', 'path': 'enblocal.ini', 'folder': 'game', 'missing': 'Install ENB first.' } ],"
    "  'presets': { 'file': 'base', 'names': [ 'Low.ini', 'Ultra.ini' ] },"
    "  'categories': [ { 'name': 'Display', 'settings': ["
    "    { 'label': 'Shadow distance', 'type': 'decimal', 'key': [ 'base', 'Display', 'fShadowDistance', '8000.0000' ], 'min': 0, 'max': 20000 },"
    "    { 'label': 'Window mode', 'type': 'choice', 'keys': [ [ 'base', 'Display', 'bFull Screen', '0' ], [ 'base', 'Display', 'bBorderless', '0' ] ],"
    "      'options': [ [ 'Fullscreen', '1', '0' ], [ 'Borderless window', '0', '1' ], [ 'Window', '0', '0' ] ] },"
    "    { 'label': 'ENB: video memory', 'type': 'integer', 'key': [ 'enb', 'MEMORY', 'VideoMemorySizeMb', '0' ], 'min': 0, 'max': 100000 } ] } ] } } }";

/// Colours in every format, a text, and a toggle that switches three keys
const char* g_kinds =
    "{ 'games': { 'testgame': {"
    "  'files': [ { 'id': 'base', 'path': 'Test.ini' } ],"
    "  'categories': [ { 'name': 'Kinds', 'settings': ["
    "    { 'label': 'Packed', 'type': 'color', 'format': 'packed', 'key': [ 'base', 'Interface', 'iSubtitleSpeakerNameColor', '8947848' ] },"
    "    { 'label': 'ABGR', 'type': 'color', 'format': 'abgr', 'key': [ 'base', 'Menu', 'iConsoleSelectedRefColor', '0' ] },"
    "    { 'label': 'Text colour', 'type': 'color', 'format': 'text', 'key': [ 'base', 'Menu', 'rConsoleTextColor', '255,255,255' ] },"
    "    { 'label': 'Bytes', 'type': 'color', 'format': 'bytes', 'keys': [ [ 'base', 'Interface', 'uSubtitleR', '187' ],"
    "      [ 'base', 'Interface', 'uSubtitleG', '187' ], [ 'base', 'Interface', 'uSubtitleB', '187' ] ] },"
    "    { 'label': 'Units', 'type': 'color', 'format': 'units', 'keys': [ [ 'base', 'Pipboy', 'fPipboyEffectColorR', '0.08' ],"
    "      [ 'base', 'Pipboy', 'fPipboyEffectColorG', '1.0' ], [ 'base', 'Pipboy', 'fPipboyEffectColorB', '0.09' ] ] },"
    "    { 'label': 'Screenshots', 'type': 'text', 'key': [ 'base', 'Display', 'sScreenShotBaseName', 'ScreenShot' ] },"
    "    { 'label': 'Free map camera', 'type': 'toggle', 'on': [ '90', '0', '400' ], 'off': [ '75', '15', '80' ],"
    "      'keys': [ [ 'base', 'MapMenu', 'fMapWorldMaxPitch', '75.0' ], [ 'base', 'MapMenu', 'fMapWorldMinPitch', '15.0' ],"
    "                [ 'base', 'MapMenu', 'fMapWorldYawRange', '80.0' ] ] },"
    "    { 'label': 'Intro video', 'type': 'toggle', 'on': 'BGS_Logo.bik', 'off': '', 'key': [ 'base', 'General', 'sIntroSequence', 'BGS_Logo.bik' ] } ] } ] } } }";

}  // namespace

class IniSettingsTest : public QObject {
    Q_OBJECT

   private slots:
    void test_document()
    {
        auto document = Ini::Document::fromBytes("\xEF\xBB\xBF[Display]\r\nfShadowDistance = 8000.0000\r\n; graphics end here\r\n\r\n"
                                                 "[General]\r\nsLanguage=ENGLISH\r\n");
        QCOMPARE(document.value("display", "FSHADOWDISTANCE"), std::optional<QString>("8000.0000"));
        QVERIFY(!document.value("Display", "iSize W"));
        QVERIFY(!document.value("Papyrus", "bEnableLogging"));
        document.setValue("Display", "fShadowDistance", "8000.0000");
        QVERIFY(!document.isModified());

        document.setValue("Display", "fShadowDistance", "5000.0000");
        document.setValue("display", "iSize W", "2560");
        document.setValue("Papyrus", "bEnableLogging", "1");
        QVERIFY(document.isModified());
        // the mark, the line ends, the spacing and the comment stay; new keys go after their section's last setting
        QCOMPARE(document.toBytes(), QByteArray("\xEF\xBB\xBF[Display]\r\nfShadowDistance = 5000.0000\r\niSize W=2560\r\n; graphics end here\r\n\r\n"
                                                "[General]\r\nsLanguage=ENGLISH\r\n\r\n[Papyrus]\r\nbEnableLogging=1\r\n"));

        auto empty = Ini::Document::fromBytes({});
        empty.setValue("Memory", "DefaultHeapInitialAllocMB", "768");
        QCOMPARE(empty.toBytes(), QByteArray("[Memory]\r\nDefaultHeapInitialAllocMB=768\r\n"));

        auto unix = Ini::Document::fromBytes("[A]\nb=1\n");
        unix.setValue("A", "c", "2");
        QCOMPARE(unix.toBytes(), QByteArray("[A]\nb=1\nc=2\n"));
    }

    void test_values()
    {
        QVERIFY(IniSettings::sameValue("1", "1.0000"));
        QVERIFY(IniSettings::sameValue("TRUE", "true"));
        QVERIFY(IniSettings::sameValue("", ""));
        QVERIFY(!IniSettings::sameValue("1", "0"));
        QVERIFY(!IniSettings::sameValue("STRINGS\\", ""));

        QString error;
        const auto catalog = IniSettings::parse(json(g_catalog), &error).value("testgame");
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto& window = catalog.categories[0].settings[1];
        QCOMPARE(IniSettings::matchingOption(window, { "1", "0" }), 0);
        QCOMPARE(IniSettings::matchingOption(window, { "0.0000", "1" }), 1);
        QCOMPARE(IniSettings::matchingOption(window, { "1", "1" }), -1);
    }

    void test_catalog_errors()
    {
        const char* broken[] = {
            "{ 'games': { 'g': { 'files': [], 'categories': [ { 'name': 'A', 'settings': [ { 'label': 'X', 'type': 'toggle', 'key': [ 'nofile', 'S', 'bX', '0' ] } ] } ] } } }",
            "{ 'games': { 'g': { 'files': [ { 'id': 'f', 'path': 'F.ini' } ], 'categories': [ { 'name': 'A', 'settings': [ { 'label': 'X', 'type': 'choice', "
            "'key': [ 'f', 'S', 'iX', '0' ], 'options': [ [ 'One', '1', '2' ], [ 'Two', '2' ] ] } ] } ] } } }",
            "{ 'games': { 'g': { 'files': [ { 'id': 'f', 'path': 'F.ini' } ], 'categories': [ { 'include': 'other/Nothing' } ] } } }",
            "{ 'games': { 'g': { 'files': [ { 'id': 'f', 'path': 'F.ini' } ], 'categories': [ { 'name': 'A', 'settings': [ { 'label': 'X', 'type': 'slider', "
            "'key': [ 'f', 'S', 'iX', '0' ] } ] } ] } } }",
            "{ 'games': { 'g': { 'files': [ { 'id': 'f', 'path': 'F.ini' } ], 'categories': [ { 'name': 'A', 'settings': [ { 'label': 'X', 'type': 'integer', "
            "'key': [ 'f', 'S', 'iX', '0' ] } ] } ] } } }",
            // a toggle needs as many "on" and "off" values as keys
            "{ 'games': { 'g': { 'files': [ { 'id': 'f', 'path': 'F.ini' } ], 'categories': [ { 'name': 'A', 'settings': [ { 'label': 'X', 'type': 'toggle', "
            "'on': [ '1', '1' ], 'off': [ '0' ], 'keys': [ [ 'f', 'S', 'bX', '0' ], [ 'f', 'S', 'bY', '0' ] ] } ] } ] } } }",
            "{ 'games': { 'g': { 'files': [ { 'id': 'f', 'path': 'F.ini' } ], 'categories': [ { 'name': 'A', 'settings': [ { 'label': 'X', 'type': 'toggle', "
            "'keys': [ [ 'f', 'S', 'bX', '0' ], [ 'f', 'S', 'bY', '0' ] ] } ] } ] } } }",
            // three keys for red, green and blue; a known format
            "{ 'games': { 'g': { 'files': [ { 'id': 'f', 'path': 'F.ini' } ], 'categories': [ { 'name': 'A', 'settings': [ { 'label': 'X', 'type': 'color', "
            "'format': 'bytes', 'key': [ 'f', 'S', 'uR', '0' ] } ] } ] } } }",
            "{ 'games': { 'g': { 'files': [ { 'id': 'f', 'path': 'F.ini' } ], 'categories': [ { 'name': 'A', 'settings': [ { 'label': 'X', 'type': 'color', "
            "'format': 'hsv', 'key': [ 'f', 'S', 'iX', '0' ] } ] } ] } } }",
        };
        for (const auto* text : broken) {
            QString error;
            QVERIFY2(IniSettings::parse(json(text), &error).isEmpty(), text);
            QVERIFY2(!error.isEmpty(), text);
        }
    }

    void test_builtin_catalog()
    {
        QFile file(":/games/settings.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QString error;
        const auto catalogs = IniSettings::parse(file.readAll(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(catalogs.keys(), QStringList({ "fallout4", "skyrim", "skyrimspecialedition" }));
        for (auto it = catalogs.cbegin(); it != catalogs.cend(); ++it) {
            const auto game = GameRegistry::byDomain(it.key());
            QVERIFY2(game.isValid(), qPrintable(it.key()));
            // settings in My Games only use the INI files the game definition lists there
            for (const auto& catalogFile : it->files) {
                QVERIFY2(catalogFile.inGameFolder || game.iniFiles.contains(catalogFile.path), qPrintable(catalogFile.path));
            }
            QStringList names;
            for (const auto& category : it->categories) {
                names << category.name;
            }
            QVERIFY2(names.contains("Display") && names.contains("Quality"), qPrintable(names.join(", ")));
        }
        const auto memorySettings = [&catalogs](const QString& domain) {
            QStringList keys;
            for (const auto& category : catalogs[domain].categories) {
                for (const auto& setting : category.settings) {
                    for (const auto& key : setting.keys) {
                        if (category.name == "Memory") {
                            keys << key.key;
                        }
                    }
                }
            }
            return keys;
        };
        for (const auto* key : { "uExterior Cell Buffer", "uInterior Cell Buffer", "iMaxAllocatedMemoryBytes", "VideoMemorySizeMb" }) {
            QVERIFY(memorySettings("skyrimspecialedition").contains(key));
            QVERIFY(memorySettings("skyrim").contains(key));
        }
        QVERIFY(memorySettings("skyrim").contains("DefaultHeapInitialAllocMB"));
        QVERIFY(memorySettings("skyrim").contains("ScrapHeapSizeMB"));

        // a game without INI files of its own shows its defaults as they are: no choice reads "Custom", no toggle sits
        // between on and off, no number lies outside its range, every colour is a colour
        int settingCount = 0;
        for (auto it = catalogs.cbegin(); it != catalogs.cend(); ++it) {
            for (const auto& category : it->categories) {
                for (const auto& setting : category.settings) {
                    settingCount++;
                    QStringList defaults;
                    for (const auto& key : setting.keys) {
                        defaults << key.defaultValue;
                    }
                    const auto where = QString("%1 / %2 / %3: %4").arg(it.key(), category.name, setting.label, defaults.join(", "));
                    switch (setting.type) {
                        case IniSettings::Setting::Type::Choice:
                            QVERIFY2(IniSettings::matchingOption(setting, defaults) >= 0, qPrintable(where));
                            break;
                        case IniSettings::Setting::Type::Toggle: {
                            auto off = setting;
                            off.on = setting.off;
                            QVERIFY2(IniSettings::isOn(setting, defaults) || IniSettings::isOn(off, defaults), qPrintable(where));
                            break;
                        }
                        case IniSettings::Setting::Type::Integer:
                        case IniSettings::Setting::Type::Decimal: {
                            bool number = false;
                            const auto value = defaults.first().toDouble(&number);
                            QVERIFY2(number && value >= setting.minimum && value <= setting.maximum, qPrintable(where));
                            break;
                        }
                        case IniSettings::Setting::Type::Color:
                            QVERIFY2(IniSettings::colorOf(setting, defaults).isValid(), qPrintable(where));
                            break;
                        default:
                            break;
                    }
                }
            }
        }
        // BethINI Pie's settings for Skyrim SE (191) and Fallout 4 (157), with Oldrim's own
        QVERIFY2(settingCount > 400, qPrintable(QString::number(settingCount)));

        // Oldrim keeps the settings it had; the Special Edition has each of them too, with the same keys
        const auto keysOf = [](const IniSettings::Setting& setting) {
            QStringList keys;
            for (const auto& key : setting.keys) {
                keys << key.file + '|' + key.section + '|' + key.key;
            }
            return keys;
        };
        QMap<QString, QStringList> specialEdition;
        for (const auto& category : catalogs["skyrimspecialedition"].categories) {
            for (const auto& setting : category.settings) {
                specialEdition.insert(setting.label, keysOf(setting));
            }
        }
        for (const auto& category : catalogs["skyrim"].categories) {
            if (category.name != "Gameplay" && category.name != "Controls" && category.name != "Troubleshooting") {
                continue;
            }
            QVERIFY(!category.settings.isEmpty());
            for (const auto& setting : category.settings) {
                QCOMPARE(specialEdition.value(setting.label), keysOf(setting));
            }
        }

        QVERIFY(!IniSettings::forGame("skyrimspecialedition").isEmpty());
        QVERIFY(IniSettings::forGame("eldenring").isEmpty());
    }

    void test_colors_texts_and_toggles()
    {
        QString error;
        const auto catalog = IniSettings::parse(json(g_kinds), &error).value("testgame");
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto& s = catalog.categories[0].settings;
        const auto& packed = s[0];
        const auto& abgr = s[1];
        const auto& text = s[2];
        const auto& bytes = s[3];
        const auto& units = s[4];
        QCOMPARE(packed.colorFormat, IniSettings::Setting::ColorFormat::Packed);
        QCOMPARE(units.keys.size(), 3);

        // reading: the games' defaults
        QCOMPARE(IniSettings::colorOf(packed, { "8947848" }), QColor("#888888"));
        QCOMPARE(IniSettings::colorOf(abgr, { "0" }), QColor(0, 0, 0));
        QCOMPARE(IniSettings::colorOf(abgr, { "-16776961" }), QColor(255, 0, 0));  // 0xFF0000FF, opaque red
        QCOMPARE(IniSettings::colorOf(text, { "255,255,255" }), QColor(255, 255, 255));
        QCOMPARE(IniSettings::colorOf(text, { " 153, 153 ,153" }), QColor(153, 153, 153));
        QCOMPARE(IniSettings::colorOf(bytes, { "187", "187", "187" }), QColor(187, 187, 187));
        QCOMPARE(IniSettings::colorOf(units, { "0.08", "1.0", "0.09" }), QColor(20, 255, 23));
        // values that are no colour
        QVERIFY(!IniSettings::colorOf(text, { "255,255" }).isValid());
        QVERIFY(!IniSettings::colorOf(bytes, { "300", "0", "0" }).isValid());
        QVERIFY(!IniSettings::colorOf(packed, { "grey" }).isValid());
        QVERIFY(!IniSettings::colorOf(units, { "0.5", "", "0.5" }).isValid());

        // writing, and reading back the same colour
        const QColor teal(0x12, 0x34, 0x56);
        QCOMPARE(IniSettings::colorValues(packed, teal), QStringList({ "1193046" }));
        QCOMPARE(IniSettings::colorValues(abgr, QColor(255, 0, 0)), QStringList({ "-16776961" }));
        QCOMPARE(IniSettings::colorValues(text, teal), QStringList({ "18,52,86" }));
        QCOMPARE(IniSettings::colorValues(bytes, teal), QStringList({ "18", "52", "86" }));
        QCOMPARE(IniSettings::colorValues(units, QColor(255, 0, 51)), QStringList({ "1.0000", "0.0000", "0.2000" }));
        for (const auto* setting : { &packed, &abgr, &text, &bytes, &units }) {
            QCOMPARE(IniSettings::colorOf(*setting, IniSettings::colorValues(*setting, teal)), teal);
        }

        // toggles with several keys are on only when every key has its "on" value
        const auto& camera = s[6];
        QCOMPARE(camera.on, QStringList({ "90", "0", "400" }));
        QVERIFY(IniSettings::isOn(camera, { "90.0000", "0", "400" }));
        QVERIFY(!IniSettings::isOn(camera, { "90", "15", "400" }));
        QVERIFY(!IniSettings::isOn(camera, { "75.0", "15.0", "80.0" }));
        // a toggle may write words; its "off" may be empty
        const auto& intro = s[7];
        QVERIFY(IniSettings::isOn(intro, { "bgs_logo.bik" }));
        QVERIFY(!IniSettings::isOn(intro, { "" }));
        QCOMPARE(intro.off, QStringList({ "" }));

        // the page: a text field, a colour button showing the colour, a toggle that writes all its keys
        QTemporaryDir dir;
        auto global = globalSettings(dir.path());
        const auto root = FS::PathCombine(dir.path(), "instance");
        auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(root, "instance.cfg"));
        settings->registerSetting("InstanceType", "Game");
        GameInstance instance(global.get(), std::move(settings), root);
        GameDefinition game;
        game.domain = "testgame";
        game.name = "Test Game";
        game.executable = "Game.exe";
        game.myGamesFolder = "TestGame";
        game.iniFiles = { "Test.ini" };
        GameInstance::applyDefinition(instance.settings(), game);
        const auto ini = FS::PathCombine(instance.myGamesDir(), "Test.ini");
        writeFile(ini, "[Interface]\r\nuSubtitleR=10\r\nuSubtitleG=20\r\nuSubtitleB=30\r\n");

        IniSettingsView view(&instance, catalog);
        QStringList buttons;
        for (auto* button : view.findChildren<QPushButton*>()) {
            buttons << button->text();
        }
        QVERIFY2(buttons.contains("#888888") && buttons.contains("#0A141E") && buttons.contains("#14FF17"), qPrintable(buttons.join(", ")));
        auto* screenshots = view.findChild<QLineEdit*>();
        QVERIFY(screenshots);
        QCOMPARE(screenshots->text(), QString("ScreenShot"));
        screenshots->setText("C:\\Shots\\Skyrim");
        emit screenshots->editingFinished();
        const auto boxes = view.findChildren<QCheckBox*>();
        QCOMPARE(boxes.size(), 2);
        QVERIFY(!boxes[0]->isChecked());  // the map camera's defaults are its "off" values
        QVERIFY(boxes[1]->isChecked());   // the intro video plays by default
        boxes[0]->click();
        boxes[1]->click();
        for (auto* button : view.findChildren<QPushButton*>()) {
            if (button->text() == "Save") {
                button->click();
            }
        }
        QCOMPARE(readFile(ini), QByteArray("[Interface]\r\nuSubtitleR=10\r\nuSubtitleG=20\r\nuSubtitleB=30\r\n\r\n"
                                           "[Display]\r\nsScreenShotBaseName=C:\\Shots\\Skyrim\r\n\r\n"
                                           "[MapMenu]\r\nfMapWorldMaxPitch=90\r\nfMapWorldMinPitch=0\r\nfMapWorldYawRange=400\r\n\r\n"
                                           "[General]\r\nsIntroSequence=\r\n"));
    }

    void test_files_and_view()
    {
        QTemporaryDir dir;
        auto global = globalSettings(dir.path());
        const auto root = FS::PathCombine(dir.path(), "instance");
        auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(root, "instance.cfg"));
        settings->registerSetting("InstanceType", "Game");
        GameInstance instance(global.get(), std::move(settings), root);
        GameDefinition game;
        game.domain = "testgame";
        game.name = "Test Game";
        game.executable = "Game.exe";
        game.myGamesFolder = "TestGame";
        game.iniFiles = { "Test.ini", "TestCustom.ini" };
        GameInstance::applyDefinition(instance.settings(), game);
        QVERIFY(!instance.isMain());
        const auto myGames = instance.myGamesDir();
        QVERIFY2(myGames.startsWith(root), qPrintable(myGames));  // never the real Documents folder
        const auto base = FS::PathCombine(myGames, "Test.ini");
        const auto custom = FS::PathCombine(myGames, "TestCustom.ini");
        const auto enb = FS::PathCombine(instance.gameRoot(), "enblocal.ini");
        writeFile(base, "[Display]\r\nfShadowDistance=8000.0000\r\nbFull Screen=1\r\nbBorderless=0\r\n\r\n[Audio]\r\nfVolume=1.0\r\n");
        writeFile(custom, "[Display]\r\nbFull Screen=0\r\n");
        writeFile(FS::PathCombine(instance.gameRoot(), "Low.ini"), "[Display]\r\nfShadowDistance=3000\r\n[Grass]\r\nfGrassStartFadeDistance=1000\r\n");

        QString error;
        const auto catalog = IniSettings::parse(json(g_catalog), &error).value("testgame");
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto& display = catalog.categories[0].settings;
        IniSettings::Files files(&instance, catalog);
        QCOMPARE(files.value(display[0].keys[0]), QString("8000.0000"));
        // the custom file wins over the base file, like in the game
        QCOMPARE(files.values(display[1]), QStringList({ "0", "0" }));
        QVERIFY(!files.available("enb"));

        // borderless: bFull Screen changes where the game reads it (the custom file), bBorderless in the base file
        files.setValue(display[1].keys[0], "1");
        files.setValue(display[1].keys[1], "1");
        files.setValue(display[0].keys[0], "5000.0000");
        files.setValue(display[2].keys[0], "4096");  // ENB is not installed: nothing to change
        QVERIFY(files.isModified());
        QVERIFY(files.save().has_value());
        QVERIFY(!files.isModified());
        QCOMPARE(readFile(base), QByteArray("[Display]\r\nfShadowDistance=5000.0000\r\nbFull Screen=1\r\nbBorderless=1\r\n\r\n[Audio]\r\nfVolume=1.0\r\n"));
        QCOMPARE(readFile(custom), QByteArray("[Display]\r\nbFull Screen=1\r\n"));
        QVERIFY(!QFileInfo::exists(enb));

        // a quality preset changes the preset file's values and nothing else
        const auto applied = files.applyPreset(FS::PathCombine(instance.gameRoot(), "Low.ini"));
        QVERIFY(applied.has_value());
        QCOMPARE(*applied, 2);
        QVERIFY(files.save().has_value());
        QCOMPARE(readFile(base), QByteArray("[Display]\r\nfShadowDistance=3000\r\nbFull Screen=1\r\nbBorderless=1\r\n\r\n[Audio]\r\nfVolume=1.0\r\n\r\n"
                                            "[Grass]\r\nfGrassStartFadeDistance=1000\r\n"));

        // enblocal.ini linked to the Steam copy: saving replaces the link, the Steam copy keeps its value
        const auto steamEnb = FS::PathCombine(dir.path(), "steam/enblocal.ini");
        writeFile(steamEnb, "[MEMORY]\r\nVideoMemorySizeMb=4096\r\n");
        const auto placed = GameFiles::placeFile(steamEnb, enb, GameFiles::PlaceMode::Link);
        QVERIFY(placed.has_value());
        QCOMPARE(*placed, GameFiles::PlaceMode::Link);
        files.load();
        QVERIFY(files.available("enb"));
        QCOMPARE(files.value(display[2].keys[0]), QString("4096"));
        files.setValue(display[2].keys[0], "8192");
        QVERIFY(files.save().has_value());
        QCOMPARE(readFile(enb), QByteArray("[MEMORY]\r\nVideoMemorySizeMb=8192\r\n"));
        QCOMPARE(readFile(steamEnb), QByteArray("[MEMORY]\r\nVideoMemorySizeMb=4096\r\n"));

        // the view shows the files and saves what the controls change
        IniSettingsView view(&instance, catalog);
        auto* shadows = view.findChild<QDoubleSpinBox*>();
        auto* windowMode = view.findChild<QComboBox*>();
        auto* memory = view.findChild<QSpinBox*>();
        QVERIFY(shadows && windowMode && memory);
        QCOMPARE(shadows->value(), 3000.0);
        QCOMPARE(windowMode->currentText(), QString("Custom (1, 1)"));
        QCOMPARE(memory->value(), 8192);
        QPushButton* save = nullptr;
        QList<QPushButton*> presets;
        for (auto* button : view.findChildren<QPushButton*>()) {
            if (button->text() == "Save") {
                save = button;
            } else if (button->text() == "Low" || button->text() == "Ultra") {
                presets << button;
            }
        }
        QVERIFY(save && !save->isEnabled());
        // only presets the game folder has are offered
        QCOMPARE(presets.size(), 2);
        QVERIFY(!presets[0]->isHidden() || !presets[1]->isHidden());
        QVERIFY(presets[0]->isHidden() != presets[1]->isHidden());

        shadows->setValue(4500);
        windowMode->setCurrentIndex(1);
        emit windowMode->activated(1);
        QCOMPARE(windowMode->count(), 3);  // the custom entry is gone once an option fits
        QVERIFY(save->isEnabled());
        save->click();
        QVERIFY(!save->isEnabled());
        QCOMPARE(readFile(base), QByteArray("[Display]\r\nfShadowDistance=4500.0000\r\nbFull Screen=1\r\nbBorderless=1\r\n\r\n[Audio]\r\nfVolume=1.0\r\n\r\n"
                                            "[Grass]\r\nfGrassStartFadeDistance=1000\r\n"));
        QCOMPARE(readFile(custom), QByteArray("[Display]\r\nbFull Screen=0\r\n"));
    }

    void test_saving_a_mod_ini_keeps_it_the_mods()
    {
        QTemporaryDir dir;
        auto global = globalSettings(dir.path());
        const auto root = FS::PathCombine(dir.path(), "instance");
        auto settings = std::make_unique<INISettingsObject>(FS::PathCombine(root, "instance.cfg"));
        settings->registerSetting("InstanceType", "Game");
        GameInstance instance(global.get(), std::move(settings), root);
        writeFile(FS::PathCombine(instance.modsRoot(), "ENB/enblocal.ini"), "[MEMORY]\r\nVideoMemorySizeMb=4096\r\n");
        auto params = [&](QList<ModEntry> enabled) {
            return DeployTask::Params{ .gameDir = instance.gameRoot(),
                                       .modsDir = instance.modsRoot(),
                                       .overwrittenDir = instance.overwrittenDir(),
                                       .manifestPath = instance.deploymentManifestPath(),
                                       .enabledMods = std::move(enabled) };
        };
        auto run = [](DeployTask& task) {
            QSignalSpy finished(&task, &Task::finished);
            task.start();
            return (task.isFinished() || finished.wait(30000)) && task.wasSuccessful();
        };
        DeployTask deploy(params({ ModEntry{ .id = "ENB", .name = "ENB", .enabled = true, .stamp = 1 } }));
        QVERIFY(run(deploy));

        const auto enb = FS::PathCombine(instance.gameRoot(), "enblocal.ini");
        QVERIFY(Ini::save(&instance, enb, "[MEMORY]\r\nVideoMemorySizeMb=8192\r\n").has_value());
        QCOMPARE(readFile(FS::PathCombine(instance.modsRoot(), "ENB/enblocal.ini")), QByteArray("[MEMORY]\r\nVideoMemorySizeMb=8192\r\n"));
        QVERIFY(GameFiles::isSameFile(enb, FS::PathCombine(instance.modsRoot(), "ENB/enblocal.ini")));

        // the editor's new file is still the mod's: disabling the mod takes it away instead of keeping it as changed
        DeployTask purge(params({}));
        QVERIFY(run(purge));
        QCOMPARE(purge.kept(), 0);
        QVERIFY(!QFileInfo::exists(enb));
        QCOMPARE(readFile(FS::PathCombine(instance.modsRoot(), "ENB/enblocal.ini")), QByteArray("[MEMORY]\r\nVideoMemorySizeMb=8192\r\n"));
    }
};

QTEST_MAIN(IniSettingsTest)

#include "IniSettings_test.moc"
