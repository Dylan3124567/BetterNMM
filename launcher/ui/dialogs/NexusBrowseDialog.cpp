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

#include "NexusBrowseDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Application.h"
#include "DesktopServices.h"
#include "FileSystem.h"
#include "game/GameDefinition.h"
#include "game/GameInstance.h"
#include "game/ModList.h"
#include "net/ApiRequest.h"
#include "nexus/NexusAccount.h"
#include "settings/SettingsObject.h"
#include "ui/dialogs/ModInstallFlow.h"
#include "ui/widgets/ProjectDescriptionPage.h"
#include "ui/widgets/ProjectItem.h"

namespace {

constexpr int g_pageSize = 25;

QString categoryLabel(const QString& category)
{
    static const QHash<QString, QString> labels{ { "MAIN", QObject::tr("Main file") },
                                                 { "UPDATE", QObject::tr("Update") },
                                                 { "OPTIONAL", QObject::tr("Optional") },
                                                 { "MISCELLANEOUS", QObject::tr("Miscellaneous") },
                                                 { "OLD_VERSION", QObject::tr("Old version") },
                                                 { "ARCHIVED", QObject::tr("Archived") },
                                                 { "REMOVED", QObject::tr("Removed") } };
    return labels.value(category.toUpper(), category);
}

/// "SkyUI 6.11", without the version twice when the file name already has it ("Wet and Cold SE v2.4.0")
QString fileTitle(const Nexus::ModFile& file)
{
    return file.version.isEmpty() || file.name.contains(file.version) ? file.name : QString("%1 %2").arg(file.name, file.version);
}

bool isOldFile(const Nexus::ModFile& file)
{
    return file.category == "OLD_VERSION" || file.category == "ARCHIVED" || file.category == "REMOVED";
}

}  // namespace

