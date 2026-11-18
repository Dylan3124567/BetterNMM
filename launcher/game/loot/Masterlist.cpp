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

#include "Masterlist.h"

#include <QObject>
#include <QSet>

#include "Yaml.h"

namespace Loot {

namespace {

using Kind = YamlNode::Kind;

QString where(const YamlNode& node)
{
    return QObject::tr("line %1").arg(node.line);
}

Result<QString> stringValue(const YamlNode& map, QStringView key, bool required)
{
    const auto* value = map.get(key);
    if (!value || value->kind == Kind::Null) {
        if (required) {
            return std::unexpected(QObject::tr("%1: \"%2\" is missing").arg(where(map), key.toString()));
        }
        return QString();
    }
    if (value->kind != Kind::Scalar) {
        return std::unexpected(QObject::tr("%1: \"%2\" must be text").arg(where(*value), key.toString()));
    }
    return value->scalar;
}

/// The elements of a list value; none when the key is absent
Result<std::vector<YamlNode>> listValue(const YamlNode& map, QStringView key)
{
    const auto* value = map.get(key);
    if (!value || value->kind == Kind::Null) {
        return std::vector<YamlNode>{};
    }
    if (value->kind != Kind::Sequence) {
        return std::unexpected(QObject::tr("%1: \"%2\" must be a list").arg(where(*value), key.toString()));
    }
    return value->items;
}

Result<std::optional<Condition>> conditionValue(const YamlNode& map, QStringView key)
{
    TRY_INTO(const auto text, stringValue(map, key, false));
    if (text.isEmpty()) {
        return std::optional<Condition>();
    }
    TRY_INTO(auto condition, Condition::parse(text));
    return std::optional<Condition>(std::move(condition));
}

Result<std::vector<MetadataFile>> files(const YamlNode& map, QStringView key)
{
    TRY_INTO(const auto elements, listValue(map, key));
    std::vector<MetadataFile> out;
    for (const auto& element : elements) {
        MetadataFile file;
        if (element.kind == Kind::Scalar) {
            file.name = element.scalar;
        } else if (element.kind == Kind::Mapping) {
            TRY_INTO(file.name, stringValue(element, u"name", true));
            TRY_INTO(file.condition, conditionValue(element, u"condition"));
            TRY_INTO(file.constraint, conditionValue(element, u"constraint"));
        } else {
            return std::unexpected(QObject::tr("%1: a file must be a name or a map").arg(where(element)));
        }
        out.push_back(std::move(file));
    }
    return out;
}

QString trimDotGhost(const QString& name)
{
    return name.endsWith(".ghost", Qt::CaseInsensitive) ? name.chopped(6) : name;
}

bool isRegexName(const QString& name)
{
    return std::any_of(name.begin(), name.end(), [](QChar c) { return QStringView(u":\\*?|").contains(c); });
}

void merge(PluginMetadata& target, const PluginMetadata& source)
{
    if (!target.group && source.group) {
        target.group = source.group;
    }
    target.after.insert(target.after.end(), source.after.begin(), source.after.end());
    target.requirements.insert(target.requirements.end(), source.requirements.begin(), source.requirements.end());
}

}  // namespace

QString Masterlist::replacePrelude(const QString& masterlist, const QString& prelude)
{
    // split at the "prelude:" key; the old prelude ends before the first line that is not indented, blank or a comment
    QString before;
    QString remainder;
    if (masterlist.startsWith("prelude:")) {
        before = QStringLiteral("prelude:");
        remainder = masterlist.mid(8);
    } else if (const auto at = masterlist.indexOf("\nprelude:"); at >= 0) {
        before = masterlist.left(at + 9);
        remainder = masterlist.mid(at + 9);
    } else {
        return masterlist;
    }
    QString after;
    for (qsizetype i = 0; i + 1 < remainder.size(); i++) {
        if (remainder[i] == '\n' && !QStringView(u" #\n\r").contains(remainder[i + 1])) {
            after = remainder.mid(i + 1);
            break;
        }
    }
    QString indented = ("  " + QString(prelude).replace("\n", "\n  ")).replace("  \r\n", "\r\n").replace("  \n", "\n");
    if (indented.endsWith("\n  ")) {
        while (indented.endsWith(' ')) {
            indented.chop(1);
        }
    }
    return after.isEmpty() ? before + "\n" + indented : before + "\n" + indented + "\n" + after;
}

Result<Masterlist> Masterlist::load(const QString& masterlist, const QString& prelude)
{
    TRY_INTO(const auto document, parseYaml(replacePrelude(masterlist, prelude)));
    if (document.kind != Kind::Mapping) {
        return std::unexpected(QObject::tr("The masterlist is not a map of sections"));
    }
    Masterlist out;
    TRY_INTO(const auto plugins, listValue(document, u"plugins"));
    for (const auto& entry : plugins) {
        if (entry.kind != Kind::Mapping) {
            return std::unexpected(QObject::tr("%1: a plugin entry must be a map").arg(where(entry)));
        }
        TRY_INTO(auto name, stringValue(entry, u"name", true));
        name = trimDotGhost(name);
        PluginMetadata metadata;
        if (const auto* group = entry.get(u"group"); group && group->kind != Kind::Null) {
            TRY_INTO(metadata.group, stringValue(entry, u"group", false));
        }
        TRY_INTO(metadata.after, files(entry, u"after"));
        TRY_INTO(metadata.requirements, files(entry, u"req"));
        if (isRegexName(name)) {
            QRegularExpression regex("^" + name + "$", QRegularExpression::CaseInsensitiveOption);
            if (!regex.isValid()) {
                return std::unexpected(QObject::tr("%1: invalid regular expression \"%2\": %3").arg(where(entry), name, regex.errorString()));
            }
            out.m_regexPlugins.emplace_back(std::move(regex), std::move(metadata));
        } else {
            const auto key = name.toCaseFolded();
            if (out.m_plugins.contains(key)) {
                return std::unexpected(QObject::tr("%1: \"%2\" has more than one entry").arg(where(entry), name));
            }
            out.m_plugins.insert(key, std::move(metadata));
        }
    }
    TRY_INTO(const auto groups, listValue(document, u"groups"));
    QSet<QString> names;
    for (const auto& entry : groups) {
        if (entry.kind != Kind::Mapping) {
            return std::unexpected(QObject::tr("%1: a group must be a map").arg(where(entry)));
        }
        Group group;
        TRY_INTO(group.name, stringValue(entry, u"name", true));
        TRY_INTO(const auto after, listValue(entry, u"after"));
        for (const auto& element : after) {
            if (element.kind != Kind::Scalar) {
                return std::unexpected(QObject::tr("%1: a group's \"after\" lists group names").arg(where(element)));
            }
            group.after << element.scalar;
        }
        if (names.contains(group.name)) {
            return std::unexpected(QObject::tr("%1: the group \"%2\" is defined twice").arg(where(entry), group.name));
        }
        names.insert(group.name);
        out.m_groups << group;
    }
    if (!names.contains("default")) {
        out.m_groups.prepend(Group{ "default", {} });
    }
    return out;
}

PluginMetadata Masterlist::find(const QString& pluginName) const
{
    auto metadata = m_plugins.value(pluginName.toCaseFolded());
    for (const auto& [regex, entry] : m_regexPlugins) {
        if (regex.match(pluginName).hasMatch()) {
            merge(metadata, entry);
        }
    }
    return metadata;
}

}  // namespace Loot
