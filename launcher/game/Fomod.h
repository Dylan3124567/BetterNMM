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

#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <functional>
#include <memory>

#include "Result.h"

/// The XML ("fomod/ModuleConfig.xml") installer format most Bethesda game mods on Nexus Mods use
namespace Fomod {

enum class GroupType { SelectAny, SelectExactlyOne, SelectAtMostOne, SelectAtLeastOne, SelectAll };
enum class PluginType { Required, Optional, Recommended, NotUsable, CouldBeUsable };
enum class FileState { Missing, Inactive, Active };

struct FileItem {
    QString source;       ///< relative to the folder holding "fomod", '/' separated
    QString destination;  ///< relative to the mod's install folder, '/' separated
    bool isFolder = false;
    int priority = 0;
    bool alwaysInstall = false;
    bool installIfUsable = false;
};

struct Condition {
    enum class Kind { Flag, File, Group, Always };
    Kind kind = Kind::Always;
    QString name;   ///< flag or file name
    QString value;  ///< flag value
    FileState fileState = FileState::Active;
    bool orOperator = false;
    QList<Condition> children;
};

struct TypePattern {
    Condition condition;
    PluginType type = PluginType::Optional;
};

struct Plugin {
    QString name;
    QString description;
    QString image;
    QList<FileItem> files;
    QList<QPair<QString, QString>> flags;
    PluginType defaultType = PluginType::Optional;
    QList<TypePattern> patterns;
};

struct Group {
    QString name;
    GroupType type = GroupType::SelectAny;
    QList<Plugin> plugins;
};

struct Step {
    QString name;
    bool hasVisibility = false;
    Condition visible;
    QList<Group> groups;
};

struct ConditionalInstall {
    Condition condition;
    QList<FileItem> files;
};

struct Module {
    QString name;
    QString image;
    QList<FileItem> requiredFiles;
    QList<Step> steps;
    QList<ConditionalInstall> conditionalInstalls;
};

struct Info {
    QString name;
    QString version;
    QString author;
};

/// Answers for conditions: the state of a plugin file in the game, and the flags chosen so far
struct Context {
    std::function<FileState(const QString&)> fileState;
    QHash<QString, QString> flags;
};

Result<Module> parse(const QByteArray& xml);
Info parseInfo(const QByteArray& xml);

bool evaluate(const Condition& condition, const Context& context);
PluginType pluginType(const Plugin& plugin, const Context& context);
bool stepVisible(const Step& step, const Context& context);

/// A plugin chosen in the wizard
struct Selection {
    int step;
    int group;
    int plugin;
};

/// Recomputes the flags from the selections of visible steps
QHash<QString, QString> flagsFor(const Module& module, const QList<Selection>& selections, const Context& base);

/// Every file to install, in the order they must be copied (later overrides earlier)
QList<FileItem> filesToInstall(const Module& module, const QList<Selection>& selections, const Context& context);

/// The selections the installer makes without asking: required, recommended and select-all plugins, first option for
/// "exactly one" groups. Used as the wizard's starting point.
QList<Selection> defaultSelections(const Module& module, int stepIndex, const Context& context);

}  // namespace Fomod