NexusBrowseDialog::NexusBrowseDialog(GameInstance* instance, QWidget* parent)
    : QDialog(parent), m_instance(instance), m_game(instance->settings()->get("GameDomain").toString())
{
    const auto gameName = instance->settings()->get("GameName").toString();
    setWindowTitle(tr("Browse Nexus Mods - %1").arg(instance->name()));
    for (const auto& mod : instance->modList()->mods()) {
        if (mod.nexusModId > 0) {
            m_installed.insert(mod.nexusModId);
        }
    }

    auto* layout = new QVBoxLayout(this);

    auto* top = new QHBoxLayout();
    m_filterButton = new QToolButton(this);
    m_filterButton->setCheckable(true);
    m_filterButton->setToolTip(tr("Show categories and other filters."));
    m_query = new QLineEdit(this);
    m_query->setPlaceholderText(tr("Search %1 mods...").arg(gameName));
    m_query->setClearButtonEnabled(true);
    top->addWidget(m_filterButton);
    top->addWidget(m_query, 1);
    layout->addLayout(top);

    auto* splitter = new QSplitter(this);
    splitter->setChildrenCollapsible(false);

    auto* left = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    m_filters = new QFrame(left);
    m_filters->setFrameShape(QFrame::StyledPanel);
    m_filters->hide();
    auto* filterLayout = new QGridLayout(m_filters);
    filterLayout->addWidget(new QLabel(tr("Categories"), m_filters), 0, 0, 1, 2);
    m_categories = new QListWidget(m_filters);
    m_categories->setMaximumHeight(180);
    filterLayout->addWidget(m_categories, 1, 0, 1, 2);
    filterLayout->addWidget(new QLabel(tr("Updated"), m_filters), 2, 0);
    m_updated = new QComboBox(m_filters);
    for (const auto& [label, days] : { std::pair{ tr("Any time"), 0 }, std::pair{ tr("In the past day"), 1 }, std::pair{ tr("In the past week"), 7 },
                                       std::pair{ tr("In the past month"), 30 }, std::pair{ tr("In the past 6 months"), 182 },
                                       std::pair{ tr("In the past year"), 365 } }) {
        m_updated->addItem(label, days);
    }
    filterLayout->addWidget(m_updated, 2, 1);
    filterLayout->addWidget(new QLabel(tr("Author"), m_filters), 3, 0);
    m_author = new QLineEdit(m_filters);
    m_author->setClearButtonEnabled(true);
    filterLayout->addWidget(m_author, 3, 1);
    m_adult = new QCheckBox(tr("Show adult content"), m_filters);
    filterLayout->addWidget(m_adult, 4, 0, 1, 2);
    auto* reset = new QPushButton(tr("Reset filters"), m_filters);
    filterLayout->addWidget(reset, 5, 0, 1, 2);
    filterLayout->setColumnStretch(1, 1);
    leftLayout->addWidget(m_filters);

    m_list = new QListWidget(left);
    auto* delegate = new ProjectItemDelegate(m_list);
    m_list->setItemDelegate(delegate);
    m_list->setIconSize(QSize(48, 48));
    m_list->setUniformItemSizes(true);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftLayout->addWidget(m_list, 1);

    auto* sortRow = new QHBoxLayout();
    sortRow->addWidget(new QLabel(tr("Sort by"), left));
    m_sort = new QComboBox(left);
    for (const auto& [label, field] : { std::pair{ tr("Relevance"), "relevance" }, std::pair{ tr("Endorsements"), "endorsements" },
                                        std::pair{ tr("Downloads"), "downloads" }, std::pair{ tr("Unique downloads"), "uniqueDownloads" },
                                        std::pair{ tr("Recently updated"), "updatedAt" }, std::pair{ tr("Newest"), "createdAt" },
                                        std::pair{ tr("Name"), "name" }, std::pair{ tr("File size"), "size" },
                                        std::pair{ tr("Recently commented"), "lastComment" }, std::pair{ tr("Random"), "random" } }) {
        m_sort->addItem(label, QString(field));
    }
    m_sort->setToolTip(tr("Relevance needs search text; without it the most endorsed mods come first."));
    sortRow->addWidget(m_sort, 1);
    leftLayout->addLayout(sortRow);

    auto* right = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    m_title = new QLabel(right);
    m_title->setTextFormat(Qt::RichText);
    m_title->setOpenExternalLinks(true);
    m_title->setWordWrap(true);
    m_stats = new QLabel(right);
    m_stats->setWordWrap(true);
    m_description = new ProjectDescriptionPage(right);
    m_description->setMetaEntry("nexus");
    m_description->setOpenExternalLinks(true);
    rightLayout->addWidget(m_title);
    rightLayout->addWidget(m_stats);
    rightLayout->addWidget(m_description, 1);
    auto* fileRow = new QHBoxLayout();
    fileRow->addWidget(new QLabel(tr("File selected:"), right));
    m_fileBox = new QComboBox(right);
    m_fileBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_fileBox->setMinimumContentsLength(24);
    fileRow->addWidget(m_fileBox, 1);
    rightLayout->addLayout(fileRow);
    m_select = new QPushButton(right);
    rightLayout->addWidget(m_select);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter, 1);

    auto* bottom = new QHBoxLayout();
    m_status = new QLabel(this);
    m_review = new QPushButton(this);
    auto* close = new QPushButton(tr("Close"), this);
    bottom->addWidget(m_status, 1);
    bottom->addWidget(m_review);
    bottom->addWidget(close);
    layout->addLayout(bottom);

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(400);
    connect(m_searchTimer, &QTimer::timeout, this, [this] { search(false); });
    auto filtersChanged = [this] {
        updateFilterButton();
        m_searchTimer->start();
    };

    connect(m_filterButton, &QToolButton::toggled, m_filters, &QWidget::setVisible);
    connect(m_query, &QLineEdit::textChanged, m_searchTimer, qOverload<>(&QTimer::start));
    connect(m_query, &QLineEdit::returnPressed, this, [this] {
        m_searchTimer->stop();
        search(false);
    });
    connect(m_categories, &QListWidget::itemChanged, this, filtersChanged);
    connect(m_updated, &QComboBox::currentIndexChanged, this, filtersChanged);
    connect(m_author, &QLineEdit::textChanged, this, filtersChanged);
    connect(m_adult, &QCheckBox::toggled, this, filtersChanged);
    connect(reset, &QPushButton::clicked, this, [this] {
        {
            const QSignalBlocker blockCategories(m_categories);
            const QSignalBlocker blockUpdated(m_updated);
            const QSignalBlocker blockAuthor(m_author);
            const QSignalBlocker blockAdult(m_adult);
            for (int i = 0; i < m_categories->count(); i++) {
                m_categories->item(i)->setCheckState(Qt::Unchecked);
            }
            m_updated->setCurrentIndex(0);
            m_author->clear();
            m_adult->setChecked(false);
        }
        updateFilterButton();
        search(false);
    });
    connect(m_sort, &QComboBox::currentIndexChanged, this, [this] { search(false); });
    connect(m_list, &QListWidget::currentRowChanged, this, &NexusBrowseDialog::showMod);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { toggleSelection(m_list->row(item)); });
    connect(delegate, &ProjectItemDelegate::checkboxClicked, this, [this](const QModelIndex& index) { toggleSelection(index.row()); });
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        // endless scrolling: the next page comes in before the end of the list is reached
        if (value >= m_list->verticalScrollBar()->maximum() - 3 * 58 && m_mods.size() < m_total) {
            search(true);
        }
    });
    connect(m_select, &QPushButton::clicked, this, [this] { toggleSelection(currentRow()); });
    connect(m_fileBox, &QComboBox::currentIndexChanged, this, [this] {
        const int row = currentRow();
        if (row >= 0 && m_selected.contains(m_mods[row].modId)) {
            for (const auto& file : m_files.value(m_mods[row].modId)) {
                if (file.fileId == m_fileBox->currentData().toInt()) {
                    m_selected[m_mods[row].modId].file = file;
                }
            }
        }
        updateSelectionUi();
    });
    connect(m_review, &QPushButton::clicked, this, &NexusBrowseDialog::reviewAndConfirm);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);

    // Enter belongs to the search box; a default button would also fire on it
    for (auto* button : { reset, m_select, m_review, close }) {
        button->setAutoDefault(false);
    }

    resize(1100, 720);
    restoreGeometry(QByteArray::fromBase64(APPLICATION->settings()->get("NexusBrowseGeometry").toByteArray()));
    updateFilterButton();
    updateSelectionUi();
    loadGameAndCategories();
    search(false);
}

