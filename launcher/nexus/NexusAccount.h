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

#include <QAbstractTableModel>
#include <QDateTime>
#include <optional>

#include "Result.h"

/// A Nexus Mods account, authenticated with the personal API key from the user's Nexus account settings
struct NexusAccount {
    QString apiKey;
    int userId = 0;
    QString name;
    bool premium = false;
    bool supporter = false;
    QString avatarUrl;
    QDateTime validated;

    QString membership() const;
};

class AccountList : public QAbstractTableModel {
    Q_OBJECT
   public:
    enum Column { NameColumn, MembershipColumn, ValidatedColumn, ColumnCount };

    explicit AccountList(QObject* parent = nullptr);

    void setListFilePath(const QString& path);
    Result<> load();
    Result<> save() const;

    const QList<NexusAccount>& accounts() const { return m_accounts; }
    /// Adds the account or refreshes the stored one with the same user id
    void upsert(const NexusAccount& account);
    void remove(int row);
    void setDefault(int row);
    int defaultIndex() const;
    std::optional<NexusAccount> defaultAccount() const;
    /// The default account's API key, empty without an account
    QString apiKey() const;
    bool isPremium() const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

   signals:
    void listChanged();
    void defaultAccountChanged();

   private:
    void changed();

    QString m_path;
    QList<NexusAccount> m_accounts;
    int m_default = -1;
};
