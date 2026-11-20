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

#include "ConflictsDialog.h"

#include <QAbstractTableModel>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QDialogButtonBox>
#include <QDir>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QTreeView>
#include <QVBoxLayout>

#include "game/ModList.h"
#include "ui/dialogs/CustomMessageBox.h"

namespace {
enum Column { FileColumn, ModColumn, StatusColumn, OthersColumn, ColumnCount };
}

class ConflictFiles : public QAbstractTableModel {
   public:
    explicit ConflictFiles(QObject* parent) : QAbstractTableModel(parent) {}

    void reset(const ModList* mods, QList<ModList::ModFile> files)
    {
        beginResetModel();
        m_files = std::move(files);
        m_names.clear();
        m_enabled.clear();
        for (const auto& mod : mods->mods()) {
            m_names.insert(mod.id, mod.name);
            if (mod.enabled) {
                m_enabled.insert(mod.id);
            }
        }
        endResetModel();
    }
    const ModList::ModFile& file(int row) const { return m_files[row]; }
    const QList<ModList::ModFile>& files() const { return m_files; }

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(m_files.size()); }
    int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : ColumnCount; }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_files.size()) {
            return {};
        }
        const auto& file = m_files[index.row()];
        switch (role) {
            case Qt::DisplayRole:
                switch (index.column()) {
                    case FileColumn:
                        return QDir::toNativeSeparators(file.path);
                    case ModColumn:
                        return name(file.mod);
                    case StatusColumn:
                        return status(file);
                    case OthersColumn: {
                        QStringList names;
                        for (const auto& other : file.others) {
                            names << name(other);
                        }
                        return names.join(", ");
                    }
                    default:
                        return {};
                }
            case Qt::ToolTipRole:
                if (file.hidden) {
                    return ConflictsDialog::tr("Hidden: it stays in the mod's folder as %1 but is not put in the game folder.")
                        .arg(QDir::toNativeSeparators(file.path + ModList::HiddenSuffix));
                }
                if (!file.wins && !file.others.isEmpty() && m_enabled.contains(file.mod)) {
                    return ConflictsDialog::tr("The game gets the copy of %1, the mod with the highest priority (lowest in the list) "
                                               "that has the file. Hide the file in that mod to let the next one win.")
                        .arg(name(file.others.last()));
                }
                return {};
            case Qt::ForegroundRole:
                return file.hidden || !m_enabled.contains(file.mod) ? QVariant(QColor(Qt::gray)) : QVariant();
            case Qt::FontRole:
                if (file.hidden) {
                    QFont font;
                    font.setItalic(true);
                    return font;
                }
                return {};
            default:
                return {};
        }
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
            return {};
        }
        switch (section) {
            case FileColumn:
                return ConflictsDialog::tr("File");
            case ModColumn:
                return ConflictsDialog::tr("Mod");
            case StatusColumn:
                return ConflictsDialog::tr("Status");
            case OthersColumn:
                return ConflictsDialog::tr("Also in");
            default:
                return {};
        }
    }

   private:
    QString name(const QString& id) const { return m_names.value(id, id); }

    QString status(const ModList::ModFile& file) const
    {
        if (file.hidden) {
            return ConflictsDialog::tr("Hidden");
        }
        if (!m_enabled.contains(file.mod)) {
            return ConflictsDialog::tr("Mod disabled");
        }
        if (file.others.isEmpty()) {
            return ConflictsDialog::tr("Only this mod");
        }
        return file.wins ? ConflictsDialog::tr("Wins") : ConflictsDialog::tr("Loses to %1").arg(name(file.others.last()));
    }

    QList<ModList::ModFile> m_files;
    QHash<QString, QString> m_names;
    QSet<QString> m_enabled;
};

class ConflictFilter : public QSortFilterProxyModel {
   public:
    explicit ConflictFilter(QObject* parent) : QSortFilterProxyModel(parent)
    {
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setFilterKeyColumn(-1);
        setSortCaseSensitivity(Qt::CaseInsensitive);
    }

    void setConflictsOnly(bool only)
    {
        beginFilterChange();
        m_conflictsOnly = only;
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }

   protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override
    {
        const auto& file = static_cast<const ConflictFiles*>(sourceModel())->file(row);
        // a hidden file stays in view, so it can be shown again
        if (m_conflictsOnly && file.others.isEmpty() && !file.hidden) {
            return false;
        }
        return QSortFilterProxyModel::filterAcceptsRow(row, parent);
    }

   private:
    bool m_conflictsOnly = false;
};