NexusBrowseDialog::~NexusBrowseDialog()
{
    APPLICATION->settings()->set("NexusBrowseGeometry", QString::fromUtf8(saveGeometry().toBase64()));
}

int NexusBrowseDialog::currentRow() const
{
    const int row = m_list->currentRow();
    return row >= 0 && row < m_mods.size() ? row : -1;
}

Nexus::SearchOptions NexusBrowseDialog::currentOptions() const
{
    Nexus::SearchOptions options;
    options.text = m_query->text();
    options.sort = m_sort->currentData().toString();
    for (int i = 0; i < m_categories->count(); i++) {
        if (m_categories->item(i)->checkState() == Qt::Checked) {
            options.categories << m_categories->item(i)->data(Qt::UserRole).toString();
        }
    }
    options.author = m_author->text();
    if (const int days = m_updated->currentData().toInt(); days > 0) {
        options.updatedSince = QDateTime::currentDateTimeUtc().addDays(-days);
    }
    options.adult = m_adult->isChecked();
    return options;
}

void NexusBrowseDialog::updateFilterButton()
{
    int active = static_cast<int>(currentOptions().categories.size());
    active += m_updated->currentIndex() > 0 ? 1 : 0;
    active += m_author->text().trimmed().isEmpty() ? 0 : 1;
    active += m_adult->isChecked() ? 1 : 0;
    m_filterButton->setText(active > 0 ? tr("Filter options (%1)").arg(active) : tr("Filter options"));
}

void NexusBrowseDialog::loadGameAndCategories()
{
    if (m_game.isEmpty()) {
        return;
    }
    m_gameJob = makeShared<NetJob>(tr("Nexus Mods game"), APPLICATION->network());
    m_gameJob->setAskRetry(false);
    auto [gameRequest, gameData] = Net::ApiRequest::makeByteArray(Nexus::graphqlUrl(), Nexus::gameQuery(m_game));
    auto [categoryRequest, categoryData] = Net::ApiRequest::makeByteArray(Nexus::graphqlUrl(), Nexus::categoriesQuery(m_game));
    m_gameData = gameData;
    m_categoryData = categoryData;
    m_gameJob->addNetAction(gameRequest);
    m_gameJob->addNetAction(categoryRequest);
    connect(m_gameJob.get(), &Task::finished, this, [this] {
        if (auto games = Nexus::parseGames(*m_gameData); games && !games->isEmpty()) {
            m_gameId = games->first().id;
        }
        if (auto categories = Nexus::parseCategories(*m_categoryData)) {
            const QSignalBlocker block(m_categories);
            for (const auto& category : *categories) {
                auto* item = new QListWidgetItem(tr("%1 (%2)").arg(category.name.simplified(), QLocale().toString(category.count)), m_categories);
                item->setData(Qt::UserRole, category.name);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Unchecked);
            }
        }
        // files and descriptions need the game id, so a mod picked before it arrived is loaded now
        if (const int row = currentRow(); row >= 0) {
            loadDetails(m_mods[row].modId);
        }
    });
    m_gameJob->start();
}

