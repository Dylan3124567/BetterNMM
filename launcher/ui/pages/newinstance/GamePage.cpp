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

#include "GamePage.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include "Application.h"
#include "FileSystem.h"
#include "icons/IconList.h"
#include "InstanceList.h"
#include "game/GameFiles.h"
#include "game/GameInstance.h"
#include "game/GameInstanceCreationTask.h"
#include "game/Steam.h"
#include "net/ApiRequest.h"
#include "net/NetJob.h"
#include "nexus/NexusApi.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "ui/dialogs/ProgressDialog.h"

GamePage::GamePage(NewInstanceDialog* dialog, QWidget* parent) : QWidget(parent), m_dialog(dialog)
{
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Games found in your Steam libraries:"), this));
    m_games = new QListWidget(this);
    m_games->setIconSize(QSize(32, 32));
    layout->addWidget(m_games, 1);

    auto* details = new QGroupBox(tr("Game"), this);
    auto* form = new QFormLayout(details);
    auto* folderRow = new QHBoxLayout();
    m_folder = new QLineEdit(details);
    auto* browse = new QPushButton(tr("Browse..."), details);
    folderRow->addWidget(m_folder, 1);
    folderRow->addWidget(browse);
    form->addRow(tr("Game folder:"), folderRow);
    auto* domainRow = new QHBoxLayout();
    m_domain = new QLineEdit(details);
    m_domain->setToolTip(tr("The game's id in Nexus Mods links, e.g. skyrimspecialedition in nexusmods.com/skyrimspecialedition."));
    auto* find = new QPushButton(tr("Find..."), details);
    find->setToolTip(tr("Search Nexus Mods for the game."));
    domainRow->addWidget(m_domain, 1);
    domainRow->addWidget(find);
    form->addRow(tr("Nexus Mods game:"), domainRow);
    m_name = new QLineEdit(details);
    form->addRow(tr("Game name:"), m_name);
    m_executable = new QLineEdit(details);
    m_executable->setToolTip(tr("The game's executable, relative to the game folder."));
    form->addRow(tr("Executable:"), m_executable);
    layout->addWidget(details);

    auto* kind = new QGroupBox(tr("Instance type"), this);
    auto* kindLayout = new QVBoxLayout(kind);
    m_isolated = new QRadioButton(tr("Isolated instance: its own game folder, saves, INI files and load order. Mods and script extenders "
                                     "installed here affect nothing else."),
                                  kind);
    m_main = new QRadioButton(tr("Steam installation: manage the game folder Steam installed and your normal saves and settings."), kind);
    m_isolated->setChecked(true);
    kindLayout->addWidget(m_isolated);
    kindLayout->addWidget(m_main);
    layout->addWidget(kind);

    m_installExtender = new QCheckBox(this);
    m_installExtender->setToolTip(tr("Downloads the script extender build made for this game's version from Nexus Mods and installs it "
                                     "as a mod of the new instance, so it can be turned off or removed like any other mod."));
    layout->addWidget(m_installExtender);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    layout->addWidget(m_info);

    connect(m_games, &QListWidget::currentRowChanged, this, &GamePage::gameSelected);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto dir = QFileDialog::getExistingDirectory(this, tr("Game folder"), m_folder->text());
        if (!dir.isEmpty()) {
            m_folder->setText(QDir::toNativeSeparators(dir));
        }
    });
    connect(find, &QPushButton::clicked, this, &GamePage::findNexusGame);
    // OK is enabled by update(): checking only when a field loses focus left it greyed out under the pointer, since a
    // disabled button never takes the focus. Typing checks again once it pauses (update() reads the Steam libraries).
    auto* typed = new QTimer(this);
    typed->setSingleShot(true);
    typed->setInterval(250);
    connect(typed, &QTimer::timeout, this, &GamePage::update);
    for (auto* edit : { m_folder, m_domain, m_name, m_executable }) {
        connect(edit, &QLineEdit::editingFinished, this, &GamePage::update);
        connect(edit, &QLineEdit::textEdited, typed, qOverload<>(&QTimer::start));
    }
    connect(m_isolated, &QRadioButton::toggled, this, &GamePage::update);
}

