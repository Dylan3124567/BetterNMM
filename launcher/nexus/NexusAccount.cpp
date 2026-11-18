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

#include "NexusAccount.h"

#include <QFile>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>

#include "FileSystem.h"

QString NexusAccount::membership() const
{
    if (premium) {
        return QObject::tr("Premium");
    }
    if (supporter) {
        return QObject::tr("Supporter");
    }
    return QObject::tr("Free");
}

AccountList::AccountList(QObject* parent) : QAbstractTableModel(parent) {}

void AccountList::setListFilePath(const QString& path)
{
    m_path = path;
}

Result<> AccountList::load()
{
    beginResetModel();
    m_accounts.clear();
    m_default = -1;
    auto finish = qScopeGuard([this] { endResetModel(); });
    if (!QFile::exists(m_path)) {
        return {};
    }
    TRY_INTO(auto data, FS::read(m_path));
    const auto root = QJsonDocument::fromJson(data).object();
    if (root.value("format").toString() != "nexus") {
        // anything else is not ours; keep it untouched and start empty
        return std::unexpected(tr("%1 is not a BetterNMM account list").arg(m_path));
    }
    for (const auto& value : root.value("accounts").toArray()) {
        const auto obj = value.toObject();
        NexusAccount account;
        account.apiKey = obj.value("apiKey").toString();
        account.userId = obj.value("userId").toInt();
        account.name = obj.value("name").toString();
        account.premium = obj.value("premium").toBool();
        account.supporter = obj.value("supporter").toBool();
        account.avatarUrl = obj.value("avatarUrl").toString();
        account.validated = QDateTime::fromString(obj.value("validated").toString(), Qt::ISODate);
        if (!account.apiKey.isEmpty()) {
            m_accounts << account;
        }
    }
    m_default = std::min(root.value("default").toInt(-1), static_cast<int>(m_accounts.size()) - 1);
    if (m_default < 0 && !m_accounts.isEmpty()) {
        m_default = 0;
    }
    return {};
}

Result<> AccountList::save() const
{
    QJsonArray accounts;
    for (const auto& account : m_accounts) {
        accounts.append(QJsonObject{ { "apiKey", account.apiKey },
                                     { "userId", account.userId },
                                     { "name", account.name },
                                     { "premium", account.premium },
                                     { "supporter", account.supporter },
                                     { "avatarUrl", account.avatarUrl },
                                     { "validated", account.validated.toString(Qt::ISODate) } });
    }
    QJsonObject root{ { "format", "nexus" }, { "default", m_default }, { "accounts", accounts } };
    return FS::write(m_path, QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void AccountList::upsert(const NexusAccount& account)
{
    for (int i = 0; i < m_accounts.size(); i++) {
        if (m_accounts[i].userId == account.userId) {
            m_accounts[i] = account;
            emit dataChanged(index(i, 0), index(i, ColumnCount - 1));
            changed();
            return;
        }
    }
    beginInsertRows({}, static_cast<int>(m_accounts.size()), static_cast<int>(m_accounts.size()));
    m_accounts << account;
    endInsertRows();
    if (m_default < 0) {
        m_default = 0;
        emit defaultAccountChanged();
    }
    changed();
}

void AccountList::remove(int row)
{
    if (row < 0 || row >= m_accounts.size()) {
        return;
    }
    beginRemoveRows({}, row, row);
    m_accounts.removeAt(row);
    endRemoveRows();
    if (m_default == row) {
        m_default = m_accounts.isEmpty() ? -1 : 0;
        emit defaultAccountChanged();
    } else if (m_default > row) {
        m_default--;
    }
    changed();
}

void AccountList::setDefault(int row)
{
    if (row < 0 || row >= m_accounts.size() || row == m_default) {
        return;
    }
    m_default = row;
    emit dataChanged(index(0, 0), index(static_cast<int>(m_accounts.size()) - 1, ColumnCount - 1));
    emit defaultAccountChanged();
    changed();
}

int AccountList::defaultIndex() const
{
    return m_default;
}

std::optional<NexusAccount> AccountList::defaultAccount() const
{
    if (m_default < 0 || m_default >= m_accounts.size()) {
        return std::nullopt;
    }
    return m_accounts[m_default];
}

QString AccountList::apiKey() const
{
    const auto account = defaultAccount();
    return account ? account->apiKey : QString();
}

bool AccountList::isPremium() const
{
    const auto account = defaultAccount();
    return account && account->premium;
}

void AccountList::changed()
{
    if (auto saved = save(); !saved) {
        qWarning() << "Could not save the Nexus accounts:" << saved.error();
    }
    emit listChanged();
}

int AccountList::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_accounts.size());
}

int AccountList::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant AccountList::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_accounts.size()) {
        return {};
    }
    const auto& account = m_accounts[index.row()];
    if (role == Qt::FontRole && index.row() == m_default) {
        QFont font;
        font.setBold(true);
        return font;
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (index.column()) {
        case NameColumn:
            return index.row() == m_default ? tr("%1 (default)").arg(account.name) : account.name;
        case MembershipColumn:
            return account.membership();
        case ValidatedColumn:
            return account.validated.isValid() ? account.validated.toLocalTime().toString(QLocale().dateTimeFormat(QLocale::ShortFormat))
                                               : tr("Never");
        default:
            return {};
    }
}

QVariant AccountList::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
        case NameColumn:
            return tr("Account");
        case MembershipColumn:
            return tr("Membership");
        case ValidatedColumn:
            return tr("Last checked");
        default:
            return {};
    }
}