void NexusBrowseDialog::search(bool more)
{
    if (m_game.isEmpty()) {
        m_status->setText(tr("This instance has no Nexus Mods game id; set it in the instance settings."));
        return;
    }
    if (m_searchJob && m_searchJob->isRunning()) {
        // one search at a time; a changed search starts as soon as this one is done
        m_searchAgain = m_searchAgain || !more;
        return;
    }
    if (more && m_mods.size() >= m_total) {
        return;
    }
    const int offset = more ? static_cast<int>(m_mods.size()) : 0;
    m_searchJob = makeShared<NetJob>(tr("Nexus Mods search"), APPLICATION->network());
    m_searchJob->setAskRetry(false);
    auto [request, data] = Net::ApiRequest::makeByteArray(Nexus::graphqlUrl(), Nexus::searchModsQuery(m_game, currentOptions(), offset, g_pageSize));
    m_searchData = data;
    m_searchJob->addNetAction(request);
    connect(m_searchJob.get(), &Task::finished, this, [this, more] { searchFinished(more); });
    m_status->setText(more ? tr("Loading more mods...") : tr("Searching..."));
    m_searchJob->start();
}

void NexusBrowseDialog::searchFinished(bool more)
{
    if (m_searchAgain) {
        m_searchAgain = false;
        QTimer::singleShot(0, this, [this] { search(false); });
        return;
    }
    if (!m_searchJob->wasSuccessful()) {
        m_status->setText(tr("Search failed: %1").arg(m_searchJob->failReason()));
        return;
    }
    auto result = Nexus::parseSearchMods(*m_searchData);
    if (!result) {
        m_status->setText(result.error());
        return;
    }
    if (!more) {
        m_generation++;
        m_mods.clear();
        m_list->clear();
    }
    for (const auto& mod : result->mods) {
        auto* item = new QListWidgetItem(m_list);
        item->setData(UserDataTypes::TITLE, mod.name);
        item->setData(UserDataTypes::DESCRIPTION, mod.summary);
        item->setData(UserDataTypes::INSTALLED, m_installed.contains(mod.modId));
        item->setToolTip(mod.summary);
        item->setIcon(QIcon::fromTheme("centralmods"));
        item->setSizeHint(QSize(0, 58));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_selected.contains(mod.modId) ? Qt::Checked : Qt::Unchecked);
        m_mods << mod;
        loadThumbnail(static_cast<int>(m_mods.size()) - 1, mod.thumbnailUrl);
    }
    m_total = result->total;
    m_status->setText(m_total == 0 ? tr("No mods match.")
                                   : tr("Showing %1 of %2 mods").arg(QLocale().toString(m_mods.size()), QLocale().toString(m_total)));
    if (!more && !m_mods.isEmpty()) {
        m_list->setCurrentRow(0);
    }
    // a page that does not fill the list leaves nothing to scroll, so the next one comes in right away
    QTimer::singleShot(0, this, [this] {
        if (m_list->verticalScrollBar()->maximum() == 0 && m_mods.size() < m_total) {
            search(true);
        }
    });
}

void NexusBrowseDialog::loadThumbnail(int row, const QString& url)
{
    if (url.isEmpty()) {
        return;
    }
    auto* reply = APPLICATION->network()->get(QNetworkRequest(QUrl(url)));
    reply->setParent(this);  // closing the dialog cancels the downloads still running
    connect(reply, &QNetworkReply::finished, this, [this, reply, row, generation = m_generation] {
        reply->deleteLater();
        QPixmap pixmap;
        if (generation != m_generation || row >= m_list->count() || reply->error() != QNetworkReply::NoError ||
            !pixmap.loadFromData(reply->readAll())) {
            return;
        }
        // Nexus thumbnails are wide; their middle square fills the icon like Prism's project icons
        const int side = std::min(pixmap.width(), pixmap.height());
        pixmap = pixmap.copy((pixmap.width() - side) / 2, (pixmap.height() - side) / 2, side, side)
                     .scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_list->item(row)->setIcon(QIcon(pixmap));
    });
}

