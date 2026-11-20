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

#include "ModInstallFlow.h"

#include <QDateTime>
#include <QtConcurrent/QtConcurrentRun>
#include <QVBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QFutureWatcher>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirListing>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScopeGuard>
#include <QSet>
#include <QStandardPaths>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QUuid>

#include <filesystem>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "StringUtils.h"
#include "archive/ArchiveReader.h"
#include "archive/ExtractZipTask.h"
#include "game/AutoSort.h"
#include "game/Deployer.h"
#include "game/Fomod.h"
#include "game/FomodPatches.h"
#include "game/GameFiles.h"
#include "game/GameInstance.h"
#include "game/ModInstaller.h"
#include "game/Nemesis.h"
#include "game/PluginList.h"
#include "net/ApiRequest.h"
#include "net/DownloadManager.h"
#include "net/NetJob.h"
#include "nexus/NexusAccount.h"
#include "nexus/NexusApi.h"
#include "nexus/NxmLink.h"
#include "DesktopServices.h"
#include "game/GameDefinition.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/FomodDialog.h"
#include "ui/dialogs/NexusDownloadQueue.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/InstanceWindow.h"
#include "ui/MainWindow.h"

namespace ModInstallFlow {

namespace {

void showError(QWidget* parent, const QString& title, const QString& text)
{
    CustomMessageBox::selectable(parent, title, text, QMessageBox::Critical)->exec();
}

/// What deployment left alone and why: files that were changed in the game folder after it placed them
void showDeployWarnings(const Task* task, QWidget* parent)
{
    if (const auto warnings = task->warnings(); !warnings.isEmpty()) {
        CustomMessageBox::selectable(parent, QObject::tr("Changed files were kept"), warnings.join("\n\n"), QMessageBox::Warning)->exec();
    }
}

bool refuseWhileRunning(GameInstance* instance, QWidget* parent)
{
    if (!instance->isRunning()) {
        return false;
    }
    showError(parent, QObject::tr("Instance is running"),
              QObject::tr("%1 is running. Close the game before changing its mods.").arg(instance->name()));
    return true;
}

bool runTask(Task* task, QWidget* parent, const QString& errorTitle)
{
    ProgressDialog dialog(parent);
    dialog.execWithTask(task);
    if (task->wasSuccessful()) {
        return true;
    }
    if (task->getState() != Task::State::AbortedByUser) {
        showError(parent, errorTitle, task->failReason());
    }
    return false;
}

/// Lower-case plugin file names a staged mod provides
QSet<QString> pluginsIn(const QString& stagedMod)
{
    QSet<QString> out;
    for (const auto& entry : QDirListing(stagedMod, QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly)) {
        const auto name = entry.fileName().toLower();
        if (name.endsWith(".esp") || name.endsWith(".esm") || name.endsWith(".esl")) {
            out.insert(name);
        }
    }
    return out;
}

/// Where Check Patches keeps what it read from archives
QString patchCache()
{
    return FS::PathCombine(APPLICATION->dataRoot(), "cache", "fomod");
}

/// The state of plugin files in the game for FOMOD conditions, taken now so it can be asked on a worker thread
FomodPatches::FileStates fileStates(GameInstance* instance)
{
    QHash<QString, Fomod::FileState> plugins;
    if (instance->definition().hasPlugins()) {
        for (const auto& plugin : instance->pluginList()->plugins()) {
            plugins.insert(plugin.name.toLower(), plugin.active ? Fomod::FileState::Active : Fomod::FileState::Inactive);
        }
    }
    return [plugins, dataDir = instance->dataDir()](const QString& file) {
        if (const auto it = plugins.constFind(QFileInfo(file).fileName().toLower()); it != plugins.cend()) {
            return *it;
        }
        return QFileInfo::exists(FS::PathCombine(dataDir, file)) ? Fomod::FileState::Active : Fomod::FileState::Missing;
    };
}

/// Switches on @p names (lower-case plugin file names) the way they would be after installing a mod by hand
void activatePlugins(GameInstance* instance, const QSet<QString>& names, QWidget* parent)
{
    if (names.isEmpty() || !instance->definition().hasPlugins()) {
        return;
    }
    auto* plugins = instance->pluginList();
    if (auto refreshed = plugins->refresh(); !refreshed) {
        return;
    }
    QList<int> rows;
    for (int i = 0; i < plugins->plugins().size(); i++) {
        if (names.contains(plugins->plugins()[i].name.toLower())) {
            rows << i;
        }
    }
    plugins->setActive(rows, true);
    if (auto saved = plugins->save(); !saved) {
        showError(parent, QObject::tr("Load order"), saved.error());
    }
}

/// Plugins of mods installed while their game ran (instance id -> names), and what waits for the game to stop
QHash<QString, QSet<QString>> g_pluginsAfterExit;
QHash<QString, QMetaObject::Connection> g_afterExit;

/**
 * The game has its folder in use while it runs: what was installed or changed meanwhile is deployed, and the new
 * plugins switched on, once it stops. Nothing interrupts the game.
 */
void deployAfterExit(GameInstance* instance, const QSet<QString>& plugins, QWidget* parent)
{
    const auto id = instance->id();
    g_pluginsAfterExit[id].unite(plugins);
    if (g_afterExit.contains(id)) {
        return;
    }
    QPointer<QWidget> owner(parent);
    g_afterExit.insert(id, QObject::connect(instance, &BaseInstance::runningStatusChanged, instance, [instance, id, owner](bool running) {
        if (running) {
            return;
        }
        QObject::disconnect(g_afterExit.take(id));
        // after the launch has finished handling the exit
        QTimer::singleShot(0, instance, [instance, id, owner] {
            const auto names = g_pluginsAfterExit.take(id);
            if (deploy(instance, owner)) {
                activatePlugins(instance, names, owner);
            }
        });
    }));
}

/// Nexus file names look like "Name-1234-1-0-1700000000.7z"; drop the id, version and timestamp
void setUpNemesis(GameInstance* instance, QWidget* parent);

QString nameFromArchive(const QString& archive, int modId)
{
    auto base = QFileInfo(archive).completeBaseName();
    if (modId > 0) {
        if (const int at = base.indexOf(QString("-%1-").arg(modId)); at > 0) {
            base = base.left(at);
        }
    }
    return base.trimmed();
}

}  // namespace

namespace {

/// Runs one GraphQL request per batch of ids (Nexus Mods answers about 50 mods per request comfortably) and parses each
template <typename T>
Result<QList<T>> fetchBatches(const QString& title, const QList<int>& ids, const std::function<QByteArray(const QList<int>&)>& query,
                              const std::function<Result<T>(const QByteArray&)>& parse, QWidget* parent)
{
    auto job = makeShared<NetJob>(title, APPLICATION->network());
    job->setAskRetry(false);
    QList<std::pair<Net::Request::Ptr, QByteArray*>> batches;
    for (qsizetype start = 0; start < ids.size(); start += 50) {
        auto [request, data] = Net::ApiRequest::makeByteArray(Nexus::graphqlUrl(), query(ids.mid(start, 50)));
        job->addNetAction(request);
        batches.append({ request, data });
    }
    if (batches.isEmpty()) {
        return QList<T>{};
    }
    ProgressDialog progress(parent);
    progress.execWithTask(job.get());
    QList<T> results;
    for (const auto& [request, data] : batches) {
        if (!request->wasSuccessful()) {
            const int status = request->replyStatusCode();
            return std::unexpected(status > 0 ? Nexus::errorMessage(status, *data) : request->failReason());
        }
        TRY_INTO(auto parsed, parse(*data));
        results << parsed;
    }
    return results;
}

}  // namespace

Result<QList<Nexus::ModDetails>> fetchModDetails(const QString& game, const QList<int>& modIds, QWidget* parent)
{
    const auto batches = fetchBatches<QList<Nexus::ModDetails>>(
        QObject::tr("Asking Nexus Mods about %n mod(s)", nullptr, modIds.size()), modIds,
        [&game](const QList<int>& ids) { return Nexus::modDetailsQuery(game, ids); }, Nexus::parseModDetails, parent);
    if (!batches) {
        return std::unexpected(batches.error());
    }
    QList<Nexus::ModDetails> mods;
    for (const auto& batch : *batches) {
        mods << batch;
    }
    return mods;
}

Result<QMap<int, QList<Nexus::ModFile>>> fetchModFiles(int gameId, const QList<int>& modIds, QWidget* parent)
{
    using Files = QMap<int, QList<Nexus::ModFile>>;
    const auto batches = fetchBatches<Files>(
        QObject::tr("Finding the files of %n mod(s)", nullptr, modIds.size()), modIds,
        [gameId](const QList<int>& ids) { return Nexus::modFilesBatchQuery(gameId, ids); }, Nexus::parseModFilesBatch, parent);
    if (!batches) {
        return std::unexpected(batches.error());
    }
    Files files;
    for (const auto& batch : *batches) {
        files.insert(batch);
    }
    return files;
}

QString downloadTarget(GameInstance* instance, const QString& fileName)
{
    auto name = FS::RemoveInvalidFilenameChars(fileName);
    if (name.isEmpty()) {
        name = QUuid::createUuid().toString(QUuid::Id128) + ".zip";
    }
    return FS::PathCombine(instance->downloadsDir(), name);
}

namespace {

/// Nexus Mods lookups on their way (a file's details and download address), kept alive until they finish
QList<NetJob::Ptr> g_lookups;

void startLookup(const NetJob::Ptr& job, std::function<void()> done)
{
    g_lookups << job;
    QObject::connect(job.get(), &Task::finished, APPLICATION, [raw = job.get(), done = std::move(done)] {
        done();
        // dropped after its signal, never inside it
        QTimer::singleShot(0, APPLICATION, [raw] { g_lookups.removeIf([raw](const NetJob::Ptr& kept) { return kept.get() == raw; }); });
    });
    job->start();
}

QWidget* dialogParent(QWidget* preferred)
{
    return preferred ? preferred : static_cast<QWidget*>(APPLICATION->mainWindow());
}

/// Downloads waiting for their turn to install, in the order they finished
struct PendingInstall {
    QString instanceId;
    QString archive;
    ModEntry metadata;
};
QList<PendingInstall> g_installs;
bool g_installing = false;

bool downloadAndInstall(GameInstance* instance, const QString& game, int modId, int fileId, const QString& key, qint64 expires, QWidget* parent)
{
    if (APPLICATION->accounts()->apiKey().isEmpty()) {
        // without a key Nexus Mods hands out no link here; the download window gets the file with one click instead, named
        // and versioned from what Nexus Mods tells without a key (left to the archive when that fails)
        NexusFile entry{ .modId = modId };
        entry.file.fileId = fileId;
        if (const auto details = fetchModDetails(game, { modId }, parent); details && !details->isEmpty()) {
            entry.modName = details->first().name;
            entry.modVersion = details->first().version;
            if (const auto files = fetchModFiles(details->first().gameId, { modId }, parent)) {
                for (const auto& candidate : files->value(modId)) {
                    if (candidate.fileId == fileId) {
                        entry.file = candidate;
                    }
                }
            }
        }
        downloadFromWebsite(instance, game, { entry }, parent);
        return true;
    }

    // the mod's details, the file's details and the download address are asked for in the background, so more links can
    // be clicked meanwhile; the download starts as soon as they arrive and installs when it is complete
    auto job = makeShared<NetJob>(QObject::tr("Nexus Mods file %1").arg(fileId), APPLICATION->network());
    auto [infoRequest, info] = Net::ApiRequest::makeByteArray(Nexus::apiUrl(QString("games/%1/mods/%2.json").arg(game).arg(modId)));
    auto [filesRequest, files] = Net::ApiRequest::makeByteArray(Nexus::apiUrl(QString("games/%1/mods/%2/files.json").arg(game).arg(modId)));
    auto [linkRequest, links] = Net::ApiRequest::makeByteArray(Nexus::downloadLinkUrl(game, modId, fileId, key, expires));
    job->addNetAction(infoRequest);
    job->addNetAction(filesRequest);
    job->addNetAction(linkRequest);
    job->setAskRetry(false);
    QPointer<QWidget> owner(parent);
    startLookup(job, [infoRequest, filesRequest, linkRequest, info, files, links, owner, instanceId = instance->id(), game, modId, fileId, key, expires] {
        const auto title = QObject::tr("Nexus Mods download failed");
        auto* target = APPLICATION->instances()->getInstanceById(instanceId);
        if (!target) {
            return;
        }
        for (const auto& request : { linkRequest, infoRequest, filesRequest }) {
            if (request->wasSuccessful()) {
                continue;
            }
            const int status = request->replyStatusCode();
            CustomMessageBox::selectable(dialogParent(owner), title, status > 0 ? Nexus::errorMessage(status, {}) : request->failReason(),
                                         QMessageBox::Critical)
                ->show();
            return;
        }
        const auto modInfo = Nexus::parseModInfo(*info);
        const auto fileList = Nexus::parseModFiles(*files);
        const auto linkList = Nexus::parseDownloadLinks(*links);
        if (!linkList || linkList->isEmpty()) {
            CustomMessageBox::selectable(dialogParent(owner), title,
                                         linkList ? QObject::tr("Nexus Mods gave no download address.") : linkList.error(), QMessageBox::Critical)
                ->show();
            return;
        }
        Nexus::ModFile file;
        if (fileList) {
            for (const auto& candidate : *fileList) {
                if (candidate.fileId == fileId) {
                    file = candidate;
                }
            }
        }

        ModEntry metadata;
        metadata.nexusGame = game;
        metadata.nexusModId = modId;
        metadata.nexusFileId = fileId;
        metadata.version = file.version;
        if (modInfo) {
            const bool mainFile = file.name.isEmpty() || file.name == modInfo->name || file.category.compare("MAIN", Qt::CaseInsensitive) == 0;
            metadata.name = mainFile ? modInfo->name : QString("%1 - %2").arg(modInfo->name, file.name);
            metadata.latestVersion = modInfo->version;
            if (metadata.version.isEmpty()) {
                metadata.version = modInfo->version;
            }
        }

        DownloadItem download;
        download.url = linkList->first().url;
        download.target = downloadTarget(target, file.fileName.isEmpty() ? QFileInfo(download.url.path()).fileName() : file.fileName);
        download.size = file.size;
        download.instanceId = instanceId;
        download.metadata = metadata;
        download.nxmKey = key;
        download.nxmExpires = expires;
        // a complete earlier download of unknown size is used as it is
        if (download.size <= 0 && QFileInfo::exists(download.target)) {
            queueInstall(instanceId, download.target, metadata);
            return;
        }
        APPLICATION->downloads()->add(download);
    });
    return true;
}

}  // namespace

QStringList pickArchives(GameInstance* instance, QWidget* parent)
{
    // a folder that does not exist yet would end up in the dialog's file name box
    auto start = QDir(instance->downloadsDir()).absolutePath();
    if (!QFileInfo(start).isDir()) {
        start = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    return QFileDialog::getOpenFileNames(parent, QObject::tr("Install mods into %1").arg(instance->name()), start,
                                         QObject::tr("Mod archives (*.zip *.7z *.rar);;All files (*)"));
}

bool isInstanceArchive(const QString& archive)
{
    MMCZip::ArchiveReader reader(archive);
    if (!reader.collectFiles()) {
        return false;
    }
    for (const auto& file : reader.getFiles()) {
        const auto clean = QString(file).replace('\\', '/');
        if (clean.compare("instance.cfg", Qt::CaseInsensitive) == 0 || (clean.count('/') == 1 && clean.endsWith("/instance.cfg", Qt::CaseInsensitive))) {
            return true;
        }
    }
    return false;
}

GameInstance* chooseInstance(const QString& gameDomain, QWidget* parent, GameInstance* preferred)
{
    auto* list = APPLICATION->instances();
    QList<GameInstance*> candidates;
    for (int i = 0; i < list->count(); i++) {
        auto* inst = list->at(i);
        if (gameDomain.isEmpty() || inst->settings()->get("GameDomain").toString().compare(gameDomain, Qt::CaseInsensitive) == 0) {
            candidates << inst;
        }
    }
    if (candidates.isEmpty()) {
        showError(parent, QObject::tr("No instance"),
                  gameDomain.isEmpty() ? QObject::tr("Create an instance first, then install mods into it.")
                                       : QObject::tr("There is no instance of the game \"%1\". Create one with \"Add Instance\" first.").arg(gameDomain));
        return nullptr;
    }
    if (candidates.size() == 1) {
        return candidates.first();
    }
    QStringList labels;
    int current = 0;
    for (int i = 0; i < candidates.size(); i++) {
        auto* inst = candidates[i];
        labels << QString("%1 (%2, %3)").arg(inst->name(), inst->settings()->get("GameName").toString(),
                                             inst->isMain() ? QObject::tr("Steam installation") : QObject::tr("isolated"));
        if (inst == preferred) {
            current = i;
        }
    }
    bool ok = false;
    const auto chosen = QInputDialog::getItem(parent, QObject::tr("Choose instance"), QObject::tr("Install into which instance?"), labels, current,
                                              false, &ok);
    if (!ok) {
        return nullptr;
    }
    return candidates.value(static_cast<int>(labels.indexOf(chosen)));
}

bool installArchive(GameInstance* instance, const QString& archive, ModEntry metadata, QWidget* parent)
{
    auto* mods = instance->modList();
    const auto game = instance->definition();
    const auto key = QUuid::createUuid().toString(QUuid::Id128).left(8);
    const auto extracted = FS::PathCombine(instance->modsRoot(), ".extract-" + key);
    const auto staging = FS::PathCombine(instance->modsRoot(), ".staging-" + key);
    auto cleanup = qScopeGuard([&] {
        GameFiles::removeTree(extracted);
        GameFiles::removeTree(staging);
    });

    {
        MMCZip::ExtractZipTask extract(archive, QDir(extracted));
        if (!runTask(&extract, parent, QObject::tr("Could not extract %1").arg(QFileInfo(archive).fileName()))) {
            return false;
        }
    }
    // Check Patches only looks in the downloads folder for archives (the downloads folder and the cache are BetterNMM's
    // settings, which an install outside it has not got)
    if (APPLICATION_DYN && QFileInfo(archive).dir() == QDir(instance->downloadsDir())) {
        FomodPatches::remember(archive, extracted, patchCache());
    }

    const auto analysis = ModInstaller::analyze(extracted, game, instance->gameRoot());
    if (metadata.name.isEmpty()) {
        metadata.name = analysis.name.isEmpty() ? nameFromArchive(archive, metadata.nexusModId) : analysis.name;
    }
    if (metadata.version.isEmpty()) {
        metadata.version = analysis.version;
    }

    // a mod that is already installed (the same Nexus mod, or for a file from disk the same name or archive) can be
    // replaced, keeping its place and state; No installs this one next to it
    QString id;
    if (const int row = mods->indexOfInstalled(metadata, QFileInfo(archive).fileName()); row >= 0) {
        const auto existing = mods->mods()[row];
        const auto question = existing.version.isEmpty() || metadata.version.isEmpty()
                                  ? QObject::tr("%1 is installed already. Replace it with this one?").arg(existing.name)
                                  : QObject::tr("%1 is installed already (version %2). Replace it with version %3?")
                                        .arg(existing.name, existing.version, metadata.version);
        const auto answer = QMessageBox::question(parent, QObject::tr("Update mod"),
                                                  question + QStringLiteral("\n\n") + QObject::tr("No installs it as a separate mod."),
                                                  QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) {
            return false;
        }
        if (answer == QMessageBox::Yes) {
            id = existing.id;
            // a file from disk knows nothing of its Nexus mod; the copy it replaces does
            if (metadata.nexusModId <= 0) {
                metadata.name = existing.name;
                metadata.nexusGame = existing.nexusGame;
                metadata.nexusModId = existing.nexusModId;
                metadata.nexusFileId = existing.nexusFileId;
                metadata.latestVersion = existing.latestVersion;
                if (metadata.version.isEmpty()) {
                    metadata.version = existing.version;
                }
            }
        }
    }

    if (!analysis.fomodConfig.isEmpty()) {
        QFile config(analysis.fomodConfig);
        if (!config.open(QIODevice::ReadOnly)) {
            showError(parent, QObject::tr("Installer error"), config.errorString());
            return false;
        }
        auto module = Fomod::parse(config.readAll());
        if (!module) {
            showError(parent, QObject::tr("Installer error"), module.error());
            return false;
        }
        Fomod::Context context;
        context.fileState = fileStates(instance);
        FomodDialog wizard(*module, analysis.fomodRoot, context, metadata.name, parent);
        if (wizard.exec() != QDialog::Accepted) {
            return false;
        }
        // kept so Check Patches knows which options this copy has
        metadata.choices = FomodPatches::choiceNames(*module, wizard.selections(), context);
        if (auto staged = ModInstaller::stageFomod(wizard.files(), analysis, game, staging); !staged) {
            showError(parent, QObject::tr("Installing %1 failed").arg(metadata.name), staged.error());
            return false;
        }
    } else {
        // the files of a scripted installer are usually options to pick from, so copying them all rarely works
        const bool scripted = !analysis.fomodRoot.isEmpty() && !ModInstaller::resolvePath(analysis.fomodRoot, "fomod/script.cs").isEmpty();
        if (scripted && QMessageBox::question(parent, QObject::tr("Scripted installer"),
                                              QObject::tr("%1 comes with a C# script installer, which BetterNMM can not run.\n\n"
                                                          "Install all files in the archive as they are? The mod page usually describes "
                                                          "which files to keep.")
                                                  .arg(metadata.name)) != QMessageBox::Yes) {
            return false;
        }
        if (auto staged = ModInstaller::stage(analysis, game, staging); !staged) {
            showError(parent, QObject::tr("Installing %1 failed").arg(metadata.name), staged.error());
            return false;
        }
    }

    // an empty archive, or one with only what the installer leaves out, stages nothing
    if (QDirListing staged(staging, QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly); staged.begin() == staged.end()) {
        showError(parent, QObject::tr("Installing %1 failed").arg(metadata.name),
                  QObject::tr("%1 has no files to install.").arg(QFileInfo(archive).fileName()));
        return false;
    }
    if (id.isEmpty()) {
        id = mods->uniqueId(metadata.name);
    }
    const auto target = mods->modDir(id);
    // deployed links of the old version keep it alive on disk until the next deploy replaces them; all or nothing, so a
    // file in use never leaves the old version half deleted
    if (auto removed = GameFiles::removeTreeWhole(target); !removed) {
        showError(parent, QObject::tr("Installing %1 failed").arg(metadata.name), removed.error());
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(StringUtils::toStdString(staging), StringUtils::toStdString(target), ec);
    if (ec) {
        showError(parent, QObject::tr("Installing %1 failed").arg(metadata.name),
                  QObject::tr("Could not move the mod into place: %1").arg(QString::fromStdString(ec.message())));
        return false;
    }

    metadata.id = id;
    metadata.enabled = true;
    metadata.archive = QFileInfo(archive).fileName();
    metadata.stamp = QDateTime::currentMSecsSinceEpoch();
    const bool isNew = mods->indexOf(id) < 0;
    mods->upsert(metadata);
    if (isNew) {
        mods->setEnabled({ mods->indexOf(id) }, true);
    }

    // plugins the mod brings are switched on, the way they would be after installing it by hand
    if (instance->isRunning()) {
        deployAfterExit(instance, pluginsIn(target), parent);
        return true;
    }
    if (!deploy(instance, parent)) {
        return false;
    }
    activatePlugins(instance, pluginsIn(target), parent);
    return true;
}

void queueInstall(const QString& instanceId, const QString& archive, const ModEntry& metadata)
{
    g_installs << PendingInstall{ instanceId, archive, metadata };
    // from the event loop, so an install never starts inside the dialog or download that finished
    QTimer::singleShot(0, APPLICATION, [] { installQueuedDownloads(); });
}

void installQueuedDownloads()
{
    // one installer at a time, and none while BetterNMM's Nexus Mods window waits for clicks (it calls this when it closes)
    if (g_installing || APPLICATION->nexusDownloads()->isWindowOpen()) {
        return;
    }
    g_installing = true;
    while (!g_installs.isEmpty()) {
        const auto next = g_installs.takeFirst();
        auto* instance = APPLICATION->instances()->getInstanceById(next.instanceId);
        if (!instance) {
            CustomMessageBox::selectable(APPLICATION->mainWindow(), QObject::tr("Install mod"),
                                         QObject::tr("%1 was downloaded for the instance %2, which no longer exists. The file is in %3.")
                                             .arg(next.metadata.name.isEmpty() ? QFileInfo(next.archive).fileName() : next.metadata.name,
                                                  next.instanceId, QDir::toNativeSeparators(QFileInfo(next.archive).absolutePath())),
                                         QMessageBox::Warning)
                ->show();
            continue;
        }
        installArchive(instance, next.archive, next.metadata, dialogParent(instance->window()));
    }
    g_installing = false;
}

void refreshNexusLink(const DownloadItem& item, std::function<void(const QUrl&, const QString&)> answer)
{
    const auto expired = QObject::tr("The download address expired. Download the file from Nexus Mods again (Mod Manager Download, or the "
                                     "download window): it continues where it stopped.");
    const auto& mod = item.metadata;
    const bool premium = APPLICATION->accounts()->isPremium();
    const bool keyValid = !item.nxmKey.isEmpty() && item.nxmExpires > QDateTime::currentSecsSinceEpoch();
    if (mod.nexusGame.isEmpty() || mod.nexusModId <= 0 || mod.nexusFileId <= 0 || APPLICATION->accounts()->apiKey().isEmpty() ||
        (!premium && !keyValid)) {
        answer({}, expired);
        return;
    }
    auto job = makeShared<NetJob>(QObject::tr("New address for %1").arg(item.fileName()), APPLICATION->network());
    auto [request, data] = Net::ApiRequest::makeByteArray(Nexus::downloadLinkUrl(mod.nexusGame, mod.nexusModId, mod.nexusFileId,
                                                                                 premium ? QString() : item.nxmKey, premium ? 0 : item.nxmExpires));
    job->addNetAction(request);
    job->setAskRetry(false);
    startLookup(job, [request, data, answer = std::move(answer), expired] {
        if (!request->wasSuccessful()) {
            const int status = request->replyStatusCode();
            answer({}, status > 0 ? Nexus::errorMessage(status, {}) : request->failReason());
            return;
        }
        const auto links = Nexus::parseDownloadLinks(*data);
        if (!links || links->isEmpty()) {
            answer({}, links ? expired : links.error());
            return;
        }
        answer(links->first().url, {});
    });
}

bool installNexusFile(GameInstance* instance, const NxmLink& link, QWidget* parent)
{
    if (link.expired()) {
        showError(parent, QObject::tr("Nexus Mods download failed"), Nexus::errorMessage(410, {}));
        return false;
    }
    if (auto account = APPLICATION->accounts()->defaultAccount(); account && link.userId > 0 && account->userId != link.userId) {
        showError(parent, QObject::tr("Nexus Mods download failed"),
                  QObject::tr("This link was made for another Nexus Mods account than your default one (%1).").arg(account->name));
        return false;
    }
    return downloadAndInstall(instance, link.game, link.modId, link.fileId, link.key, link.expires, parent);
}

bool installNexusFile(GameInstance* instance, const QString& game, int modId, int fileId, QWidget* parent)
{
    return downloadAndInstall(instance, game, modId, fileId, {}, 0, parent);
}

bool installScriptExtender(GameInstance* instance, QWidget* parent)
{
    const auto title = QObject::tr("Install script extender");
    const auto def = instance->definition();
    const int modId = GameRegistry::byDomain(def.domain).scriptExtenderMod;
    if (modId <= 0 || def.scriptExtender.isEmpty()) {
        showError(parent, title, QObject::tr("BetterNMM does not know where to get the script extender of %1.").arg(def.name));
        return false;
    }
    if (refuseWhileRunning(instance, parent)) {
        return false;
    }
    const auto gameVersion = GameFiles::fileVersion(FS::PathCombine(instance->gameRoot(), def.executable));
    const bool gog = QFileInfo::exists(FS::PathCombine(instance->gameRoot(), "Galaxy64.dll"));

    auto ask = [&](const QByteArray& query) -> Result<QByteArray> {
        auto job = makeShared<NetJob>(QObject::tr("Finding the script extender for %1").arg(def.name), APPLICATION->network());
        job->setAskRetry(false);
        auto [request, data] = Net::ApiRequest::makeByteArray(Nexus::graphqlUrl(), query);
        job->addNetAction(request);
        ProgressDialog progress(parent);
        progress.execWithTask(job.get());
        if (!job->wasSuccessful()) {
            return std::unexpected(job->failReason());
        }
        return *data;
    };
    // the file list is asked for by the game's numeric id
    auto games = ask(Nexus::gameQuery(def.domain)).and_then(Nexus::parseGames);
    if (!games || games->isEmpty()) {
        showError(parent, title, games ? QObject::tr("Nexus Mods does not know the game %1.").arg(def.domain) : games.error());
        return false;
    }
    auto files = ask(Nexus::modFilesQuery(games->first().id, modId)).and_then(Nexus::parseGraphqlModFiles);
    if (!files) {
        showError(parent, title, files.error());
        return false;
    }
    bool matched = false;
    const auto file = Nexus::scriptExtenderFile(*files, gameVersion, gog, &matched);
    if (file.fileId <= 0) {
        showError(parent, title, QObject::tr("Nexus Mods has no script extender files for %1.").arg(def.name));
        return false;
    }
    const auto builtFor = QTextDocumentFragment::fromHtml(Nexus::bbcodeToHtml(file.description)).toPlainText().simplified();
    if (!matched) {
        const auto answer = QMessageBox::question(
            parent, title,
            QObject::tr("No script extender build names your game version (%1). The newest one is %2 %3%4.\n\nInstall it anyway?")
                .arg(gameVersion.isEmpty() ? QObject::tr("unknown") : gameVersion, file.name, file.version,
                     builtFor.isEmpty() ? QString() : QString(" (%1)").arg(builtFor)));
        if (answer != QMessageBox::Yes) {
            return false;
        }
    }
    if (APPLICATION->accounts()->isPremium()) {
        return installNexusFile(instance, def.domain, modId, file.fileId, parent);
    }
    downloadFromWebsite(instance, def.domain, { NexusFile{ .modId = modId, .modName = file.name, .modVersion = file.version, .file = file } }, parent);
    return true;
}

void downloadFromWebsite(GameInstance* instance, const QString& game, const QList<NexusFile>& files, QWidget* parent)
{
    QList<NexusDownloadQueue::Request> requests;
    for (const auto& entry : files) {
        const auto& file = entry.file;
        NexusDownloadQueue::Request request;
        request.instance = instance;
        request.page = file.fileId > 0 ? Nexus::fileDownloadPageUrl(game, entry.modId, file.fileId) : Nexus::modFilesPageUrl(game, entry.modId);
        const auto name = entry.modName.isEmpty() ? QObject::tr("Nexus Mods file %1").arg(file.fileId) : entry.modName;
        request.title = file.fileName.isEmpty() ? name : QString("%1 (%2)").arg(name, file.fileName);
        request.downloadDir = instance->downloadsDir();
        request.fileName = file.fileName;
        request.size = file.size;
        request.metadata.nexusGame = game;
        request.metadata.nexusModId = entry.modId;
        request.metadata.nexusFileId = file.fileId;
        request.metadata.version = file.version.isEmpty() ? entry.modVersion : file.version;
        request.metadata.latestVersion = entry.modVersion;
        const bool mainFile = file.name.isEmpty() || file.name == entry.modName || file.category.compare("MAIN", Qt::CaseInsensitive) == 0;
        request.metadata.name = mainFile ? entry.modName : QString("%1 - %2").arg(entry.modName, file.name);
        requests << request;
    }
    APPLICATION->nexusDownloads()->add(requests, parent);
}

bool deploy(GameInstance* instance, QWidget* parent)
{
    // the progress window keeps events going, so a deploy can be asked for while one runs (the Mods page hears about
    // the change an install makes). Two at once would both place the same files; the running one goes again instead.
    static QSet<GameInstance*> running;
    static QSet<GameInstance*> again;
    if (running.contains(instance)) {
        again.insert(instance);
        return true;
    }
    if (instance->isRunning()) {
        deployAfterExit(instance, {}, parent);
        return false;
    }
    running.insert(instance);
    bool deployed = true;
    do {
        again.remove(instance);
        auto task = instance->createDeployTask();
        deployed = runTask(task.get(), parent, QObject::tr("Deploying mods failed"));
        showDeployWarnings(task.get(), parent);
    } while (deployed && again.contains(instance));
    running.remove(instance);
    again.remove(instance);
    if (deployed) {
        setUpNemesis(instance, parent);
    }
    return deployed;
}

bool purge(GameInstance* instance, QWidget* parent)
{
    if (refuseWhileRunning(instance, parent)) {
        return false;
    }
    auto task = instance->createPurgeTask();
    const bool purged = runTask(task.get(), parent, QObject::tr("Removing deployed mods failed"));
    showDeployWarnings(task.get(), parent);
    return purged;
}

namespace {

/// LOOT's masterlist for @p repository: downloaded when possible and kept, otherwise the copy kept from last time
Result<Loot::Masterlist> loadMasterlist(const QString& repository, QStringList& notes, QWidget* parent)
{
    const auto folder = FS::PathCombine(APPLICATION->dataRoot(), "loot");
    const auto masterlistPath = FS::PathCombine(folder, repository, "masterlist.yaml");
    const auto preludePath = FS::PathCombine(folder, "prelude.yaml");

    auto job = makeShared<NetJob>(QObject::tr("Downloading LOOT's masterlist"), APPLICATION->network());
    job->setAskRetry(false);
    auto [masterlistRequest, masterlistData] = Net::Request::makeByteArray(AutoSort::masterlistUrl(repository));
    auto [preludeRequest, preludeData] = Net::Request::makeByteArray(AutoSort::preludeUrl());
    job->addNetAction(masterlistRequest);
    job->addNetAction(preludeRequest);
    ProgressDialog progress(parent);
    progress.execWithTask(job.get());

    QString problem;
    if (masterlistRequest->wasSuccessful() && preludeRequest->wasSuccessful()) {
        auto loaded = Loot::Masterlist::load(QString::fromUtf8(*masterlistData), QString::fromUtf8(*preludeData));
        if (loaded) {
            for (const auto& [path, data] : { std::pair{ masterlistPath, masterlistData }, std::pair{ preludePath, preludeData } }) {
                if (auto saved = FS::write(path, *data); !saved) {
                    qWarning() << "Could not keep LOOT's masterlist:" << saved.error();
                }
            }
            return loaded;
        }
        problem = loaded.error();
    } else {
        problem = masterlistRequest->wasSuccessful() ? preludeRequest->failReason() : masterlistRequest->failReason();
    }
    const auto masterlist = FS::read(masterlistPath);
    const auto prelude = FS::read(preludePath);
    if (!masterlist || !prelude) {
        return std::unexpected(QObject::tr("Could not download LOOT's masterlist, which auto sort needs the first time: %1").arg(problem));
    }
    TRY_INTO(auto loaded, Loot::Masterlist::load(QString::fromUtf8(*masterlist), QString::fromUtf8(*prelude)));
    notes << QObject::tr("LOOT's masterlist could not be updated (%1), so the copy from %2 was used.")
                 .arg(problem, QLocale().toString(QFileInfo(masterlistPath).lastModified(), QLocale::ShortFormat));
    return loaded;
}

QStringList pluginNames(const PluginList* plugins)
{
    QStringList names;
    for (const auto& plugin : plugins->plugins()) {
        names << plugin.name;
    }
    return names;
}

}  // namespace

bool autoSort(GameInstance* instance, QWidget* parent)
{
    if (refuseWhileRunning(instance, parent)) {
        return false;
    }
    const auto title = QObject::tr("Auto sort");
    const auto definition = instance->definition();
    auto* mods = instance->modList();
    auto* plugins = instance->pluginList();
    QStringList notes;

    // plugins with LOOT's rules, and which mods have files in common, both on a worker thread
    QStringList pluginsBefore;
    std::optional<Loot::GameType> lootGame;
    std::optional<Loot::Masterlist> masterlist;
    if (definition.hasPlugins()) {
        if (auto refreshed = plugins->refresh(); !refreshed) {
            showError(parent, title, refreshed.error());
            return false;
        }
        pluginsBefore = pluginNames(plugins);
        lootGame = AutoSort::gameType(definition.domain);
        if (!lootGame) {
            notes << QObject::tr("LOOT has no masterlist for %1, so the plugins kept their order.").arg(definition.name);
        } else {
            auto loaded = loadMasterlist(AutoSort::masterlistRepository(*lootGame), notes, parent);
            if (!loaded) {
                showError(parent, title, loaded.error());
                return false;
            }
            masterlist = std::move(*loaded);
        }
    }
    QList<AutoSort::PluginState> current;
    for (const auto& plugin : plugins->plugins()) {
        current << AutoSort::PluginState{ plugin.name, plugin.active };
    }
    QStringList enabledIds;
    for (const auto& mod : mods->enabledMods()) {
        enabledIds << mod.id;
    }
    QList<std::pair<QString, QString>> conflicts;
    AutoSort::SortTask task([&conflicts, modsDir = mods->modsDir(), enabledIds, game = lootGame, dataDir = instance->dataDir(),
                             earlyLoaders = lootGame ? AutoSort::earlyLoadingPlugins(*lootGame, instance->gameRoot(), instance->myGamesDir())
                                                     : QStringList(),
                             current, list = std::move(masterlist)]() -> Result<QStringList> {
        conflicts = ModList::sharingPairs(modsDir, enabledIds);
        if (!game || !list) {
            return QStringList();
        }
        return AutoSort::sortPlugins(*game, dataDir, earlyLoaders, current, *list);
    });
    if (!runTask(&task, parent, QObject::tr("Auto sort could not sort the plugins"))) {
        return false;
    }
    const QStringList sortedPlugins = task.result();

    // mods: which ones need which, from Nexus Mods
    AutoSort::ModRules rules;
    QHash<QString, QStringList> installed;  // "game/mod id" -> installed mods of that Nexus Mods mod
    QMap<QString, QList<int>> byGame;
    for (const auto& mod : mods->mods()) {
        if (mod.nexusModId > 0) {
            installed[mod.nexusGame + '/' + QString::number(mod.nexusModId)] << mod.id;
            if (!byGame[mod.nexusGame].contains(mod.nexusModId)) {
                byGame[mod.nexusGame] << mod.nexusModId;
            }
        }
    }
    for (auto it = byGame.cbegin(); it != byGame.cend(); ++it) {
        const auto details = fetchModDetails(it.key(), it.value(), parent);
        if (!details) {
            notes << QObject::tr("Nexus Mods could not say which mods need which (%1), so plugins and the current order decided the mods.")
                         .arg(details.error());
            continue;
        }
        for (const auto& mod : *details) {
            for (const auto& requirement : mod.requirements) {
                if (requirement.modId <= 0 || (requirement.gameId != 0 && requirement.gameId != mod.gameId)) {
                    continue;
                }
                for (const auto& needer : installed.value(it.key() + '/' + QString::number(mod.modId))) {
                    for (const auto& needed : installed.value(it.key() + '/' + QString::number(requirement.modId))) {
                        if (needer != needed) {
                            rules.requirements[needer].insert(needed);
                        }
                    }
                }
            }
        }
    }
    // and where their plugins load
    const auto pluginOrder = sortedPlugins.isEmpty() ? pluginsBefore : sortedPlugins;
    QHash<QString, int> rank;
    for (int i = 0; i < pluginOrder.size(); i++) {
        rank.insert(pluginOrder[i].toLower(), i);
    }
    if (const auto manifest = DeploymentManifest::load(instance->deploymentManifestPath())) {
        for (const auto& file : manifest->files) {
            const auto name = file.path.mid(file.path.lastIndexOf('/') + 1).toLower();
            if (const auto found = rank.constFind(name); found != rank.constEnd()) {
                rules.pluginRank[file.mod] = qMax(rules.pluginRank.value(file.mod, -1), *found);
            }
        }
    }
    QStringList modsBefore;
    for (const auto& mod : mods->mods()) {
        modsBefore << mod.id;
    }
    const auto modsAfter = AutoSort::sortMods(modsBefore, conflicts, rules);

    if (!sortedPlugins.isEmpty()) {
        plugins->setOrder(sortedPlugins);
    }
    mods->setOrder(modsAfter);

    const auto pluginsAfter = pluginNames(plugins);
    const int pluginsMoved = definition.hasPlugins() ? AutoSort::movedCount(pluginsBefore, pluginsAfter) : 0;
    const int modsMoved = AutoSort::movedCount(modsBefore, modsAfter);
    QStringList lines;
    if (pluginsMoved == 0 && modsMoved == 0) {
        lines << QObject::tr("Everything was already in order.");
    } else {
        if (!sortedPlugins.isEmpty()) {
            lines << QObject::tr("Plugins: %1 of %2 moved, sorted with LOOT's rules and masterlist.").arg(pluginsMoved).arg(pluginsAfter.size());
        }
        lines << QObject::tr("Mods: %1 of %2 moved. Of two mods with the same files, the one that needs the other wins, then the one "
                             "whose plugin loads later.")
                     .arg(modsMoved)
                     .arg(modsAfter.size());
    }
    lines << notes;
    QMessageBox box(QMessageBox::Information, title, lines.join("\n\n"), QMessageBox::Ok, parent);
    QAbstractButton* undo = pluginsMoved + modsMoved > 0 ? box.addButton(QObject::tr("&Undo"), QMessageBox::ResetRole) : nullptr;
    box.exec();
    if (undo && box.clickedButton() == undo) {
        if (!sortedPlugins.isEmpty()) {
            plugins->setOrder(pluginsBefore);
        }
        mods->setOrder(modsBefore);
    }
    return true;
}

namespace {

/// Runs work on a worker thread as a task, so a progress dialog shows while it runs. The work is given a function to
/// report how much of how much it has done, from any thread.
template <typename T>
class WorkerTask : public Task {
   public:
    using Progress = std::function<void(int, int)>;
    WorkerTask(QString status, std::function<T(const Progress&)> work) : m_status(std::move(status)), m_work(std::move(work)) {}
    const T& result() const { return m_result; }

   protected:
    void executeTask() override
    {
        setStatus(m_status);
        connect(&m_watcher, &QFutureWatcher<T>::finished, this, [this] {
            m_result = m_watcher.result();
            emitSucceeded();
        });
        Progress progress = [this](int done, int total) {
            QMetaObject::invokeMethod(this, [this, done, total] { setProgress(done, total); }, Qt::QueuedConnection);
        };
        m_watcher.setFuture(QtConcurrent::run([work = m_work, progress] { return work(progress); }));
    }

   private:
    QString m_status;
    std::function<T(const Progress&)> m_work;
    QFutureWatcher<T> m_watcher;
    T m_result;
};

/**
 * Gets Nemesis ready after a deploy, when the instance has it (see Nemesis::prepare): its settings and the patches mods
 * brought ticked, its engine updated when its patches changed, and a Launch menu entry that runs it in the instance.
 *
 * Nemesis 0.84 (the current release) fails on its own "-stage" option, so what it generates goes where it always does,
 * into the instance's Data folder, as files of their own.
 */
void setUpNemesis(GameInstance* instance, QWidget* parent)
{
    const auto dataDir = instance->dataDir();
    const auto program = FS::PathCombine(dataDir, Nemesis::k_engine);
    if (!QFileInfo::exists(program)) {
        return;
    }
    const auto title = QObject::tr("Nemesis");
    const auto dataPath = instance->definition().dataPath;
    auto* mods = instance->modList();
    // the patches Nemesis comes with are the ones in its own mod; the rest came with other mods, which want them used
    QSet<QString> own;
    for (const auto& mod : mods->enabledMods()) {
        const auto data = dataPath.isEmpty() ? mods->modDir(mod.id) : FS::PathCombine(mods->modDir(mod.id), dataPath);
        if (QFileInfo::exists(FS::PathCombine(data, Nemesis::k_engine))) {
            own = Nemesis::patchCodes(FS::PathCombine(data, "Nemesis_Engine", "mod"));
        }
    }
    const auto setup = Nemesis::prepare(dataDir, own);
    if (!setup) {
        showError(parent, title, setup.error());
        return;
    }

    const auto relative = dataPath.isEmpty() ? Nemesis::k_engine : dataPath + '/' + Nemesis::k_engine;
    auto tools = instance->customExecutables();
    if (std::none_of(tools.begin(), tools.end(), [&relative](const GameExecutable& tool) { return tool.path.compare(relative, Qt::CaseInsensitive) == 0; })) {
        tools << GameExecutable{ .name = "Nemesis", .path = relative };
        instance->setCustomExecutables(tools);
    }

    if (setup->engineOutdated) {
        // its "Update Engine": the patches are worked into the engine before it can generate anything
        WorkerTask<QString> update(QObject::tr("Updating the Nemesis engine for its patches"), [program](const auto&) -> QString {
            QProcess process;
            process.setWorkingDirectory(QFileInfo(program).absolutePath());
            process.start(program, { "-update" });
            if (!process.waitForStarted()) {
                return process.errorString();
            }
            if (!process.waitForFinished(10 * 60 * 1000)) {
                process.kill();
                process.waitForFinished();
                return QObject::tr("it did not finish within 10 minutes");
            }
            return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 ? QString()
                                                                                           : QObject::tr("it stopped with code %1").arg(process.exitCode());
        });
        runTask(&update, parent, title);
        if (!update.result().isEmpty()) {
            showError(parent, title,
                      QObject::tr("Updating the Nemesis engine failed: %1. Its log: %2")
                          .arg(update.result(), QDir::toNativeSeparators(FS::PathCombine(QFileInfo(program).absolutePath(), "UpdateLog.txt"))));
        } else if (auto saved = Nemesis::engineUpdated(dataDir); !saved) {
            showError(parent, title, saved.error());
        }
    }
}

FomodPatches::Mod patchMod(GameInstance* instance, const ModEntry& entry)
{
    const auto dir = instance->modList()->modDir(entry.id);
    const auto dataPath = instance->definition().dataPath;
    return FomodPatches::Mod{ .id = entry.id,
                              .name = entry.name,
                              .archive = entry.archive.isEmpty() ? QString() : FS::PathCombine(instance->downloadsDir(), entry.archive),
                              .folder = dataPath.isEmpty() ? dir : FS::PathCombine(dir, dataPath),
                              .choices = entry.choices };
}

void addPatches(GameInstance* instance, const QList<FomodPatches::Patch>& patches, QWidget* parent)
{
    const auto title = QObject::tr("Add patches");
    if (refuseWhileRunning(instance, parent)) {
        return;
    }
    auto* mods = instance->modList();
    QStringList order;
    QHash<QString, QList<FomodPatches::Patch>> byMod;
    for (const auto& patch : patches) {
        if (!byMod.contains(patch.modId)) {
            order << patch.modId;
        }
        byMod[patch.modId] << patch;
    }
    const auto states = fileStates(instance);
    QSet<QString> plugins;
    QStringList errors;
    int added = 0;
    for (const auto& id : order) {
        const int row = mods->indexOf(id);
        if (row < 0) {
            continue;
        }
        auto entry = mods->mods()[row];
        const auto modPatches = byMod.value(id);
        WorkerTask<Result<FomodPatches::Installed>> adding(
            QObject::tr("Adding patches to %1").arg(entry.name),
            [mod = patchMod(instance, entry), modPatches, states, cache = patchCache()](const auto&) {
                return FomodPatches::install(mod, modPatches, states, cache);
            });
        runTask(&adding, parent, title);
        const auto& result = adding.result();
        if (!result) {
            errors << QObject::tr("%1: %2").arg(entry.name, result.error());
            continue;
        }
        entry.choices = result->choices;
        entry.stamp = QDateTime::currentMSecsSinceEpoch();  // the deployer links the changed files anew
        mods->upsert(entry);
        plugins.unite(QSet<QString>(result->plugins.begin(), result->plugins.end()));
        added += static_cast<int>(modPatches.size());
    }
    if (added > 0) {
        instance->saveNow();
        if (deploy(instance, parent)) {
            activatePlugins(instance, plugins, parent);
        }
    }
    if (!errors.isEmpty()) {
        showError(parent, title, QObject::tr("These patches were not added:\n\n%1").arg(errors.join("\n")));
    } else if (added > 0) {
        QMessageBox::information(parent, title,
                                 QObject::tr("Added %n patch(es).", "", added) +
                                     (plugins.isEmpty() ? QString()
                                                        : " " + QObject::tr("Their plugins are on; Auto Sort puts them in their place in the load order.")));
    }
}

}  // namespace

void checkPatches(GameInstance* instance, QWidget* parent)
{
    const auto title = QObject::tr("Check patches");
    if (instance->definition().hasPlugins()) {
        if (auto refreshed = instance->pluginList()->refresh(); !refreshed) {
            showError(parent, title, refreshed.error());
            return;
        }
    }
    QList<FomodPatches::Mod> mods;
    for (const auto& entry : instance->modList()->mods()) {
        if (entry.enabled) {
            mods << patchMod(instance, entry);
        }
    }
    WorkerTask<FomodPatches::Check> reading(QObject::tr("Reading the installers of %n mod(s)", "", static_cast<int>(mods.size())),
                                            [mods, states = fileStates(instance), cache = patchCache()](const auto& progress) {
                                                return FomodPatches::find(mods, states, cache, progress);
                                            });
    if (!runTask(&reading, parent, title)) {
        return;
    }
    const auto check = reading.result();

    QStringList notes;
    if (!check.unchecked.isEmpty()) {
        notes << QObject::tr("Not checked, because their archive is not in the downloads folder: %1").arg(check.unchecked.join(", "));
    }
    if (!check.errors.isEmpty()) {
        notes << QObject::tr("Could not be read:\n%1").arg(check.errors.join("\n"));
    }
    if (check.patches.isEmpty()) {
        CustomMessageBox::selectable(
            parent, title,
            (QStringList{ QObject::tr("No missing patches: the installers of your mods recommend nothing more for the mods you have.") } + notes)
                .join("\n\n"),
            QMessageBox::Information)
            ->exec();
        return;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto* layout = new QVBoxLayout(&dialog);
    auto* intro = new QLabel(QObject::tr("The installers of these mods recommend these options because of other mods you have, and they are "
                                         "not installed. The checked ones are added to their mods:"));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* list = new QListWidget;
    for (int i = 0; i < check.patches.size(); i++) {
        const auto& patch = check.patches[i];
        auto text = QObject::tr("%1: %2").arg(patch.modName, patch.title.isEmpty() ? QObject::tr("files it adds for mods you have") : patch.title);
        if (!patch.files.isEmpty()) {
            text += QString(" (%1)").arg(patch.files.join(", "));
        }
        if (!patch.replaces.isEmpty()) {
            text += " " + QObject::tr("instead of %1").arg(patch.replaces.join(", "));
        }
        auto* item = new QListWidgetItem(text, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, i);
    }
    layout->addWidget(list);
    if (!notes.isEmpty()) {
        auto* label = new QLabel(notes.join("\n\n"));
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(label);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* add = buttons->addButton(QObject::tr("&Add Checked"), QDialogButtonBox::AcceptRole);
    QObject::connect(list, &QListWidget::itemChanged, add, [list, add] {
        bool any = false;
        for (int i = 0; i < list->count(); i++) {
            any = any || list->item(i)->checkState() == Qt::Checked;
        }
        add->setEnabled(any);
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(720, 420);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QList<FomodPatches::Patch> chosen;
    for (int i = 0; i < list->count(); i++) {
        if (list->item(i)->checkState() == Qt::Checked) {
            chosen << check.patches[list->item(i)->data(Qt::UserRole).toInt()];
        }
    }
    addPatches(instance, chosen, parent);
}

}  // namespace ModInstallFlow