bool GamePage::installScriptExtender() const
{
    return !m_installExtender->isHidden() && m_installExtender->isChecked();
}

void GamePage::openedImpl()
{
    if (!m_populated) {
        m_populated = true;
        populate();
    }
    update();
}

void GamePage::populate()
{
    m_games->clear();
    for (const auto& app : Steam::installedApps(Steam::steamRoot())) {
        const auto game = GameRegistry::bySteamAppId(app.appId);
        if (!game.isValid()) {
            continue;
        }
        auto* item = new QListWidgetItem(game.name, m_games);
        item->setToolTip(QDir::toNativeSeparators(app.installDir));
        item->setData(Qt::UserRole, app.appId);
        item->setData(Qt::UserRole + 1, app.installDir);
        item->setData(Qt::UserRole + 2, game.domain);
        item->setIcon(APPLICATION->icons()->getIcon(GameIcons::iconForGame(game, app.installDir)));
    }
    auto* other = new QListWidgetItem(tr("Another game or folder..."), m_games);
    other->setData(Qt::UserRole, 0);
    other->setIcon(QIcon::fromTheme("viewfolder"));
    m_games->setCurrentRow(0);
}

void GamePage::gameSelected()
{
    auto* item = m_games->currentItem();
    if (!item) {
        return;
    }
    m_appId = item->data(Qt::UserRole).toInt();
    if (m_appId == 0) {
        m_folder->clear();
        m_domain->clear();
        m_name->clear();
        m_executable->clear();
        m_folder->setFocus();
        update();
        return;
    }
    const auto game = GameRegistry::byDomain(item->data(Qt::UserRole + 2).toString());
    m_folder->setText(QDir::toNativeSeparators(item->data(Qt::UserRole + 1).toString()));
    m_domain->setText(game.domain);
    m_name->setText(game.name);
    m_executable->setText(QDir::toNativeSeparators(game.executable));
    update();
}

void GamePage::findNexusGame()
{
    bool ok = false;
    const auto text = QInputDialog::getText(this, tr("Find a game on Nexus Mods"), tr("Game name:"), QLineEdit::Normal, m_name->text(), &ok);
    if (!ok || text.trimmed().isEmpty()) {
        return;
    }
    auto job = makeShared<NetJob>(tr("Searching Nexus Mods games"), APPLICATION->network());
    job->setAskRetry(false);
    auto [request, data] = Net::ApiRequest::makeByteArray(Nexus::graphqlUrl(), Nexus::searchGamesQuery(text, 25));
    job->addNetAction(request);
    ProgressDialog progress(this);
    progress.execWithTask(job.get());
    auto games = job->wasSuccessful() ? Nexus::parseGames(*data) : Result<QList<Nexus::GameInfo>>(std::unexpected(job->failReason()));
    if (!games || games->isEmpty()) {
        QMessageBox::warning(this, tr("Find a game"), games ? tr("Nexus Mods has no game called \"%1\".").arg(text) : games.error());
        return;
    }
    QStringList labels;
    for (const auto& game : *games) {
        labels << QString("%1 (%2)").arg(game.name, game.domain);
    }
    const auto chosen = QInputDialog::getItem(this, tr("Find a game"), tr("Game:"), labels, 0, false, &ok);
    if (!ok) {
        return;
    }
    const auto& game = games->at(static_cast<int>(labels.indexOf(chosen)));
    m_domain->setText(game.domain);
    m_name->setText(game.name);
    if (const auto known = GameRegistry::byDomain(game.domain); known.isValid() && m_executable->text().isEmpty()) {
        m_executable->setText(QDir::toNativeSeparators(known.executable));
    }
    update();
}