void NexusBrowseDialog::showMod()
{
    const int row = currentRow();
    m_description->flush();
    {
        const QSignalBlocker block(m_fileBox);
        m_fileBox->clear();
    }
    if (row < 0) {
        m_title->clear();
        m_stats->clear();
        m_description->clear();
        updateSelectionUi();
        return;
    }
    const auto& mod = m_mods[row];
    auto title = QString("<span style=\"font-size:x-large; font-weight:bold\"><a href=\"%1\">%2</a></span>")
                     .arg(Nexus::modPageUrl(m_game, mod.modId).toString().toHtmlEscaped(), mod.name.toHtmlEscaped());
    if (!mod.author.isEmpty()) {
        title += tr(" by %1").arg(mod.author.toHtmlEscaped());
    }
    m_title->setText(title);
    QStringList stats;
    if (!mod.category.isEmpty()) {
        stats << mod.category;
    }
    if (!mod.version.isEmpty()) {
        stats << tr("Version %1").arg(mod.version);
    }
    stats << tr("%1 endorsements").arg(QLocale().toString(mod.endorsements)) << tr("%1 downloads").arg(QLocale().toString(mod.downloads))
          << tr("updated %1").arg(QLocale().toString(mod.updated.toLocalTime().date(), QLocale::ShortFormat));
    m_stats->setText(stats.join(QString::fromUtf8(" · ")));

    if (m_descriptions.contains(mod.modId)) {
        showDescription(mod.modId);
    } else {
        m_description->setHtml(QString("<p>%1</p><p><i>%2</i></p>").arg(mod.summary.toHtmlEscaped(), tr("Loading the description...")));
    }
    if (m_files.contains(mod.modId)) {
        showFiles(mod.modId);
    } else {
        m_fileBox->addItem(tr("Loading files..."));
    }
    if (!m_descriptions.contains(mod.modId) || !m_files.contains(mod.modId)) {
        loadDetails(mod.modId);
    }
    updateSelectionUi();
}

void NexusBrowseDialog::loadDetails(int modId)
{
    if (m_gameId <= 0 || (m_detailsJob && m_detailsJob->isRunning())) {
        return;  // picked up when the game id arrives or the running request ends
    }
    m_detailsModId = modId;
    m_detailsJob = makeShared<NetJob>(tr("Nexus Mods mod details"), APPLICATION->network());
    m_detailsJob->setAskRetry(false);
    auto [filesRequest, filesData] = Net::ApiRequest::makeByteArray(Nexus::graphqlUrl(), Nexus::modFilesQuery(m_gameId, modId));
    auto [descriptionRequest, descriptionData] =
        Net::ApiRequest::makeByteArray(Nexus::graphqlUrl(), Nexus::modDescriptionQuery(m_gameId, modId));
    m_filesData = filesData;
    m_descriptionData = descriptionData;
    m_detailsJob->addNetAction(filesRequest);
    m_detailsJob->addNetAction(descriptionRequest);
    connect(m_detailsJob.get(), &Task::finished, this, [this] {
        const int modId = m_detailsModId;
        const int row = currentRow();
        const bool current = row >= 0 && m_mods[row].modId == modId;
        if (auto files = Nexus::parseGraphqlModFiles(*m_filesData)) {
            // current files newest first, old and removed ones after them
            auto sorted = *files;
            std::stable_sort(sorted.begin(), sorted.end(), [](const Nexus::ModFile& a, const Nexus::ModFile& b) {
                if (isOldFile(a) != isOldFile(b)) {
                    return !isOldFile(a);
                }
                return a.uploaded > b.uploaded;
            });
            m_files.insert(modId, sorted);
        } else if (current) {
            const QSignalBlocker block(m_fileBox);
            m_fileBox->clear();
            m_fileBox->addItem(tr("Could not load the files: %1").arg(files.error()));
        }
        if (auto description = Nexus::parseModDescription(*m_descriptionData)) {
            m_descriptions.insert(modId, *description);
        } else if (current) {
            m_description->setHtml(QString("<p><i>%1</i></p>").arg(tr("Could not load the description: %1").arg(description.error()).toHtmlEscaped()));
        }
        if (current) {
            if (m_files.contains(modId)) {
                showFiles(modId);
            }
            if (m_descriptions.contains(modId)) {
                showDescription(modId);
            }
        }
        if (m_selectWhenLoaded == modId) {
            m_selectWhenLoaded = 0;
            for (int i = 0; i < m_mods.size(); i++) {
                if (m_mods[i].modId == modId && m_files.contains(modId)) {
                    toggleSelection(i);
                }
            }
        }
        // the user moved on to another mod while this one loaded
        if (row >= 0 && !current && (!m_files.contains(m_mods[row].modId) || !m_descriptions.contains(m_mods[row].modId))) {
            QTimer::singleShot(0, this, [this, next = m_mods[row].modId] { loadDetails(next); });
        }
    });
    m_detailsJob->start();
}

