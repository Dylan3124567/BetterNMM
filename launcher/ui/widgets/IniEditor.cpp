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

#include "IniEditor.h"

#include <QButtonGroup>
#include <QDir>
#include <QDirListing>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QScopeGuard>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTime>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "DesktopServices.h"
#include "FileSystem.h"
#include "game/Deployer.h"
#include "game/GameInstance.h"
#include "game/ModList.h"

namespace {

class LineNumberArea : public QWidget {
   public:
    explicit LineNumberArea(CodeEditor* editor) : QWidget(editor), m_editor(editor) {}
    QSize sizeHint() const override { return { m_editor->lineNumberWidth(), 0 }; }

   protected:
    void paintEvent(QPaintEvent* event) override { m_editor->paintLineNumbers(event); }

   private:
    CodeEditor* m_editor;
};

/// Only values are edited in place; names and sections are changed in the text
class ValueColumnDelegate : public QStyledItemDelegate {
   public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        return index.column() == 1 ? QStyledItemDelegate::createEditor(parent, option, index) : nullptr;
    }
};

class IniHighlighter : public QSyntaxHighlighter {
   public:
    IniHighlighter(QTextDocument* document, bool dark) : QSyntaxHighlighter(document)
    {
        m_section.setForeground(QColor(dark ? "#82aaff" : "#0451a5"));
        m_section.setFontWeight(QFont::Bold);
        m_key.setForeground(QColor(dark ? "#9cdcfe" : "#001080"));
        m_value.setForeground(QColor(dark ? "#ce9178" : "#a31515"));
        m_number.setForeground(QColor(dark ? "#b5cea8" : "#098658"));
        m_comment.setForeground(QColor(dark ? "#6a9955" : "#008000"));
        m_comment.setFontItalic(true);
    }

   protected:
    void highlightBlock(const QString& text) override
    {
        const auto line = Ini::parse(text);
        switch (line.kind) {
            case Ini::Line::Kind::Comment:
                setFormat(0, static_cast<int>(text.size()), m_comment);
                break;
            case Ini::Line::Kind::Section:
                setFormat(0, static_cast<int>(text.size()), m_section);
                break;
            case Ini::Line::Kind::Setting: {
                const auto equals = static_cast<int>(text.indexOf('='));
                setFormat(0, equals, m_key);
                bool number = false;
                line.value.toDouble(&number);
                setFormat(equals + 1, static_cast<int>(text.size()) - equals - 1, number ? m_number : m_value);
                break;
            }
            default:
                break;
        }
    }

   private:
    QTextCharFormat m_section;
    QTextCharFormat m_key;
    QTextCharFormat m_value;
    QTextCharFormat m_number;
    QTextCharFormat m_comment;
};

}  // namespace

CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent), m_lineNumbers(new LineNumberArea(this))
{
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { setViewportMargins(lineNumberWidth(), 0, 0, 0); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
        if (dy != 0) {
            m_lineNumbers->scroll(0, dy);
        } else {
            m_lineNumbers->update(0, rect.y(), m_lineNumbers->width(), rect.height());
        }
    });
    setViewportMargins(lineNumberWidth(), 0, 0, 0);
}

int CodeEditor::lineNumberWidth() const
{
    const auto digits = QString::number(std::max(1, blockCount())).size();
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * static_cast<int>(digits);
}

void CodeEditor::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    const auto area = contentsRect();
    m_lineNumbers->setGeometry(QRect(area.left(), area.top(), lineNumberWidth(), area.height()));
}

void CodeEditor::paintLineNumbers(QPaintEvent* event)
{
    QPainter painter(m_lineNumbers);
    painter.fillRect(event->rect(), palette().alternateBase());
    painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
    auto block = firstVisibleBlock();
    auto number = block.blockNumber();
    auto top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    auto bottom = top + qRound(blockBoundingRect(block).height());
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.drawText(0, top, m_lineNumbers->width() - 6, fontMetrics().height(), Qt::AlignRight, QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        number++;
    }
}