void GamePage::update()
{
    const auto folder = QDir::cleanPath(QDir::fromNativeSeparators(m_folder->text().trimmed()));
    const auto domain = m_domain->text().trimmed().toLower();
    auto game = GameRegistry::byDomain(domain);
    if (!game.isValid()) {
        game.domain = domain;
        game.name = m_name->text().trimmed();
    }
    game.executable = QDir::fromNativeSeparators(m_executable->text().trimmed());
    if (!m_name->text().trimmed().isEmpty()) {
        game.name = m_name->text().trimmed();
    }
    int appId = m_appId;
    if (appId <= 0 && QFileInfo(folder).isDir()) {
        if (auto app = Steam::findAppByInstallDir(Steam::steamRoot(), folder)) {
            appId = app->appId;
        }
    }

    // one Steam instance per installation: two would deploy over each other's files
    auto* steamInstance = folder.isEmpty() ? nullptr : GameInstance::mainInstanceFor(folder, appId);
    m_main->setEnabled(!steamInstance);
    m_main->setText(steamInstance ? tr("Steam installation: already managed by \"%1\"; there can only be one.").arg(steamInstance->name())
                                  : tr("Steam installation: manage the game folder Steam installed and your normal saves and settings."));
    if (steamInstance && m_main->isChecked()) {
        const QSignalBlocker block(m_isolated);
        m_isolated->setChecked(true);
    }
    const bool main = m_main->isChecked();

    const auto loader = QFileInfo(game.scriptExtender).fileName();
    m_installExtender->setHidden(loader.isEmpty() || GameRegistry::byDomain(domain).scriptExtenderMod <= 0);
    m_installExtender->setText(tr("Install the script extender (%1) from Nexus Mods").arg(loader));

    auto fail = [this](const QString& why) {
        m_info->setText(why);
        m_dialog->setSuggestedPack(QString(), nullptr);
    };
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        fail(tr("Choose the folder the game is installed in."));
        return;
    }
    if (domain.isEmpty() || game.name.isEmpty()) {
        fail(tr("Choose which Nexus Mods game this is, so mods can be downloaded for it."));
        return;
    }
    if (game.executable.isEmpty() || !QFileInfo::exists(FS::PathCombine(folder, game.executable))) {
        fail(tr("The executable %1 is not in the game folder.").arg(QDir::toNativeSeparators(game.executable)));
        return;
    }
    // the instance goes on the game's drive, where its files can be hard links of each other
    const auto target = InstancePlacement::instanceDirFor(folder);
    m_dialog->setTargetDir(target);
    const bool linked = GameFiles::isSameVolume(target, folder) && GameFiles::supportsHardLinks(folder);
    const auto why = GameFiles::isSameVolume(target, folder) ? tr("the drive of %1 has no hard links").arg(QDir::toNativeSeparators(folder))
                                                             : tr("%1 is on another drive than the game").arg(QDir::toNativeSeparators(target));
    if (main) {
        m_info->setText(linked ? tr("Mods you enable are linked into %1. The files they replace are kept in the instance folder and put "
                                    "back when you disable them. Your saves are never touched.")
                                     .arg(QDir::toNativeSeparators(folder))
                               : tr("Warning: %1, so mods you enable are copied into the game folder instead of linked and take their "
                                    "space twice. The files they replace are kept in the instance folder. Your saves are never touched.")
                                     .arg(why));
    } else {
        m_info->setText(linked ? tr("The instance goes in %1. Its game files are hard links to %2, so it takes almost no extra disk "
                                    "space. Executables, DLLs and configs are copied so patching them only changes this instance.")
                                     .arg(QDir::toNativeSeparators(target), QDir::toNativeSeparators(folder))
                               : tr("Warning: %1, so game files are copied instead of linked and use as much space as the game. "
                                    "BetterNMM checks there is room before copying.")
                                     .arg(why));
    }
    m_dialog->setSuggestedIcon(GameIcons::iconForGame(game, folder));
    m_dialog->setSuggestedPack(main ? tr("%1 (Steam)").arg(game.name) : game.name,
                               new GameInstanceCreationTask(game, folder, appId, main ? GameInstance::Kind::Main : GameInstance::Kind::Isolated));
}
