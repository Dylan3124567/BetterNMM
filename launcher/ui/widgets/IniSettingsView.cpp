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

#include "IniSettingsView.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTime>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

#include "FileSystem.h"
#include "game/GameInstance.h"

namespace {

QString sizeText(const QSize& size)
{
    return QString("%1 × %2").arg(size.width()).arg(size.height());
}

/// The sizes of this computer's screens, then common ones, smallest first
QList<std::pair<QSize, bool>> resolutions()
{
    QList<std::pair<QSize, bool>> out;
    const auto add = [&out](const QSize& size, bool screen) {
        if (std::none_of(out.cbegin(), out.cend(), [&size](const auto& known) { return known.first == size; })) {
            out.append({ size, screen });
        }
    };
    for (const auto* screen : QGuiApplication::screens()) {
        add(screen->size() * screen->devicePixelRatio(), true);
    }
    for (const auto& size : { QSize(1280, 720), QSize(1366, 768), QSize(1600, 900), QSize(1920, 1080), QSize(2560, 1080), QSize(2560, 1440),
                              QSize(3440, 1440), QSize(3840, 2160) }) {
        add(size, false);
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return std::pair(a.first.width() * a.first.height(), a.first.width()) < std::pair(b.first.width() * b.first.height(), b.first.width());
    });
    return out;
}

/// The value as a number; the default when the file holds something else
double number(const QString& value, const QString& fallback)
{
    bool ok = false;
    const auto parsed = value.toDouble(&ok);
    return ok ? parsed : fallback.toDouble();
}

}  // namespace

IniSettingsView::IniSettingsView(GameInstance* instance, const IniSettings::Catalog& catalog, QWidget* parent)
    : QWidget(parent), m_instance(instance), m_catalog(catalog), m_files(instance, catalog)
{
    auto* layout = new QVBoxLayout(this);

    m_presetBar = new QWidget(this);
    auto* presetRow = new QHBoxLayout(m_presetBar);
    presetRow->setContentsMargins(0, 0, 0, 0);
    presetRow->addWidget(new QLabel(tr("Quality preset:"), m_presetBar));
    for (const auto& name : m_catalog.presets) {
        auto* button = new QPushButton(QFileInfo(name).completeBaseName(), m_presetBar);
        button->setToolTip(tr("Sets the graphics settings the game's own launcher uses for this quality. "
                              "Resolution, controls and everything else stay as they are."));
        connect(button, &QPushButton::clicked, this, [this, name, label = button->text()] {
            const auto applied = m_files.applyPreset(FS::PathCombine(m_instance->gameRoot(), name));
            if (!applied) {
                m_status->setText(applied.error());
                return;
            }
            refresh();
            updateState();
            m_status->setText(tr("Applied the %1 preset (%2 values). Save to keep it.").arg(label).arg(*applied));
        });
        presetRow->addWidget(button);
        m_presets << button;
    }
    presetRow->addStretch();
    layout->addWidget(m_presetBar);

    auto* body = new QHBoxLayout;
    m_categories = new QListWidget(this);
    m_pages = new QStackedWidget(this);
    for (const auto& category : m_catalog.categories) {
        m_categories->addItem(category.name);
        m_pages->addWidget(createPage(category));
    }
    m_categories->setFixedWidth(m_categories->sizeHintForColumn(0) + 2 * m_categories->frameWidth() + 24);
    // selection rather than the current row, so accessibility tools that select an item switch pages too
    connect(m_categories, &QListWidget::itemSelectionChanged, this, [this] {
        if (const auto selected = m_categories->selectedItems(); !selected.isEmpty()) {
            m_pages->setCurrentIndex(m_categories->row(selected.first()));
        }
    });
    m_categories->setCurrentRow(0);
    body->addWidget(m_categories);
    body->addWidget(m_pages, 1);
    layout->addLayout(body, 1);

    auto* bottom = new QHBoxLayout;
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_revert = new QPushButton(tr("Revert"), this);
    m_revert->setToolTip(tr("Forget the unsaved changes and show what the files say"));
    m_save = new QPushButton(tr("Save"), this);
    bottom->addWidget(m_status, 1);
    bottom->addWidget(m_revert);
    bottom->addWidget(m_save);
    layout->addLayout(bottom);
    connect(m_save, &QPushButton::clicked, this, [this] {
        if (const auto saved = save(); !saved) {
            m_status->setText(saved.error());
        }
    });
    connect(m_revert, &QPushButton::clicked, this, &IniSettingsView::revert);

    showFiles();
}

