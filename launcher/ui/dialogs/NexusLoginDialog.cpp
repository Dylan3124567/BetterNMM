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

#include "NexusLoginDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "Application.h"
#include "BuildConfig.h"
#include "DesktopServices.h"
#include "net/NetJob.h"
#include "net/RawHeaderProxy.h"
#include "nexus/NexusApi.h"
#include "ui/dialogs/ProgressDialog.h"

NexusLoginDialog::NexusLoginDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Add Nexus Mods Account"));
    auto* layout = new QVBoxLayout(this);
    auto* explain = new QLabel(tr("A key is optional: browsing, updates and downloads work with the Nexus Mods login in %1's download "
                                  "window. With your personal API key, Premium members download without clicking, and this list shows "
                                  "your account. Open your Nexus Mods API key page, copy the \"Personal API Key\" at the bottom and "
                                  "paste it here. The key is stored only on this computer.")
                                   .arg(BuildConfig.LAUNCHER_DISPLAYNAME),
                               this);
    explain->setWordWrap(true);
    layout->addWidget(explain);
    auto* open = new QPushButton(tr("&Open My API Key Page"), this);
    connect(open, &QPushButton::clicked, this, [] { DesktopServices::openUrl(QUrl(BuildConfig.NEXUS_API_KEY_PAGE)); });
    layout->addWidget(open, 0, Qt::AlignLeft);
    m_key = new QLineEdit(this);
    m_key->setEchoMode(QLineEdit::Password);
    m_key->setPlaceholderText(tr("Personal API key"));
    layout->addWidget(m_key);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    m_ok->setText(tr("&Sign In"));
    m_ok->setEnabled(false);
    connect(m_key, &QLineEdit::textChanged, this, [this](const QString& text) { m_ok->setEnabled(!text.trimmed().isEmpty()); });
    connect(buttons, &QDialogButtonBox::accepted, this, &NexusLoginDialog::check);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(460, sizeHint().height());
}

void NexusLoginDialog::check()
{
    auto account = validate(m_key->text().trimmed(), this);
    if (!account) {
        m_status->setText(account.error());
        return;
    }
    m_account = *account;
    accept();
}

std::optional<NexusAccount> NexusLoginDialog::ask(QWidget* parent)
{
    NexusLoginDialog dialog(parent);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return dialog.account();
}

Result<NexusAccount> NexusLoginDialog::validate(const QString& key, QWidget* parent)
{
    auto job = makeShared<NetJob>(tr("Checking your Nexus Mods account"), APPLICATION->network());
    job->setAskRetry(false);
    // the key under test, not the default account's, goes into this request
    auto [request, data] = Net::Request::makeByteArray(Nexus::apiUrl("users/validate.json"));
    request->addHeaderProxy(std::make_unique<Net::RawHeaderProxy>(
        QList<Net::HeaderPair>{ { "apikey", key.toUtf8() },
                                { "Application-Name", BuildConfig.LAUNCHER_NAME.toUtf8() },
                                { "Application-Version", BuildConfig.printableVersionString().toUtf8() } }));
    job->addNetAction(request);
    ProgressDialog progress(parent);
    progress.execWithTask(job.get());
    if (!job->wasSuccessful()) {
        const int status = request->replyStatusCode();
        return std::unexpected(status > 0 ? Nexus::errorMessage(status, {}) : job->failReason());
    }
    auto account = Nexus::parseValidate(*data);
    if (account) {
        account->apiKey = key;
    }
    return account;
}