void NexusBrowseDialog::showFiles(int modId)
{
    const QSignalBlocker block(m_fileBox);
    m_fileBox->clear();
    const auto files = m_files.value(modId);
    if (files.isEmpty()) {
        m_fileBox->addItem(tr("This mod has no files to download"));
        updateSelectionUi();
        return;
    }
    const int chosen = m_selected.contains(modId) ? m_selected.value(modId).file.fileId : Nexus::bestFile(files).fileId;
    for (const auto& file : files) {
        const auto parts = QStringList{ fileTitle(file), categoryLabel(file.category),
                                        QLocale().formattedDataSize(file.size) };
        m_fileBox->addItem(parts.join(QString::fromUtf8(" · ")), file.fileId);
        m_fileBox->setItemData(m_fileBox->count() - 1, QTextDocumentFragment::fromHtml(Nexus::bbcodeToHtml(file.description)).toPlainText(),
                               Qt::ToolTipRole);
        if (file.fileId == chosen) {
            m_fileBox->setCurrentIndex(m_fileBox->count() - 1);
        }
    }
    updateSelectionUi();
}

void NexusBrowseDialog::showDescription(int modId)
{
    m_description->flush();
    m_description->setHtml(m_descriptions.value(modId));
}

void NexusBrowseDialog::toggleSelection(int row)
{
    if (row < 0 || row >= m_mods.size()) {
        return;
    }
    const auto mod = m_mods[row];
    if (m_selected.remove(mod.modId) == 0) {
        if (!m_files.contains(mod.modId)) {
            // the file to download is picked once the mod's files are known
            m_selectWhenLoaded = mod.modId;
            if (row != currentRow()) {
                m_list->setCurrentRow(row);
            } else {
                loadDetails(mod.modId);
            }
            return;
        }
        const auto files = m_files.value(mod.modId);
        if (files.isEmpty()) {
            return;
        }
        auto file = Nexus::bestFile(files);
        if (row == currentRow()) {
            for (const auto& candidate : files) {
                if (candidate.fileId == m_fileBox->currentData().toInt()) {
                    file = candidate;
                }
            }
        }
        m_selected.insert(mod.modId, Selection{ mod, file });
    }
    m_list->item(row)->setCheckState(m_selected.contains(mod.modId) ? Qt::Checked : Qt::Unchecked);
    updateSelectionUi();
}

void NexusBrowseDialog::updateSelectionUi()
{
    const int row = currentRow();
    const bool selected = row >= 0 && m_selected.contains(m_mods[row].modId);
    m_select->setText(selected ? tr("Deselect mod for download") : tr("Select mod for download"));
    m_select->setEnabled(selected || (row >= 0 && m_fileBox->currentData().isValid()));
    m_review->setEnabled(!m_selected.isEmpty());
    m_review->setText(m_selected.isEmpty() ? tr("Review and confirm") : tr("Review and confirm (%1)").arg(m_selected.size()));
}