IniEditor::IniEditor(GameInstance* instance, QWidget* parent) : QWidget(parent), m_instance(instance)
{
    auto* layout = new QHBoxLayout(this);
    auto* splitter = new QSplitter(this);
    layout->addWidget(splitter);

    m_fileList = new QTreeWidget(splitter);
    m_fileList->setHeaderHidden(true);
    m_fileList->setRootIsDecorated(false);
    // long paths of mod INI files scroll sideways instead of being cut off
    m_fileList->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_fileList->header()->setStretchLastSection(false);
    splitter->addWidget(m_fileList);

    auto* right = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    m_path = new QLabel(right);
    m_path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_path->setWordWrap(true);
    rightLayout->addWidget(m_path);

    auto* bar = new QHBoxLayout();
    m_textButton = new QToolButton(right);
    m_textButton->setText(tr("Text"));
    m_textButton->setToolTip(tr("Edit the whole file."));
    m_settingsButton = new QToolButton(right);
    m_settingsButton->setText(tr("Settings"));
    m_settingsButton->setToolTip(tr("Every setting by section; double-click a value to change it."));
    for (auto* button : { m_textButton, m_settingsButton }) {
        button->setCheckable(true);
        button->setAutoRaise(true);
        bar->addWidget(button);
    }
    auto* views = new QButtonGroup(this);
    views->addButton(m_textButton);
    views->addButton(m_settingsButton);
    m_textButton->setChecked(true);
    m_find = new QLineEdit(right);
    m_find->setClearButtonEnabled(true);
    m_find->setPlaceholderText(tr("Find (Ctrl+F)"));
    bar->addWidget(m_find, 1);
    auto* previous = new QToolButton(right);
    previous->setArrowType(Qt::UpArrow);
    previous->setToolTip(tr("Previous match (Shift+F3)"));
    auto* next = new QToolButton(right);
    next->setArrowType(Qt::DownArrow);
    next->setToolTip(tr("Next match (F3)"));
    bar->addWidget(previous);
    bar->addWidget(next);
    m_save = new QToolButton(right);
    m_save->setText(tr("Save"));
    m_save->setToolTip(tr("Save the file (Ctrl+S)."));
    m_revert = new QToolButton(right);
    m_revert->setText(tr("Revert"));
    m_revert->setToolTip(tr("Throw away unsaved changes and load the file again."));
    auto* external = new QToolButton(right);
    external->setText(tr("Open in Editor"));
    external->setToolTip(tr("Open the file in the program Windows uses for INI files."));
    auto* folder = new QToolButton(right);
    folder->setText(tr("Open Folder"));
    for (auto* button : { m_save, m_revert, external, folder }) {
        bar->addWidget(button);
    }
    rightLayout->addLayout(bar);

    m_views = new QStackedWidget(right);
    m_editor = new CodeEditor(m_views);
    new IniHighlighter(m_editor->document(), palette().color(QPalette::Base).lightness() < 128);
    m_views->addWidget(m_editor);
    m_settings = new QTreeWidget(m_views);
    m_settings->setHeaderLabels({ tr("Setting"), tr("Value") });
    m_settings->setItemDelegate(new ValueColumnDelegate(m_settings));
    m_settings->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    m_settings->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_views->addWidget(m_settings);
    rightLayout->addWidget(m_views, 1);
    m_status = new QLabel(right);
    m_status->setWordWrap(true);
    rightLayout->addWidget(m_status);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    // wide enough for paths like Data/SKSE/Plugins/EngineFixes.ini; the splitter adjusts it
    splitter->setSizes({ 260, 700 });

    m_watcher = new QFileSystemWatcher(this);

    connect(m_fileList, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item, QTreeWidgetItem* previousItem) {
        const auto index = item ? item->data(0, Qt::UserRole).toInt() : -1;
        if (!item || index < 0 || index == m_current) {
            return;
        }
        if (!maybeSave()) {
            // stay on the file whose changes the user is keeping
            const QSignalBlocker block(m_fileList);
            m_fileList->setCurrentItem(previousItem);
            return;
        }
        openFile(index);
    });
    connect(m_textButton, &QToolButton::toggled, this, [this](bool text) { showSettingsView(!text); });
    connect(m_find, &QLineEdit::returnPressed, this, [this] { find(false); });
    connect(m_find, &QLineEdit::textChanged, this, [this] {
        if (m_views->currentWidget() == m_settings) {
            rebuildSettings();
        }
    });
    connect(previous, &QToolButton::clicked, this, [this] { find(true); });
    connect(next, &QToolButton::clicked, this, [this] { find(false); });
    connect(m_save, &QToolButton::clicked, this, [this] {
        if (auto saved = save(); !saved) {
            m_status->setText(saved.error());
        }
    });
    connect(m_revert, &QToolButton::clicked, this, &IniEditor::revert);
    connect(external, &QToolButton::clicked, this, [this] {
        if (m_current >= 0 && m_exists) {
            DesktopServices::openPath(m_files[m_current].path);
        }
    });
    connect(folder, &QToolButton::clicked, this, [this] {
        if (m_current >= 0) {
            DesktopServices::openPath(QFileInfo(m_files[m_current].path).absolutePath(), true);
        }
    });
    connect(m_editor->document(), &QTextDocument::modificationChanged, this, &IniEditor::updateState);
    // a double-click on the setting's name edits its value too
    connect(m_settings, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (item->parent() && column == 0) {
            m_settings->editItem(item, 1);
        }
    });
    connect(m_settings, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        if (item->parent()) {
            m_settings->editItem(item, 1);
        }
    });
    connect(m_settings, &QTreeWidget::itemChanged, this, &IniEditor::settingEdited);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &IniEditor::fileChangedOnDisk);

    auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
    saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveShortcut, &QShortcut::activated, m_save, &QToolButton::click);
    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut, &QShortcut::activated, this, [this] {
        m_find->setFocus();
        m_find->selectAll();
    });
    auto* nextShortcut = new QShortcut(QKeySequence::FindNext, this);
    nextShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(nextShortcut, &QShortcut::activated, this, [this] { find(false); });
    auto* previousShortcut = new QShortcut(QKeySequence::FindPrevious, this);
    previousShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(previousShortcut, &QShortcut::activated, this, [this] { find(true); });
}

