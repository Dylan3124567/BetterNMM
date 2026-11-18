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

#include "Fomod.h"

#include <QDomDocument>
#include <QDomElement>
#include <QObject>

#include <algorithm>

namespace Fomod {

namespace {

QString cleanPath(QString path)
{
    path.replace('\\', '/');
    while (path.startsWith('/')) {
        path.remove(0, 1);
    }
    while (path.endsWith('/')) {
        path.chop(1);
    }
    return path;
}

QList<QDomElement> children(const QDomElement& parent, const QString& tag = {})
{
    QList<QDomElement> out;
    for (auto child = parent.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
        if (tag.isEmpty() || child.tagName() == tag) {
            out << child;
        }
    }
    return out;
}

PluginType parsePluginType(const QString& name)
{
    if (name == "Required") {
        return PluginType::Required;
    }
    if (name == "Recommended") {
        return PluginType::Recommended;
    }
    if (name == "NotUsable") {
        return PluginType::NotUsable;
    }
    if (name == "CouldBeUsable") {
        return PluginType::CouldBeUsable;
    }
    return PluginType::Optional;
}

GroupType parseGroupType(const QString& name)
{
    if (name == "SelectExactlyOne") {
        return GroupType::SelectExactlyOne;
    }
    if (name == "SelectAtMostOne") {
        return GroupType::SelectAtMostOne;
    }
    if (name == "SelectAtLeastOne") {
        return GroupType::SelectAtLeastOne;
    }
    if (name == "SelectAll") {
        return GroupType::SelectAll;
    }
    return GroupType::SelectAny;
}

Condition parseCondition(const QDomElement& element)
{
    Condition condition;
    const auto tag = element.tagName();
    if (tag == "flagDependency") {
        condition.kind = Condition::Kind::Flag;
        condition.name = element.attribute("flag");
        condition.value = element.attribute("value");
    } else if (tag == "fileDependency") {
        condition.kind = Condition::Kind::File;
        condition.name = cleanPath(element.attribute("file"));
        const auto state = element.attribute("state");
        condition.fileState = state == "Missing" ? FileState::Missing : state == "Inactive" ? FileState::Inactive : FileState::Active;
    } else if (tag == "dependencies" || tag == "visible" || tag == "moduleDependencies") {
        condition.kind = Condition::Kind::Group;
        condition.orOperator = element.attribute("operator") == "Or";
        for (const auto& child : children(element)) {
            condition.children << parseCondition(child);
        }
    } else {
        // game, script extender and manager version checks are taken as met
        condition.kind = Condition::Kind::Always;
    }
    return condition;
}

QList<FileItem> parseFiles(const QDomElement& parent)
{
    QList<FileItem> items;
    for (const auto& element : children(parent)) {
        if (element.tagName() != "file" && element.tagName() != "folder") {
            continue;
        }
        FileItem item;
        item.isFolder = element.tagName() == "folder";
        item.source = cleanPath(element.attribute("source"));
        item.destination = element.hasAttribute("destination") ? cleanPath(element.attribute("destination")) : item.source;
        item.priority = element.attribute("priority", "0").toInt();
        item.alwaysInstall = element.attribute("alwaysInstall") == "true";
        item.installIfUsable = element.attribute("installIfUsable") == "true";
        items << item;
    }
    return items;
}

template <typename T>
void applyOrder(QList<T>& items, const QString& order)
{
    if (order == "Explicit") {
        return;
    }
    std::stable_sort(items.begin(), items.end(), [](const T& a, const T& b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    if (order == "Descending") {
        std::reverse(items.begin(), items.end());
    }
}

Plugin parsePlugin(const QDomElement& element)
{
    Plugin plugin;
    plugin.name = element.attribute("name");
    plugin.description = element.firstChildElement("description").text().trimmed();
    plugin.image = cleanPath(element.firstChildElement("image").attribute("path"));
    plugin.files = parseFiles(element.firstChildElement("files"));
    for (const auto& flag : children(element.firstChildElement("conditionFlags"), "flag")) {
        plugin.flags.append({ flag.attribute("name"), flag.text() });
    }
    const auto descriptor = element.firstChildElement("typeDescriptor");
    if (const auto type = descriptor.firstChildElement("type"); !type.isNull()) {
        plugin.defaultType = parsePluginType(type.attribute("name"));
    } else if (const auto dependency = descriptor.firstChildElement("dependencyType"); !dependency.isNull()) {
        plugin.defaultType = parsePluginType(dependency.firstChildElement("defaultType").attribute("name"));
        for (const auto& pattern : children(dependency.firstChildElement("patterns"), "pattern")) {
            plugin.patterns << TypePattern{ .condition = parseCondition(pattern.firstChildElement("dependencies")),
                                            .type = parsePluginType(pattern.firstChildElement("type").attribute("name")) };
        }
    }
    return plugin;
}

}  // namespace

Result<Module> parse(const QByteArray& xml)
{
    QDomDocument doc;
    if (const auto result = doc.setContent(xml); !result) {
        return std::unexpected(QObject::tr("The installer configuration is invalid: %1 (line %2)").arg(result.errorMessage).arg(result.errorLine));
    }
    const auto root = doc.documentElement();
    if (root.tagName() != "config") {
        return std::unexpected(QObject::tr("The installer configuration is not a FOMOD configuration."));
    }
    Module module;
    module.name = root.firstChildElement("moduleName").text().trimmed();
    module.image = cleanPath(root.firstChildElement("moduleImage").attribute("path"));
    module.requiredFiles = parseFiles(root.firstChildElement("requiredInstallFiles"));

    const auto steps = root.firstChildElement("installSteps");
    for (const auto& stepElement : children(steps, "installStep")) {
        Step step;
        step.name = stepElement.attribute("name");
        if (const auto visible = stepElement.firstChildElement("visible"); !visible.isNull()) {
            step.hasVisibility = true;
            const auto deps = visible.firstChildElement("dependencies");
            step.visible = parseCondition(deps.isNull() ? visible : deps);
        }
        const auto groups = stepElement.firstChildElement("optionalFileGroups");
        for (const auto& groupElement : children(groups, "group")) {
            Group group;
            group.name = groupElement.attribute("name");
            group.type = parseGroupType(groupElement.attribute("type"));
            const auto plugins = groupElement.firstChildElement("plugins");
            for (const auto& pluginElement : children(plugins, "plugin")) {
                group.plugins << parsePlugin(pluginElement);
            }
            applyOrder(group.plugins, plugins.attribute("order", "Ascending"));
            step.groups << group;
        }
        applyOrder(step.groups, groups.attribute("order", "Ascending"));
        module.steps << step;
    }
    applyOrder(module.steps, steps.attribute("order", "Ascending"));

    for (const auto& pattern : children(root.firstChildElement("conditionalFileInstalls").firstChildElement("patterns"), "pattern")) {
        module.conditionalInstalls << ConditionalInstall{ .condition = parseCondition(pattern.firstChildElement("dependencies")),
                                                          .files = parseFiles(pattern.firstChildElement("files")) };
    }
    return module;
}

Info parseInfo(const QByteArray& xml)
{
    QDomDocument doc;
    if (!doc.setContent(xml)) {
        return {};
    }
    const auto root = doc.documentElement();
    return Info{ .name = root.firstChildElement("Name").text().trimmed(),
                 .version = root.firstChildElement("Version").text().trimmed(),
                 .author = root.firstChildElement("Author").text().trimmed() };
}

bool evaluate(const Condition& condition, const Context& context)
{
    switch (condition.kind) {
        case Condition::Kind::Always:
            return true;
        case Condition::Kind::Flag:
            return context.flags.value(condition.name) == condition.value;
        case Condition::Kind::File:
            return (context.fileState ? context.fileState(condition.name) : FileState::Missing) == condition.fileState;
        case Condition::Kind::Group:
            if (condition.children.isEmpty()) {
                return true;
            }
            if (condition.orOperator) {
                return std::any_of(condition.children.begin(), condition.children.end(), [&](const Condition& c) { return evaluate(c, context); });
            }
            return std::all_of(condition.children.begin(), condition.children.end(), [&](const Condition& c) { return evaluate(c, context); });
    }
    return true;
}

PluginType pluginType(const Plugin& plugin, const Context& context)
{
    for (const auto& pattern : plugin.patterns) {
        if (evaluate(pattern.condition, context)) {
            return pattern.type;
        }
    }
    return plugin.defaultType;
}

bool stepVisible(const Step& step, const Context& context)
{
    return !step.hasVisibility || evaluate(step.visible, context);
}

QHash<QString, QString> flagsFor(const Module& module, const QList<Selection>& selections, const Context& base)
{
    Context context = base;
    for (int s = 0; s < module.steps.size(); s++) {
        if (!stepVisible(module.steps[s], context)) {
            continue;
        }
        for (const auto& selection : selections) {
            if (selection.step != s) {
                continue;
            }
            for (const auto& [name, value] : module.steps[s].groups[selection.group].plugins[selection.plugin].flags) {
                context.flags.insert(name, value);
            }
        }
    }
    return context.flags;
}

QList<FileItem> filesToInstall(const Module& module, const QList<Selection>& selections, const Context& base)
{
    QList<FileItem> files = module.requiredFiles;
    Context context = base;
    context.flags = flagsFor(module, selections, base);

    Context stepContext = base;
    for (int s = 0; s < module.steps.size(); s++) {
        const auto& step = module.steps[s];
        if (!stepVisible(step, stepContext)) {
            continue;
        }
        for (int g = 0; g < step.groups.size(); g++) {
            for (int p = 0; p < step.groups[g].plugins.size(); p++) {
                const auto& plugin = step.groups[g].plugins[p];
                const bool selected = std::any_of(selections.begin(), selections.end(),
                                                  [&](const Selection& sel) { return sel.step == s && sel.group == g && sel.plugin == p; });
                const bool usable = pluginType(plugin, context) != PluginType::NotUsable;
                for (const auto& file : plugin.files) {
                    if (selected || file.alwaysInstall || (file.installIfUsable && usable)) {
                        files << file;
                    }
                }
                if (selected) {
                    for (const auto& [name, value] : plugin.flags) {
                        stepContext.flags.insert(name, value);
                    }
                }
            }
        }
    }
    for (const auto& conditional : module.conditionalInstalls) {
        if (evaluate(conditional.condition, context)) {
            files << conditional.files;
        }
    }
    std::stable_sort(files.begin(), files.end(), [](const FileItem& a, const FileItem& b) { return a.priority < b.priority; });
    return files;
}

QList<Selection> defaultSelections(const Module& module, int stepIndex, const Context& context)
{
    QList<Selection> out;
    if (stepIndex < 0 || stepIndex >= module.steps.size()) {
        return out;
    }
    const auto& step = module.steps[stepIndex];
    for (int g = 0; g < step.groups.size(); g++) {
        const auto& group = step.groups[g];
        QList<int> chosen;
        int firstUsable = -1;
        for (int p = 0; p < group.plugins.size(); p++) {
            const auto type = pluginType(group.plugins[p], context);
            if (type == PluginType::NotUsable) {
                continue;
            }
            if (firstUsable < 0) {
                firstUsable = p;
            }
            if (group.type == GroupType::SelectAll || type == PluginType::Required || type == PluginType::Recommended) {
                chosen << p;
            }
        }
        switch (group.type) {
            case GroupType::SelectExactlyOne:
                if (chosen.isEmpty() && firstUsable >= 0) {
                    chosen << firstUsable;
                }
                chosen = chosen.mid(0, 1);
                break;
            case GroupType::SelectAtMostOne:
                chosen = chosen.mid(0, 1);
                break;
            case GroupType::SelectAtLeastOne:
                if (chosen.isEmpty() && firstUsable >= 0) {
                    chosen << firstUsable;
                }
                break;
            case GroupType::SelectAny:
            case GroupType::SelectAll:
                break;
        }
        for (int p : chosen) {
            out << Selection{ .step = stepIndex, .group = g, .plugin = p };
        }
    }
    return out;
}

}  // namespace Fomod
