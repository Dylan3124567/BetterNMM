// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (c) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2023 TheKodeToad <TheKodeToad@proton.me>
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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#pragma once
#include <QList>
#include <QString>

/**
 * \brief The Config class holds all the build-time information passed from the build system.
 */
class Config {
   public:
    Config();
    QString LAUNCHER_NAME;
    QString LAUNCHER_APP_BINARY_NAME;
    QString LAUNCHER_DISPLAYNAME;
    QString LAUNCHER_COPYRIGHT;
    QString LAUNCHER_DOMAIN;
    QString LAUNCHER_CONFIGFILE;
    QString LAUNCHER_GIT;
    QString LAUNCHER_APPID;
    QString LAUNCHER_SVGFILENAME;
    QString LAUNCHER_ENVNAME;

    /// The major version number.
    int VERSION_MAJOR;
    /// The minor version number.
    int VERSION_MINOR;
    /// The patch version number.
    int VERSION_PATCH;

    /// The version channel (branch name or short commit of this build)
    QString VERSION_CHANNEL;

    /// A short string identifying this build's platform or distribution.
    QString BUILD_PLATFORM;

    /// A string containing the build timestamp
    QString BUILD_DATE;

    /// A string identifying the compiler use to build
    QString COMPILER_NAME;

    /// A string identifying the compiler version used to build
    QString COMPILER_VERSION;

    /// A string identifying the compiler target system os
    QString COMPILER_TARGET_SYSTEM;

    /// A String identifying the compiler target system version
    QString COMPILER_TARGET_SYSTEM_VERSION;

    /// A String identifying the compiler target processor
    QString COMPILER_TARGET_SYSTEM_PROCESSOR;

    /// User-Agent to use.
    QString USER_AGENT;

    /// The git commit hash of this build
    QString GIT_COMMIT;

    /// The git tag of this build
    QString GIT_TAG;

    /// The git refspec of this build
    QString GIT_REFSPEC;

    /// URL (with arg %1 to be substituted with page-id) that gets opened when the user requests help
    QString HELP_URL;

    QString BUG_TRACKER_URL;

    /// Nexus Mods REST API base (v1), ends with a slash
    QString NEXUS_API_URL;
    /// Nexus Mods GraphQL endpoint (v2)
    QString NEXUS_GRAPHQL_URL;
    /// Nexus Mods website, ends with a slash
    QString NEXUS_SITE_URL;
    /// GitHub API URL of the newest release, for updating; empty when this build does not update itself
    QString RELEASES_API_URL;
    /// Where a user creates a personal API key
    QString NEXUS_API_KEY_PAGE = "https://www.nexusmods.com/settings/api-keys";

    QString versionString() const;
    /**
     * \brief Converts the Version to a string.
     * \return The version number in string format (major.minor.revision.build).
     */
    QString printableVersionString() const;

    /**
     * \brief Compiler ID String
     * \return a string of the form "Name - Version"  of just "Name" if the version is empty
     */
    QString compilerID() const;

    /**
     * \brief System ID String
     * \return a string of the form "OS Verison Processor"
     */
    QString systemID() const;
};

extern const Config BuildConfig;