void IniEditor::reloadFiles()
{
    const auto currentPath = m_current >= 0 ? m_files[m_current].path : QString();
    m_files.clear();
    QSet<QString> seen;
    auto add = [&](File file) {
        if (!seen.contains(file.path.toLower())) {
            seen.insert(file.path.toLower());
            m_files << file;
        }
    };
    const auto def = m_instance->definition();
    if (const auto myGames = m_instance->myGamesDir(); !myGames.isEmpty()) {
        for (const auto& name : def.iniFiles) {
            add({ .path = FS::PathCombine(myGames, name), .label = name, .settings = true });
        }
        if (QFileInfo(myGames).isDir()) {
            for (const auto& entry : QDirListing(myGames, QStringList{ "*.ini" }, QDirListing::IteratorFlag::FilesOnly)) {
                add({ .path = entry.absoluteFilePath(), .label = entry.fileName(), .settings = true });
            }
        }
    }
    // INI files in the game folder, with the mod that put each there
    QMap<QString, QString> owners;
    if (auto manifest = DeploymentManifest::load(m_instance->deploymentManifestPath())) {
        for (auto it = manifest->files.begin(); it != manifest->files.end(); ++it) {
            owners.insert(it.key(), it.value().mod);
        }
    }
    QList<File> gameFiles;
    const QDir root(m_instance->gameRoot());
    if (root.exists()) {
        for (const auto& entry : QDirListing(root.absolutePath(), QStringList{ "*.ini" },
                                             QDirListing::IteratorFlag::FilesOnly | QDirListing::IteratorFlag::Recursive)) {
            const auto rel = root.relativeFilePath(entry.absoluteFilePath());
            gameFiles << File{ .path = entry.absoluteFilePath(), .label = rel, .modId = owners.value(rel.toLower()) };
        }
    }
    std::sort(gameFiles.begin(), gameFiles.end(), [](const File& a, const File& b) { return a.label.compare(b.label, Qt::CaseInsensitive) < 0; });
    for (const auto& file : gameFiles) {
        add(file);
    }

    const QSignalBlocker block(m_fileList);
    m_fileList->clear();
    auto* settingsGroup = new QTreeWidgetItem(m_fileList, { tr("Game settings (My Games)") });
    auto* gameGroup = new QTreeWidgetItem(m_fileList, { tr("Game folder") });
    for (auto* group : { settingsGroup, gameGroup }) {
        auto font = group->font(0);
        font.setBold(true);
        group->setFont(0, font);
        group->setFlags(Qt::ItemIsEnabled);
        group->setData(0, Qt::UserRole, -1);
    }
    QTreeWidgetItem* toSelect = nullptr;
    for (int i = 0; i < m_files.size(); i++) {
        const auto& file = m_files[i];
        auto label = file.label;
        if (!file.modId.isEmpty()) {
            const auto row = m_instance->modList()->indexOf(file.modId);
            label += tr(" (from %1)").arg(row >= 0 ? m_instance->modList()->mods()[row].name : file.modId);
        }
        auto* item = new QTreeWidgetItem(file.settings ? settingsGroup : gameGroup, { label });
        item->setData(0, Qt::UserRole, i);
        item->setToolTip(0, QDir::toNativeSeparators(file.path));
        if (!QFileInfo::exists(file.path)) {
            item->setText(0, tr("%1 (not created yet)").arg(label));
            auto font = item->font(0);
            font.setItalic(true);
            item->setFont(0, font);
        }
        if (file.path == currentPath || (!toSelect && currentPath.isEmpty())) {
            toSelect = item;
        }
    }
    m_fileList->expandAll();
    for (auto* group : { settingsGroup, gameGroup }) {
        group->setHidden(group->childCount() == 0);
    }
    m_current = -1;
    if (toSelect) {
        m_fileList->setCurrentItem(toSelect);
        const auto index = toSelect->data(0, Qt::UserRole).toInt();
        if (m_files[index].path == currentPath && m_editor->document()->isModified()) {
            m_current = index;  // the list changed around the open file; its unsaved edits stay
            updateState();
        } else {
            openFile(index);
        }
    } else {
        m_editor->clear();
        m_status->setText(tr("This instance has no INI files."));
        updateState();
    }
}

