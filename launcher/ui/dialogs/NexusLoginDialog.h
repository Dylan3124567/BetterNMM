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

#include <QDialog>
#include <optional>

#include "Result.h"
#include "nexus/NexusAccount.h"

class QLabel;
class QLineEdit;
class QPushButton;

/// Asks for a Nexus Mods personal API key and checks it with Nexus Mods
class NexusLoginDialog : public QDialog {
    Q_OBJECT
   public:
    explicit NexusLoginDialog(QWidget* parent = nullptr);

    /// Shows the dialog; the validated account, or nothing when cancelled
    static std::optional<NexusAccount> ask(QWidget* parent);
    /// Checks a key with Nexus Mods, showing progress
    static Result<NexusAccount> validate(const QString& key, QWidget* parent);

    NexusAccount account() const { return m_account; }

   private:
    void check();

    QLineEdit* m_key = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_ok = nullptr;
    NexusAccount m_account;
};
