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

#include "HealthCheck.h"

#include <QHash>
#include <QMap>
#include <QObject>

#include <algorithm>

namespace HealthCheck {

QList<Problem> pluginProblems(const QList<PluginEntry>& plugins, const QHash<QString, std::pair<QString, QString>>& disabledMods)
{
    QHash<QString, int> position;  // lower-case name -> load order index
    for (int i = 0; i < plugins.size(); i++) {
        position.insert(plugins[i].name.toLower(), i);
    }
    QList<Problem> out;
    for (int i = 0; i < plugins.size(); i++) {
        const auto& plugin = plugins[i];
        if (!plugin.active && !plugin.implicit) {
            continue;
        }
        for (const auto& master : plugin.masters) {
            const auto found = position.constFind(master.toLower());
            Kind kind;
            if (found == position.cend()) {
                kind = Kind::MissingMaster;
            } else if (const auto& entry = plugins[*found]; !entry.active && !entry.implicit) {
                kind = Kind::InactiveMaster;
            } else if (*found > i) {
                kind = Kind::MasterLoadsLater;
            } else {
                continue;
            }
            Problem problem{ .kind = kind, .plugin = plugin.name, .master = found == position.cend() ? master : plugins[*found].name };
            if (const auto provider = disabledMods.constFind(master.toLower()); kind == Kind::MissingMaster && provider != disabledMods.cend()) {
                problem.kind = Kind::MasterInDisabledMod;
                problem.modId = provider->first;
                problem.requirement = provider->second;
            }
            out << problem;
        }
    }
    return out;
}

namespace {

bool isScriptExtender(const Nexus::Requirement& requirement)
{
    // hosted on its own site; mod pages link it or name it
    return requirement.url.contains("silverlock.org", Qt::CaseInsensitive) || requirement.name.contains("Script Extender", Qt::CaseInsensitive);
}

}  // namespace

QList<Problem> requirementProblems(const QList<ModEntry>& mods, const QList<Nexus::ModDetails>& details, const ScriptExtender& extender)
{
    QHash<QString, QList<const ModEntry*>> installed;  // "game/Nexus mod id" -> installed copies
    for (const auto& mod : mods) {
        if (!mod.separator && mod.nexusModId > 0) {
            installed[mod.nexusGame + '/' + QString::number(mod.nexusModId)] << &mod;
        }
    }
    QMap<QString, Problem> byRequirement;  // ordered, so the list is the same from one check to the next
    for (const auto& detail : details) {
        QStringList needers;
        for (const auto* mod : installed.value(detail.game + '/' + QString::number(detail.modId))) {
            if (mod->enabled) {
                needers << mod->name;
            }
        }
        if (needers.isEmpty()) {
            continue;
        }
        for (const auto& requirement : detail.requirements) {
            if (requirement.gameId != 0 && requirement.gameId != detail.gameId) {
                continue;
            }
            Problem problem{ .requirement = requirement.name, .nexusModId = requirement.modId > 0 ? requirement.modId : 0, .game = detail.game };
            QString key;
            if (requirement.modId > 0) {
                if (requirement.modId == detail.modId) {
                    continue;
                }
                const auto copies = installed.value(detail.game + '/' + QString::number(requirement.modId));
                if (std::any_of(copies.begin(), copies.end(), [](const ModEntry* copy) { return copy->enabled; })) {
                    continue;
                }
                if (!copies.isEmpty()) {
                    problem.kind = Kind::DisabledRequirement;
                    problem.modId = copies.first()->id;
                } else if (extender.exists && requirement.modId == extender.nexusModId) {
                    if (extender.installed) {
                        continue;
                    }
                    problem.kind = Kind::MissingScriptExtender;
                } else {
                    problem.kind = Kind::MissingRequirement;
                }
                key = problem.kind == Kind::MissingScriptExtender ? QStringLiteral("|extender") : problem.game + '/' + QString::number(requirement.modId);
            } else if (extender.exists && isScriptExtender(requirement)) {
                if (extender.installed) {
                    continue;
                }
                problem.kind = Kind::MissingScriptExtender;
                key = QStringLiteral("|extender");
            } else {
                continue;  // a tool or mod outside Nexus Mods: nothing tells whether it is there
            }
            auto found = byRequirement.find(key);
            if (found == byRequirement.end()) {
                found = byRequirement.insert(key, problem);
            }
            for (const auto& needer : std::as_const(needers)) {
                if (!found->neededBy.contains(needer)) {
                    found->neededBy << needer;
                }
            }
            if (const auto notes = requirement.notes.trimmed(); !notes.isEmpty() && !found->notes.contains(notes)) {
                found->notes << notes;
            }
        }
    }
    return byRequirement.values();
}

QString describe(const Problem& problem)
{
    const auto needers = problem.neededBy.join(", ");
    const int count = static_cast<int>(problem.neededBy.size());
    switch (problem.kind) {
        case Kind::MissingMaster:
            return QObject::tr("%1 needs %2, which is not installed: the game crashes while loading.").arg(problem.plugin, problem.master);
        case Kind::MasterInDisabledMod:
            return QObject::tr("%1 needs %2, which is in the disabled mod %3: the game crashes while loading.")
                .arg(problem.plugin, problem.master, problem.requirement);
        case Kind::InactiveMaster:
            return QObject::tr("%1 needs %2, which is not active: the game crashes while loading.").arg(problem.plugin, problem.master);
        case Kind::MasterLoadsLater:
            return QObject::tr("%1 loads before %2, a master it needs.").arg(problem.plugin, problem.master);
        case Kind::MissingRequirement:
            return QObject::tr("%1 need(s) %2, which is not installed.", nullptr, count).arg(needers, problem.requirement);
        case Kind::DisabledRequirement:
            return QObject::tr("%1 need(s) %2, which is disabled.", nullptr, count).arg(needers, problem.requirement);
        case Kind::MissingScriptExtender:
            return QObject::tr("%1 need(s) the script extender (%2), which is not in the game folder.", nullptr, count).arg(needers, problem.requirement);
    }
    return {};
}

QString remedy(const Problem& problem)
{
    switch (problem.kind) {
        case Kind::MissingMaster:
            return QObject::tr("Deactivate %1, or install the mod that has %2").arg(problem.plugin, problem.master);
        case Kind::MasterInDisabledMod:
            return QObject::tr("Enable %1").arg(problem.requirement);
        case Kind::InactiveMaster:
            return QObject::tr("Activate %1").arg(problem.master);
        case Kind::MasterLoadsLater:
            return QObject::tr("Load %1 right before %2").arg(problem.master, problem.plugin);
        case Kind::MissingRequirement:
            return QObject::tr("Install %1 from Nexus Mods").arg(problem.requirement);
        case Kind::DisabledRequirement:
            return QObject::tr("Enable %1").arg(problem.requirement);
        case Kind::MissingScriptExtender:
            return QObject::tr("Install the script extender");
    }
    return {};
}

}  // namespace HealthCheck