void IniEditor::openFile(int index)
{
    if (!m_watcher->files().isEmpty()) {
        m_watcher->removePaths(m_watcher->files());
    }
    m_current = index;
    const auto& file = m_files[index];
    QFile in(file.path);
    m_exists = in.exists();
    m_loaded = {};
    m_status->clear();
    if (m_exists) {
        if (in.open(QIODevice::ReadOnly)) {
            m_loaded = Ini::decode(in.readAll());
        } else {
            m_status->setText(tr("Could not read %1: %2").arg(file.label, in.errorString()));
        }
        m_watcher->addPath(file.path);
    } else {
        m_status->setText(tr("%1 does not exist yet; saving creates it.").arg(file.label));
    }
    m_editor->setPlainText(m_loaded.text);
    m_editor->document()->setModified(false);
    rebuildSettings();
    updateState();
}

Result<> IniEditor::save()
{
    if (m_current < 0) {
        return {};
    }
    const auto& file = m_files[m_current];
    auto text = m_loaded;
    text.text = m_editor->toPlainText();
    m_saving = true;
    const auto done = qScopeGuard([this] { m_saving = false; });
    TRY(Ini::save(m_instance, file.path, Ini::encode(text)));
    m_loaded = text;
    const bool created = !m_exists;
    m_exists = true;
    m_editor->document()->setModified(false);
    if (!m_watcher->files().contains(file.path)) {
        m_watcher->addPath(file.path);
    }
    m_status->setText(tr("Saved at %1.").arg(QTime::currentTime().toString(Qt::TextDate)));
    if (created) {
        // the "not created yet" mark goes away
        reloadFiles();
    }
    return {};
}

void IniEditor::revert()
{
    if (m_current >= 0) {
        const auto index = m_current;
        openFile(index);
        m_status->setText(tr("Loaded the file again."));
    }
}

bool IniEditor::maybeSave()
{
    if (m_current < 0 || !m_editor->document()->isModified()) {
        return true;
    }
    const auto answer = QMessageBox::question(this, tr("Unsaved INI changes"), tr("Save your changes to %1?").arg(m_files[m_current].label),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) {
        return false;
    }
    if (answer == QMessageBox::Discard) {
        revert();
        return true;
    }
    if (auto saved = save(); !saved) {
        QMessageBox::warning(this, tr("Unsaved INI changes"), saved.error());
        return false;
    }
    return true;
}

void IniEditor::find(bool backwards)
{
    const auto text = m_find->text();
    if (text.isEmpty() || m_views->currentWidget() != m_editor) {
        return;
    }
    const auto flags = backwards ? QTextDocument::FindBackward : QTextDocument::FindFlags();
    if (m_editor->find(text, flags)) {
        m_status->clear();
        return;
    }
    // wrap around to the other end
    auto cursor = m_editor->textCursor();
    cursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
    m_editor->setTextCursor(cursor);
    m_status->setText(m_editor->find(text, flags) ? tr("Searched from the %1 again.").arg(backwards ? tr("end") : tr("start"))
                                                  : tr("\"%1\" is not in this file.").arg(text));
}