QWidget* IniSettingsView::createPage(const IniSettings::Category& category)
{
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget(scroll);
    auto* layout = new QVBoxLayout(page);
    auto* note = new QLabel(page);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    for (const auto& setting : category.settings) {
        auto* field = new QWidget(page);
        auto* fieldLayout = new QVBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(2);
        auto* row = new QHBoxLayout;
        row->addWidget(createControl(setting));
        row->addStretch();
        fieldLayout->addLayout(row);
        if (!setting.help.isEmpty()) {
            auto* help = new QLabel(setting.help, field);
            help->setWordWrap(true);
            help->setForegroundRole(QPalette::PlaceholderText);
            fieldLayout->addWidget(help);
        }
        form->addRow(setting.label, field);
        // the label names the INI lines, for people who know them
        QStringList lines;
        for (const auto& key : setting.keys) {
            lines << QString("%1 [%2] %3").arg(m_catalog.file(key.file)->path, key.section, key.key);
        }
        form->labelForField(field)->setToolTip(lines.join('\n'));
        m_refreshers << [this, field, keys = setting.keys] {
            field->setEnabled(std::all_of(keys.cbegin(), keys.cend(), [this](const IniSettings::Key& key) { return m_files.available(key.file); }));
        };
    }
    layout->addLayout(form);
    layout->addStretch();
    m_refreshers << [this, note, category] {
        QStringList reasons;
        for (const auto& setting : category.settings) {
            for (const auto& key : setting.keys) {
                if (m_files.available(key.file)) {
                    continue;
                }
                const auto* file = m_catalog.file(key.file);
                const auto reason = file->missing.isEmpty() || QFileInfo::exists(m_files.path(key.file))
                                        ? tr("%1 could not be read, so its settings are locked.").arg(file->path)
                                        : file->missing;
                if (!reasons.contains(reason)) {
                    reasons << reason;
                }
            }
        }
        note->setText(reasons.join('\n'));
        note->setVisible(!reasons.isEmpty());
    };
    scroll->setWidget(page);
    return scroll;
}

QWidget* IniSettingsView::createControl(const IniSettings::Setting& setting)
{
    QWidget* control = nullptr;
    switch (setting.type) {
        case IniSettings::Setting::Type::Toggle: {
            auto* box = new QCheckBox(this);
            connect(box, &QCheckBox::toggled, this, [this, setting](bool on) { setValues(setting, on ? setting.on : setting.off); });
            m_refreshers << [this, box, setting] {
                const QSignalBlocker block(box);
                box->setChecked(IniSettings::isOn(setting, m_files.values(setting)));
            };
            return box;
        }
        case IniSettings::Setting::Type::Text: {
            auto* edit = new QLineEdit(this);
            edit->setMinimumWidth(320);
            connect(edit, &QLineEdit::editingFinished, this, [this, setting, edit] {
                if (edit->text() != m_files.value(setting.keys.first())) {
                    setValues(setting, { edit->text() });
                }
            });
            m_refreshers << [this, edit, setting] {
                const QSignalBlocker block(edit);
                edit->setText(m_files.value(setting.keys.first()));
            };
            control = edit;
            break;
        }
        case IniSettings::Setting::Type::Color: {
            auto* button = new QPushButton(this);
            connect(button, &QPushButton::clicked, this, [this, setting] {
                const auto current = IniSettings::colorOf(setting, m_files.values(setting));
                const auto chosen = QColorDialog::getColor(current.isValid() ? current : QColor(Qt::white), this, setting.label);
                if (chosen.isValid()) {  // invalid: the dialog was cancelled
                    setValues(setting, IniSettings::colorValues(setting, chosen));
                }
            });
            m_refreshers << [this, button, setting] {
                const auto values = m_files.values(setting);
                const auto color = IniSettings::colorOf(setting, values);
                QPixmap swatch(28, 16);
                swatch.fill(color.isValid() ? color : QColor(Qt::transparent));
                button->setIcon(QIcon(swatch));
                // a value that is no colour is shown as it is; choosing a colour replaces it
                button->setText(color.isValid() ? color.name().toUpper() : tr("Custom (%1)").arg(values.join(", ")));
            };
            control = button;
            break;
        }
        case IniSettings::Setting::Type::Integer: {
            auto* spin = new QSpinBox(this);
            spin->setSingleStep(static_cast<int>(setting.step));
            spin->setSuffix(setting.suffix);
            spin->setGroupSeparatorShown(true);
            connect(spin, &QSpinBox::valueChanged, this, [this, setting](int value) { setValues(setting, { QString::number(value) }); });
            m_refreshers << [this, spin, setting] {
                const auto& key = setting.keys.first();
                const auto value = std::clamp<double>(std::round(number(m_files.value(key), key.defaultValue)), std::numeric_limits<int>::min(),
                                                      std::numeric_limits<int>::max());
                const QSignalBlocker block(spin);
                // a value outside the usual range is shown as it is
                spin->setRange(static_cast<int>(std::min(setting.minimum, value)), static_cast<int>(std::max(setting.maximum, value)));
                spin->setValue(static_cast<int>(value));
            };
            control = spin;
            break;
        }
        case IniSettings::Setting::Type::Decimal: {
            auto* spin = new QDoubleSpinBox(this);
            spin->setDecimals(setting.decimals);
            spin->setSingleStep(setting.step);
            spin->setSuffix(setting.suffix);
            spin->setGroupSeparatorShown(true);
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, setting](double value) {
                // the games write decimals with four places
                setValues(setting, { QString::number(value, 'f', std::max(4, setting.decimals)) });
            });
            m_refreshers << [this, spin, setting] {
                const auto& key = setting.keys.first();
                const auto value = number(m_files.value(key), key.defaultValue);
                const QSignalBlocker block(spin);
                spin->setRange(std::min(setting.minimum, value), std::max(setting.maximum, value));
                spin->setValue(value);
            };
            control = spin;
            break;
        }
        case IniSettings::Setting::Type::Choice: {
            auto* combo = new QComboBox(this);
            for (const auto& option : setting.options) {
                combo->addItem(option.label);
            }
            connect(combo, &QComboBox::activated, this, [this, setting](int index) {
                if (index < setting.options.size()) {
                    setValues(setting, setting.options[index].values);
                }
            });
            m_refreshers << [this, combo, setting] {
                const QSignalBlocker block(combo);
                while (combo->count() > setting.options.size()) {
                    combo->removeItem(combo->count() - 1);
                }
                const auto values = m_files.values(setting);
                auto index = IniSettings::matchingOption(setting, values);
                if (index < 0) {
                    // the files hold a mix no option sets; show it as it is
                    combo->addItem(tr("Custom (%1)").arg(values.join(", ")));
                    index = combo->count() - 1;
                }
                combo->setCurrentIndex(index);
            };
            control = combo;
            break;
        }
        case IniSettings::Setting::Type::Resolution: {
            auto* combo = new QComboBox(this);
            combo->setEditable(true);
            combo->setInsertPolicy(QComboBox::NoInsert);
            combo->setMinimumContentsLength(20);
            for (const auto& [size, screen] : resolutions()) {
                combo->addItem(screen ? tr("%1 (your screen)").arg(sizeText(size)) : sizeText(size));
            }
            connect(combo, &QComboBox::activated, this, [this, setting, combo] { setResolution(setting, combo->currentText()); });
            connect(combo->lineEdit(), &QLineEdit::editingFinished, this, [this, setting, combo] { setResolution(setting, combo->currentText()); });
            m_refreshers << [this, combo, setting] {
                const auto values = m_files.values(setting);
                const QSignalBlocker block(combo);
                combo->setEditText(QString("%1 × %2").arg(values[0], values[1]));
            };
            control = combo;
            break;
        }
    }
    // the mouse wheel scrolls the page; it changes a value only in the control that has focus
    control->setFocusPolicy(Qt::StrongFocus);
    control->installEventFilter(this);
    return control;
}

