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

#include "NxmLink.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QSettings>
#include <QUrlQuery>

#include "BuildConfig.h"

Result<NxmLink> NxmLink::parse(const QUrl& url)
{
    if (url.scheme().compare("nxm", Qt::CaseInsensitive) != 0) {
        return std::unexpected(QObject::tr("%1 is not a Nexus Mods download link").arg(url.toString()));
    }
    const auto parts = url.path().split('/', Qt::SkipEmptyParts);
    if (parts.size() >= 2 && parts[0] == "collections") {
        return std::unexpected(QObject::tr("Collections are not supported. Download the mods of the collection one by one."));
    }
    if (parts.size() != 4 || parts[0] != "mods" || parts[2] != "files") {
        return std::unexpected(QObject::tr("%1 is not a Nexus Mods file link").arg(url.toString()));
    }
    NxmLink link;
    link.game = url.host().toLower();
    bool modOk = false;
    bool fileOk = false;
    link.modId = parts[1].toInt(&modOk);
    link.fileId = parts[3].toInt(&fileOk);
    if (link.game.isEmpty() || !modOk || !fileOk || link.modId <= 0 || link.fileId <= 0) {
        return std::unexpected(QObject::tr("%1 is not a Nexus Mods file link").arg(url.toString()));
    }
    const QUrlQuery query(url);
    link.key = query.queryItemValue("key");
    link.expires = query.queryItemValue("expires").toLongLong();
    link.userId = query.queryItemValue("user_id").toInt();
    return link;
}

bool NxmLink::expired() const
{
    return expires > 0 && QDateTime::currentSecsSinceEpoch() >= expires;
}

namespace Nxm {

static QString command()
{
    return QString("\"%1\" \"%2\"").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()), "%1");
}

bool isHandler()
{
#ifdef Q_OS_WIN
    QSettings classes(R"(HKEY_CURRENT_USER\Software\Classes\nxm\shell\open\command)", QSettings::NativeFormat);
    return classes.value(".").toString().compare(command(), Qt::CaseInsensitive) == 0;
#else
    return false;
#endif
}

Result<> registerHandler()
{
#ifdef Q_OS_WIN
    QSettings classes(R"(HKEY_CURRENT_USER\Software\Classes\nxm)", QSettings::NativeFormat);
    classes.setValue(".", QString("URL:NXM Protocol"));
    classes.setValue("URL Protocol", QString());
    classes.setValue("DefaultIcon/.", QString("\"%1\",0").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
    classes.setValue("shell/open/command/.", command());
    classes.sync();
    if (classes.status() != QSettings::NoError || !isHandler()) {
        return std::unexpected(QObject::tr("Windows did not accept the nxm:// link registration."));
    }
    return {};
#else
    return std::unexpected(QObject::tr("Registering nxm:// links is only supported on Windows."));
#endif
}

}  // namespace Nxm