void IniEditor::showSettingsView(bool settings)
{
    m_views->setCurrentWidget(settings ? static_cast<QWidget*>(m_settings) : m_editor);
    m_find->setPlaceholderText(settings ? tr("Filter settings (Ctrl+F)") : tr("Find (Ctrl+F)"));
    rebuildSettings();
}

void IniEditor::rebuildSettings()
{
    if (m_views->currentWidget() != m_settings) {
        return;
    }
    m_updatingSettings = true;
    m_settings->clear();
    const auto filter = m_find->text().trimmed();
    QTreeWidgetItem* section = nullptr;
    QString sectionName;
    for (auto block = m_editor->document()->begin(); block.isValid(); block = block.next()) {
        const auto line = Ini::parse(block.text());
        if (line.kind == Ini::Line::Kind::Section) {
            sectionName = line.section;
            section = nullptr;
            continue;
        }
        if (line.kind != Ini::Line::Kind::Setting) {
            continue;
        }
        const bool matches = filter.isEmpty() || sectionName.contains(filter, Qt::CaseInsensitive) ||
                             line.key.contains(filter, Qt::CaseInsensitive) || line.value.contains(filter, Qt::CaseInsensitive);
        if (!matches) {
            continue;
        }
        if (!section) {
            section = new QTreeWidgetItem(m_settings, { sectionName.isEmpty() ? tr("(before the first section)") : QString("[%1]").arg(sectionName) });
            auto font = section->font(0);
            font.setBold(true);
            section->setFont(0, font);
            section->setFirstColumnSpanned(true);
        }
        auto* item = new QTreeWidgetItem(section, { line.key, line.value });
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setData(0, Qt::UserRole, block.blockNumber());
        if (const auto type = Ini::typeDescription(line.key); !type.isEmpty()) {
            item->setToolTip(0, type);
            item->setToolTip(1, type);
        }
    }
    m_settings->expandAll();
    m_updatingSettings = false;
}

void IniEditor::settingEdited(QTreeWidgetItem* item, int column)
{
    if (m_updatingSettings || column != 1 || !item->parent()) {
        return;
    }
    auto block = m_editor->document()->findBlockByNumber(item->data(0, Qt::UserRole).toInt());
    const auto line = Ini::parse(block.text());
    const auto value = item->text(1).trimmed();
    if (line.kind != Ini::Line::Kind::Setting || value == line.value) {
        return;
    }
    if (const auto problem = Ini::checkValue(line.key, value); !problem.isEmpty()) {
        m_status->setText(problem);
        m_updatingSettings = true;
        item->setText(1, line.value);
        m_updatingSettings = false;
        return;
    }
    // edited through the text, so it is one undo step there and the file stays the single truth
    QTextCursor cursor(block);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.insertText(Ini::withValue(block.text(), value));
    m_status->setText(tr("%1 is now %2 (not saved yet).").arg(line.key, value));
}

void IniEditor::updateState()
{
    const bool open = m_current >= 0;
    const bool modified = open && m_editor->document()->isModified();
    m_save->setEnabled(modified);
    m_revert->setEnabled(modified);
    m_editor->setEnabled(open);
    if (!open) {
        m_path->clear();
        return;
    }
    const auto& file = m_files[m_current];
    auto path = QDir::toNativeSeparators(file.path);
    if (!file.modId.isEmpty()) {
        path += tr(" (saved into the mod that installed it)");
    }
    m_path->setText(modified ? tr("%1 (unsaved changes)").arg(path) : path);
}

void IniEditor::fileChangedOnDisk()
{
    if (m_saving || m_current < 0) {
        return;
    }
    const auto& path = m_files[m_current].path;
    // a file replaced on disk drops out of the watcher
    if (QFileInfo::exists(path) && !m_watcher->files().contains(path)) {
        m_watcher->addPath(path);
    }
    // the watcher also reports this editor's own saves, late; the bytes tell whether anything else happened
    QFile onDisk(path);
    if (onDisk.open(QIODevice::ReadOnly) && onDisk.readAll() == Ini::encode(m_loaded)) {
        return;
    }
    if (!m_editor->document()->isModified()) {
        openFile(m_current);
        m_status->setText(tr("Loaded again: the file changed on disk."));
    } else {
        m_status->setText(tr("Another program changed this file. Saving replaces its changes; Revert loads them instead."));
    }
}