void NexusBrowseDialog::reviewAndConfirm()
{
    if (m_selected.isEmpty()) {
        return;
    }
    const bool premium = APPLICATION->accounts()->isPremium();

    // Like Prism: what the chosen mods need comes along, ticked, saying which mod needs it. Mods the instance has, or
    // that are chosen already, are left out; so is the script extender when the instance has it.
    QSet<int> have;
    for (const auto& mod : m_instance->modList()->mods()) {
        if (mod.nexusModId > 0 && mod.nexusGame == m_game) {
            have.insert(mod.nexusModId);
        }
    }
    const auto game = m_instance->definition();
    const int extenderMod = GameRegistry::byDomain(m_game).scriptExtenderMod;
    if (extenderMod > 0 && !game.scriptExtender.isEmpty() && QFileInfo::exists(FS::PathCombine(m_instance->gameRoot(), game.scriptExtender))) {
        have.insert(extenderMod);
    }
    QList<int> chosenIds;
    for (const auto& selection : std::as_const(m_selected)) {
        chosenIds << selection.mod.modId;
    }
    QString lookupError;
    Nexus::Requirements requirements;
    if (auto resolved = Nexus::resolveRequirements(chosenIds, have, [this](const QList<int>& ids) { return ModInstallFlow::fetchModDetails(m_game, ids, this); })) {
        requirements = *resolved;
    } else {
        lookupError = resolved.error();
    }
    // the newest main file of each needed mod; the script extender's build is picked by its own installer
    QList<Selection> needed;
    bool needsExtender = false;
    QStringList extenderFor;
    QList<int> neededIds;
    for (const auto& dependency : std::as_const(requirements.mods)) {
        if (dependency.mod.modId == extenderMod) {
            needsExtender = true;
            extenderFor = dependency.requiredBy;
        } else {
            neededIds << dependency.mod.modId;
        }
    }
    QMap<int, QStringList> requiredBy;
    if (!neededIds.isEmpty()) {
        const int gameId = m_gameId > 0 ? m_gameId : requirements.mods.first().mod.gameId;
        if (const auto files = ModInstallFlow::fetchModFiles(gameId, neededIds, this)) {
            for (const auto& dependency : std::as_const(requirements.mods)) {
                if (dependency.mod.modId == extenderMod) {
                    continue;
                }
                const auto file = Nexus::bestFile(files->value(dependency.mod.modId));
                if (file.fileId <= 0) {
                    requirements.other << Nexus::OtherRequirement{ .name = dependency.mod.name,
                                                                   .url = Nexus::modPageUrl(m_game, dependency.mod.modId).toString(),
                                                                   .notes = tr("it has no files to download"),
                                                                   .requiredBy = dependency.requiredBy };
                    continue;
                }
                Nexus::ModInfo info;
                info.modId = dependency.mod.modId;
                info.game = m_game;
                info.name = dependency.mod.name;
                info.version = dependency.mod.version;
                needed << Selection{ info, file };
                requiredBy.insert(info.modId, dependency.requiredBy);
            }
        } else {
            lookupError = files.error();
        }
    }

    QDialog review(this);
    review.setWindowTitle(tr("Review and confirm"));
    auto* layout = new QVBoxLayout(&review);
    auto* info = new QLabel(premium ? tr("These files are downloaded and installed into %1:").arg(m_instance->name())
                                    : tr("Nexus Mods lets accounts without Premium download only with its \"Slow download\" button, "
                                         "so BetterNMM shows each file's download page in turn: click the button on each, the next "
                                         "one is already there. Everything then installs into %1.")
                                          .arg(m_instance->name()),
                            &review);
    info->setWordWrap(true);
    layout->addWidget(info);
    if (!needed.isEmpty() || needsExtender) {
        auto* added = new QLabel(tr("The mods marked \"Required by\" were added because a chosen mod needs them. Untick any you get "
                                    "some other way."),
                                 &review);
        added->setWordWrap(true);
        layout->addWidget(added);
    }

    enum Kind { Chosen, Needed, Extender };
    auto* tree = new QTreeWidget(&review);
    tree->setHeaderLabels({ tr("Mod"), tr("File"), tr("Size"), tr("Required by") });
    tree->setRootIsDecorated(false);
    for (const auto& selection : std::as_const(m_selected)) {
        auto* item = new QTreeWidgetItem(tree, { selection.mod.name, fileTitle(selection.file), QLocale().formattedDataSize(selection.file.size) });
        item->setCheckState(0, Qt::Checked);
        item->setData(0, Qt::UserRole, selection.mod.modId);
        item->setData(1, Qt::UserRole, Chosen);
    }
    if (needsExtender) {
        auto* item = new QTreeWidgetItem(tree, { tr("Script extender (%1)").arg(game.scriptExtender), tr("the build for your game version"), QString(),
                                                 extenderFor.join(", ") });
        item->setCheckState(0, Qt::Checked);
        item->setData(1, Qt::UserRole, Extender);
    }
    for (qsizetype i = 0; i < needed.size(); i++) {
        const auto& selection = needed[i];
        auto* item = new QTreeWidgetItem(tree, { selection.mod.name, fileTitle(selection.file), QLocale().formattedDataSize(selection.file.size),
                                                 requiredBy.value(selection.mod.modId).join(", ") });
        item->setCheckState(0, Qt::Checked);
        item->setData(0, Qt::UserRole, int(i));
        item->setData(1, Qt::UserRole, Needed);
    }
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 4; column++) {
        tree->resizeColumnToContents(column);
    }
    layout->addWidget(tree, 1);

    QStringList notes;
    for (const auto& other : std::as_const(requirements.other)) {
        auto line = other.url.isEmpty() ? other.name.toHtmlEscaped()
                                        : QString("<a href=\"%1\">%2</a>").arg(other.url.toHtmlEscaped(), other.name.toHtmlEscaped());
        if (!other.notes.isEmpty()) {
            line += " \u2014 " + other.notes.toHtmlEscaped();
        }
        notes << tr("%1 (required by %2)").arg(line, other.requiredBy.join(", ").toHtmlEscaped());
    }
    if (!notes.isEmpty() || !lookupError.isEmpty()) {
        QString text;
        if (!notes.isEmpty()) {
            text = tr("<b>Also needed, but BetterNMM can not download it:</b>") + "<br>" + notes.join("<br>");
        }
        if (!lookupError.isEmpty()) {
            text += (text.isEmpty() ? QString() : QString("<br><br>")) +
                    tr("Could not check what these mods need: %1").arg(lookupError.toHtmlEscaped());
        }
        auto* other = new QLabel(text, &review);
        other->setWordWrap(true);
        other->setTextFormat(Qt::RichText);
        other->setOpenExternalLinks(true);
        layout->addWidget(other);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &review);
    buttons->button(QDialogButtonBox::Ok)->setText(premium ? tr("Install") : tr("Download"));
    connect(buttons, &QDialogButtonBox::accepted, &review, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &review, &QDialog::reject);
    layout->addWidget(buttons);
    review.resize(760, 440);
    if (review.exec() != QDialog::Accepted) {
        return;
    }

    // what they need first, so each mod finds its requirements installed
    QList<Selection> chosen;
    QList<Selection> install;
    bool installExtender = false;
    for (int i = 0; i < tree->topLevelItemCount(); i++) {
        const auto* item = tree->topLevelItem(i);
        if (item->checkState(0) != Qt::Checked) {
            continue;
        }
        switch (item->data(1, Qt::UserRole).toInt()) {
            case Chosen:
                chosen << m_selected.value(item->data(0, Qt::UserRole).toInt());
                break;
            case Needed:
                install << needed.value(item->data(0, Qt::UserRole).toInt());
                break;
            case Extender:
                installExtender = true;
                break;
        }
    }
    install << chosen;
    if (installExtender) {
        ModInstallFlow::installScriptExtender(m_instance, this);
    }
    QList<ModInstallFlow::NexusFile> fromWebsite;
    for (const auto& selection : std::as_const(install)) {
        m_selected.remove(selection.mod.modId);
        if (!premium) {
            fromWebsite << ModInstallFlow::NexusFile{ .modId = selection.mod.modId,
                                                      .modName = selection.mod.name,
                                                      .modVersion = selection.mod.version,
                                                      .file = selection.file };
        } else if (ModInstallFlow::installNexusFile(m_instance, m_game, selection.mod.modId, selection.file.fileId, this)) {
            m_installed.insert(selection.mod.modId);
        }
    }
    if (!fromWebsite.isEmpty()) {
        ModInstallFlow::downloadFromWebsite(m_instance, m_game, fromWebsite, this);
    }
    for (int i = 0; i < m_list->count() && i < m_mods.size(); i++) {
        m_list->item(i)->setCheckState(m_selected.contains(m_mods[i].modId) ? Qt::Checked : Qt::Unchecked);
        m_list->item(i)->setData(UserDataTypes::INSTALLED, m_installed.contains(m_mods[i].modId));
    }
    updateSelectionUi();
}