ConflictsDialog::ConflictsDialog(ModList* mods, QStringList ids, QWidget* parent)
    : QDialog(parent), m_mods(mods), m_ids(std::move(ids)), m_files(new ConflictFiles(this)), m_filter(new ConflictFilter(this))
{
    QStringList names;
    for (const auto& id : std::as_const(m_ids)) {
        if (const int row = m_mods->indexOf(id); row >= 0) {
            names << m_mods->mods()[row].name;
        }
    }
    setWindowTitle(names.isEmpty() ? tr("Conflicts: all enabled mods") : tr("Conflicts: %1").arg(names.join(", ")));
    resize(900, 560);

    auto* layout = new QVBoxLayout(this);
    auto* about = new QLabel(tr("When several enabled mods have the same file, the game gets the copy of the mod lowest in the "
                                "list. <b>Hide</b> takes a file out of the game folder while leaving it in its mod (renamed to "
                                "<i>name</i>%1), so the next mod with it wins, or the game gets none. <b>Show</b> brings it back.")
                                 .arg(ModList::HiddenSuffix),
                             this);
    about->setWordWrap(true);
    layout->addWidget(about);

    auto* filters = new QHBoxLayout;
    auto* search = new QLineEdit(this);
    search->setPlaceholderText(tr("Search files and mods"));
    search->setClearButtonEnabled(true);
    filters->addWidget(search, 1);
    auto* conflictsOnly = new QCheckBox(tr("Only files other mods have too"), this);
    // a few mods' files are worth seeing in full; every mod's are mostly files nobody else has
    conflictsOnly->setChecked(m_ids.isEmpty());
    filters->addWidget(conflictsOnly);
    layout->addLayout(filters);

    m_filter->setSourceModel(m_files);
    m_filter->setConflictsOnly(conflictsOnly->isChecked());
    m_view = new QTreeView(this);
    m_view->setModel(m_filter);
    m_view->setRootIsDecorated(false);
    m_view->setUniformRowHeights(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(FileColumn, Qt::AscendingOrder);
    m_view->header()->setStretchLastSection(true);
    layout->addWidget(m_view, 1);

    m_summary = new QLabel(this);
    layout->addWidget(m_summary);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_hide = buttons->addButton(tr("&Hide"), QDialogButtonBox::ActionRole);
    m_hide->setToolTip(tr("Take the selected files out of the game folder; they stay in their mods."));
    m_unhide = buttons->addButton(tr("&Show"), QDialogButtonBox::ActionRole);
    m_unhide->setToolTip(tr("Put the selected hidden files back in the game folder."));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_hide, &QPushButton::clicked, this, [this] { setSelectedHidden(true); });
    connect(m_unhide, &QPushButton::clicked, this, [this] { setSelectedHidden(false); });
    connect(search, &QLineEdit::textChanged, m_filter, &QSortFilterProxyModel::setFilterFixedString);
    connect(conflictsOnly, &QCheckBox::toggled, this, [this](bool only) {
        m_filter->setConflictsOnly(only);
        updateButtons();
    });
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ConflictsDialog::updateButtons);
    connect(m_filter, &QAbstractItemModel::rowsInserted, this, &ConflictsDialog::updateButtons);
    connect(m_filter, &QAbstractItemModel::rowsRemoved, this, &ConflictsDialog::updateButtons);

    reload();
    m_view->resizeColumnToContents(FileColumn);
    m_view->resizeColumnToContents(ModColumn);
    m_view->resizeColumnToContents(StatusColumn);
}

void ConflictsDialog::reload()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_files->reset(m_mods, m_mods->files(m_ids));
    QApplication::restoreOverrideCursor();
    int shared = 0;
    int hidden = 0;
    for (const auto& file : m_files->files()) {
        shared += file.others.isEmpty() || file.hidden ? 0 : 1;
        hidden += file.hidden ? 1 : 0;
    }
    m_summary->setText(tr("%n file(s)", nullptr, static_cast<int>(m_files->files().size())) + ", " +
                       tr("%n that other mods have too", nullptr, shared) + ", " + tr("%n hidden", nullptr, hidden));
    updateButtons();
}

void ConflictsDialog::setSelectedHidden(bool hidden)
{
    QList<std::pair<QString, QString>> files;
    for (const auto& index : m_view->selectionModel()->selectedRows()) {
        const auto& file = m_files->file(m_filter->mapToSource(index).row());
        if (file.hidden != hidden) {
            files << std::pair{ file.mod, file.path };
        }
    }
    if (files.isEmpty()) {
        return;
    }
    const auto done = m_mods->setFilesHidden(files, hidden);
    reload();
    // the same files stay selected, so a mistake is one click to undo
    QSet<std::pair<QString, QString>> wanted(files.begin(), files.end());
    QItemSelection selection;
    for (int row = 0; row < m_files->rowCount(); row++) {
        const auto& file = m_files->file(row);
        if (wanted.contains({ file.mod, file.path })) {
            const auto index = m_filter->mapFromSource(m_files->index(row, 0));
            if (index.isValid()) {
                selection.select(index, index);
            }
        }
    }
    m_view->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    if (!done) {
        CustomMessageBox::selectable(this, hidden ? tr("Hide files") : tr("Show files"), done.error(), QMessageBox::Warning)->exec();
    }
}

void ConflictsDialog::updateButtons()
{
    bool anyShown = false;
    bool anyHidden = false;
    for (const auto& index : m_view->selectionModel()->selectedRows()) {
        const bool hidden = m_files->file(m_filter->mapToSource(index).row()).hidden;
        anyHidden |= hidden;
        anyShown |= !hidden;
    }
    m_hide->setEnabled(anyShown);
    m_unhide->setEnabled(anyHidden);
}
