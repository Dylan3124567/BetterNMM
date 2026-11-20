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

#include "SelfUpdate.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QSettings>

#include "Application.h"
#include "BuildConfig.h"
#include "DesktopServices.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "game/GameInstance.h"
#include "net/NetJob.h"
#include "ui/dialogs/ProgressDialog.h"

namespace SelfUpdate {

Result<Release> parseRelease(const QByteArray& json)
{
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(json, &error);
    if (!doc.isObject()) {
        return std::unexpected(QObject::tr("GitHub's answer is not readable: %1").arg(error.errorString()));
    }
    const auto obj = doc.object();
    auto tag = obj.value("tag_name").toString();
    if (tag.startsWith('v', Qt::CaseInsensitive)) {
        tag.remove(0, 1);
    }
    Release release;
    release.version = QVersionNumber::fromString(tag);
    if (release.version.isNull()) {
        return std::unexpected(QObject::tr("GitHub's newest release has no version number (\"%1\").").arg(obj.value("tag_name").toString()));
    }
    release.page = QUrl(obj.value("html_url").toString());
    release.notes = obj.value("body").toString();
    for (const auto& value : obj.value("assets").toArray()) {
        const auto asset = value.toObject();
        const auto name = asset.value("name").toString();
        if (name.contains("setup", Qt::CaseInsensitive) && name.endsWith(".exe", Qt::CaseInsensitive)) {
            release.installer = QUrl(asset.value("browser_download_url").toString());
            release.size = asset.value("size").toInteger();
        }
    }
    return release;
}

namespace {

/// Whether this copy is the one the setup program installed; another copy (a build folder, a copied folder) is left alone
bool installedByInstaller()
{
#ifdef Q_OS_WIN
    for (const auto* root : { "HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE" }) {
        const QSettings key(QString("%1\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BetterNMM").arg(root), QSettings::NativeFormat);
        const auto location = key.value("InstallLocation").toString();
        if (!location.isEmpty() && QDir(location) == QDir(QCoreApplication::applicationDirPath())) {
            return true;
        }
    }
#endif
    return false;
}

bool gameRunning()
{
    auto* instances = APPLICATION->instances();
    for (int i = 0; i < instances->count(); i++) {
        if (instances->at(i)->isRunning()) {
            return true;
        }
    }
    return false;
}

void install(QWidget* parent, const Release& release)
{
    const auto title = QObject::tr("Update %1").arg(BuildConfig.LAUNCHER_DISPLAYNAME);
    // BetterNMM quits for the update; a running game still needs it to put the mod files back afterwards
    if (gameRunning()) {
        QMessageBox::information(parent, title, QObject::tr("Close the game first, then check for updates again (Help menu)."));
        return;
    }
    const auto target = FS::PathCombine(QDir::tempPath(), release.installer.fileName());
    auto job = makeShared<NetJob>(QObject::tr("Downloading %1 %2").arg(BuildConfig.LAUNCHER_DISPLAYNAME, release.version.toString()),
                                  APPLICATION->network());
    job->setAskRetry(false);
    job->addNetAction(Net::Request::makeFile(release.installer, target));
    ProgressDialog progress(parent);
    progress.execWithTask(job.get());
    if (!job->wasSuccessful() || (release.size > 0 && QFileInfo(target).size() != release.size)) {
        QFile::remove(target);
        if (job->getState() != Task::State::AbortedByUser) {
            QMessageBox::warning(parent, title,
                                 QObject::tr("The download failed: %1").arg(job->wasSuccessful() ? QObject::tr("the file is incomplete") : job->failReason()));
        }
        return;
    }
    // the setup program waits for this process to end, replaces the installed files and starts the new version
    const QStringList arguments{ "--silent", "--launch", "1", "--parent", QString::number(QCoreApplication::applicationPid()) };
    if (!QProcess::startDetached(target, arguments)) {
        QMessageBox::warning(parent, title, QObject::tr("Could not start %1.").arg(QDir::toNativeSeparators(target)));
        return;
    }
    QCoreApplication::quit();
}

void offer(QWidget* parent, const Release& release)
{
    const bool installable = !release.installer.isEmpty() && installedByInstaller();
    QMessageBox box(QMessageBox::Information, QObject::tr("Update %1").arg(BuildConfig.LAUNCHER_DISPLAYNAME),
                    QObject::tr("%1 %2 is out; you have %3.")
                            .arg(BuildConfig.LAUNCHER_DISPLAYNAME, release.version.toString(), BuildConfig.printableVersionString()) +
                        "\n\n" +
                        (installable ? QObject::tr("Update now? %1 closes, updates and starts again.").arg(BuildConfig.LAUNCHER_DISPLAYNAME)
                                     : QObject::tr("This copy was not installed with the setup program, so it can not update itself. "
                                                   "Download the new version from its release page.")),
                    QMessageBox::NoButton, parent);
    if (!release.notes.isEmpty()) {
        box.setDetailedText(release.notes);
    }
    auto* accept = box.addButton(installable ? QObject::tr("Update now") : QObject::tr("Open release page"), QMessageBox::AcceptRole);
    box.addButton(QObject::tr("Not now"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != accept) {
        return;
    }
    if (installable) {
        install(parent, release);
    } else {
        DesktopServices::openUrl(release.page);
    }
}

}  // namespace

void check(QWidget* parent, bool quiet)
{
    const auto title = QObject::tr("Check for updates");
    if (BuildConfig.RELEASES_API_URL.isEmpty()) {
        if (!quiet) {
            QMessageBox::information(parent, title, QObject::tr("This build of %1 has no update source.").arg(BuildConfig.LAUNCHER_DISPLAYNAME));
        }
        return;
    }
    QNetworkRequest request{ QUrl(BuildConfig.RELEASES_API_URL) };
    request.setHeader(QNetworkRequest::UserAgentHeader, APPLICATION->getUserAgent());
    request.setRawHeader("Accept", "application/vnd.github+json");
    auto* reply = APPLICATION->network()->get(request);
    if (!quiet) {
        QApplication::setOverrideCursor(Qt::BusyCursor);
    }
    QPointer<QWidget> owner(parent);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, owner, quiet, title] {
        reply->deleteLater();
        if (!quiet) {
            QApplication::restoreOverrideCursor();
        }
        if (!owner) {
            return;
        }
        auto release = reply->error() == QNetworkReply::NoError ? parseRelease(reply->readAll())
                                                                : Result<Release>(std::unexpected(reply->errorString()));
        if (!release) {
            if (!quiet) {
                QMessageBox::warning(owner, title, QObject::tr("Could not ask GitHub for the newest version: %1").arg(release.error()));
            }
            return;
        }
        if (release->version <= QVersionNumber(BuildConfig.VERSION_MAJOR, BuildConfig.VERSION_MINOR, BuildConfig.VERSION_PATCH)) {
            if (!quiet) {
                QMessageBox::information(owner, title,
                                         QObject::tr("%1 %2 is the newest version.").arg(BuildConfig.LAUNCHER_DISPLAYNAME, BuildConfig.printableVersionString()));
            }
            return;
        }
        offer(owner, *release);
    });
}

}  // namespace SelfUpdate