bool IniSettingsView::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Wheel && !static_cast<QWidget*>(watched)->hasFocus()) {
        event->ignore();  // ignored, the event goes on to the scroll area
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void IniSettingsView::setValues(const IniSettings::Setting& setting, const QStringList& values)
{
    for (qsizetype i = 0; i < setting.keys.size(); i++) {
        m_files.setValue(setting.keys[i], values[i]);
    }
    refresh();
    updateState();
    m_status->setText(m_files.isModified() ? tr("Not saved yet.") : QString());
}

void IniSettingsView::setResolution(const IniSettings::Setting& setting, const QString& text)
{
    static const QRegularExpression size("(\\d{3,5})\\s*[x×X*]\\s*(\\d{3,5})");
    const auto match = size.match(text);
    if (!match.hasMatch()) {
        refresh();
        m_status->setText(tr("Type the resolution as width x height, like 1920 x 1080."));
        return;
    }
    setValues(setting, { match.captured(1), match.captured(2) });
}

void IniSettingsView::refresh()
{
    for (const auto& refresher : m_refreshers) {
        refresher();
    }
}

void IniSettingsView::showFiles()
{
    bool anyPreset = false;
    for (qsizetype i = 0; i < m_presets.size(); i++) {
        const auto exists = QFileInfo::exists(FS::PathCombine(m_instance->gameRoot(), m_catalog.presets[i]));
        m_presets[i]->setVisible(exists);
        anyPreset = anyPreset || exists;
    }
    m_presetBar->setVisible(anyPreset);
    refresh();
    updateState();
    m_status->setText(m_files.errors().join('\n'));
}

void IniSettingsView::reload()
{
    if (!m_files.isModified()) {
        m_files.load();
        showFiles();
    }
}

Result<> IniSettingsView::save()
{
    TRY(m_files.save());
    showFiles();
    m_status->setText(tr("Saved at %1.").arg(QTime::currentTime().toString(Qt::TextDate)));
    return {};
}

void IniSettingsView::revert()
{
    m_files.load();
    showFiles();
    if (m_status->text().isEmpty()) {
        m_status->setText(tr("Loaded the files again."));
    }
}

void IniSettingsView::updateState()
{
    const auto modified = m_files.isModified();
    m_save->setEnabled(modified);
    m_revert->setEnabled(modified);
}

bool IniSettingsView::maybeSave()
{
    if (!m_files.isModified()) {
        return true;
    }
    const auto answer = QMessageBox::question(this, tr("Unsaved INI changes"), tr("Save your changes to the game's settings?"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) {
        return false;
    }
    if (answer == QMessageBox::Discard) {
        revert();
        return true;
    }
    if (const auto saved = save(); !saved) {
        QMessageBox::warning(this, tr("Unsaved INI changes"), saved.error());
        return false;
    }
    return true;
}
