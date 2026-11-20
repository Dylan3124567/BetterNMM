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

#include "FomodDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "Application.h"
#include "game/ModInstaller.h"
#include "settings/SettingsObject.h"

namespace {

/// Shows a plugin's description and picture while the pointer is over its option
class HoverFilter : public QObject {
   public:
    HoverFilter(QObject* parent, std::function<void()> onHover) : QObject(parent), m_onHover(std::move(onHover)) {}

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Enter || event->type() == QEvent::FocusIn) {
            m_onHover();
        }
        return QObject::eventFilter(watched, event);
    }

   private:
    std::function<void()> m_onHover;
};

}  // namespace

FomodDialog::FomodDialog(Fomod::Module module,
                         QString root,
                         Fomod::Context context,
                         const QString& modName,
                         QWidget* parent,
                         QList<Fomod::Selection> preset)
    : QDialog(parent), m_module(std::move(module)), m_root(std::move(root)), m_base(std::move(context)), m_preset(std::move(preset))
{
    setWindowTitle(tr("Install %1").arg(m_module.name.isEmpty() ? modName : m_module.name));
    auto* layout = new QVBoxLayout(this);

    m_stepLabel = new QLabel(this);
    auto font = m_stepLabel->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.2);
    m_stepLabel->setFont(font);
    layout->addWidget(m_stepLabel);

    auto* splitter = new QSplitter(this);
    m_scroll = new QScrollArea(splitter);
    m_scroll->setWidgetResizable(true);
    m_groups = new QWidget(m_scroll);
    m_groupsLayout = new QVBoxLayout(m_groups);
    m_scroll->setWidget(m_groups);
    splitter->addWidget(m_scroll);

    auto* info = new QWidget(splitter);
    auto* infoLayout = new QVBoxLayout(info);
    m_image = new QLabel(info);
    m_image->setAlignment(Qt::AlignCenter);
    m_image->setMinimumHeight(200);
    infoLayout->addWidget(m_image, 1);
    m_description = new QTextBrowser(info);
    m_description->setOpenExternalLinks(true);
    infoLayout->addWidget(m_description, 1);
    splitter->addWidget(info);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    auto* buttons = new QDialogButtonBox(this);
    m_back = buttons->addButton(tr("< &Back"), QDialogButtonBox::ActionRole);
    m_next = buttons->addButton(tr("&Next >"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(m_back, &QPushButton::clicked, this, &FomodDialog::goBack);
    connect(m_next, &QPushButton::clicked, this, &FomodDialog::goNext);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    disconnect(buttons, &QDialogButtonBox::accepted, nullptr, nullptr);
    layout->addWidget(buttons);

    if (!m_module.image.isEmpty()) {
        if (const auto path = ModInstaller::resolvePath(m_root, m_module.image); !path.isEmpty()) {
            m_image->setPixmap(QPixmap(path).scaled(480, 270, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }

    resize(900, 600);
    restoreGeometry(QByteArray::fromBase64(APPLICATION->settings()->get("FomodGeometry").toByteArray()));
    showStep(nextVisibleStep(-1));
}

FomodDialog::~FomodDialog()
{
    APPLICATION->settings()->set("FomodGeometry", QString::fromUtf8(saveGeometry().toBase64()));
}

Fomod::Context FomodDialog::contextBefore(int step) const
{
    QList<Fomod::Selection> earlier;
    for (const auto& selection : m_selections) {
        if (selection.step < step) {
            earlier << selection;
        }
    }
    auto context = m_base;
    context.flags = Fomod::flagsFor(m_module, earlier, m_base);
    return context;
}

int FomodDialog::nextVisibleStep(int after) const
{
    for (int i = after + 1; i < m_module.steps.size(); i++) {
        if (Fomod::stepVisible(m_module.steps[i], contextBefore(i))) {
            return i;
        }
    }
    return -1;
}

void FomodDialog::showStep(int index)
{
    m_current = index;
    m_buttons.clear();
    while (auto* item = m_groupsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_back->setEnabled(!m_history.isEmpty());

    if (index < 0) {
        m_stepLabel->setText(tr("Ready to install"));
        m_groupsLayout->addWidget(new QLabel(tr("The installer has everything it needs. Click Install to finish."), m_groups));
        m_groupsLayout->addStretch(1);
        m_next->setText(tr("&Install"));
        return;
    }

    const auto& step = m_module.steps[index];
    m_stepLabel->setText(step.name);
    const auto context = contextBefore(index);
    const bool firstVisit = std::none_of(m_selections.begin(), m_selections.end(), [index](const Fomod::Selection& s) { return s.step == index; });
    if (firstVisit) {
        m_selections << Fomod::startingSelections(m_module, index, context, m_preset);
    }

    for (int g = 0; g < step.groups.size(); g++) {
        const auto& group = step.groups[g];
        auto* box = new QGroupBox(group.name, m_groups);
        auto* boxLayout = new QVBoxLayout(box);
        QList<QAbstractButton*> buttons;
        auto* exclusive = new QButtonGroup(box);
        exclusive->setExclusive(group.type == Fomod::GroupType::SelectExactlyOne);
        for (int p = 0; p < group.plugins.size(); p++) {
            const auto& plugin = group.plugins[p];
            QAbstractButton* button = nullptr;
            if (group.type == Fomod::GroupType::SelectExactlyOne) {
                button = new QRadioButton(plugin.name, box);
                exclusive->addButton(button);
            } else {
                button = new QCheckBox(plugin.name, box);
            }
            const auto type = Fomod::pluginType(plugin, context);
            const bool selected = std::any_of(m_selections.begin(), m_selections.end(),
                                              [&](const Fomod::Selection& s) { return s.step == index && s.group == g && s.plugin == p; });
            button->setChecked(selected);
            if (type == Fomod::PluginType::Required || group.type == Fomod::GroupType::SelectAll) {
                button->setChecked(true);
                button->setEnabled(false);
            } else if (type == Fomod::PluginType::NotUsable) {
                button->setChecked(false);
                button->setEnabled(false);
                button->setToolTip(tr("Not usable with your setup."));
            }
            if (group.type == Fomod::GroupType::SelectAtMostOne) {
                // at most one: behaves like radio buttons that can all be off
                connect(button, &QAbstractButton::toggled, this, [this, g, button](bool checked) {
                    if (!checked) {
                        return;
                    }
                    for (auto* other : m_buttons.value(g)) {
                        if (other != button && other->isEnabled()) {
                            other->setChecked(false);
                        }
                    }
                });
            }
            button->installEventFilter(new HoverFilter(button, [this, g, p] { showPluginInfo(g, p); }));
            connect(button, &QAbstractButton::toggled, this, [this, g, p] { showPluginInfo(g, p); });
            boxLayout->addWidget(button);
            buttons << button;
        }
        m_buttons << buttons;
        m_groupsLayout->addWidget(box);
    }
    m_groupsLayout->addStretch(1);
    // the splitter sized this column from its content before any step was shown; every option has to be readable
    const auto needed = m_groups->sizeHint().width() + m_scroll->verticalScrollBar()->sizeHint().width() + 2 * m_scroll->frameWidth();
    m_scroll->setMinimumWidth(std::min(needed, width() / 2));
    m_next->setText(nextVisibleStep(index) < 0 ? tr("&Install") : tr("&Next >"));
    if (!step.groups.isEmpty() && !step.groups.first().plugins.isEmpty()) {
        showPluginInfo(0, 0);
    }
}

void FomodDialog::showPluginInfo(int group, int plugin)
{
    if (m_current < 0) {
        return;
    }
    const auto& entry = m_module.steps[m_current].groups[group].plugins[plugin];
    m_description->setPlainText(entry.description);
    if (!entry.image.isEmpty()) {
        if (const auto path = ModInstaller::resolvePath(m_root, entry.image); !path.isEmpty()) {
            m_image->setPixmap(QPixmap(path).scaled(m_image->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            return;
        }
    }
    m_image->clear();
}

void FomodDialog::collectStep()
{
    if (m_current < 0) {
        return;
    }
    m_selections.removeIf([this](const Fomod::Selection& s) { return s.step >= m_current; });
    for (int g = 0; g < m_buttons.size(); g++) {
        for (int p = 0; p < m_buttons[g].size(); p++) {
            if (m_buttons[g][p]->isChecked()) {
                m_selections << Fomod::Selection{ .step = m_current, .group = g, .plugin = p };
            }
        }
    }
}

bool FomodDialog::validateStep(QString* error) const
{
    if (m_current < 0) {
        return true;
    }
    const auto& step = m_module.steps[m_current];
    for (int g = 0; g < m_buttons.size(); g++) {
        const auto checked = std::count_if(m_buttons[g].begin(), m_buttons[g].end(), [](QAbstractButton* b) { return b->isChecked(); });
        const auto& group = step.groups[g];
        if ((group.type == Fomod::GroupType::SelectExactlyOne && checked != 1) ||
            (group.type == Fomod::GroupType::SelectAtLeastOne && checked < 1)) {
            *error = tr("Choose an option in \"%1\".").arg(group.name);
            return false;
        }
    }
    return true;
}

void FomodDialog::goNext()
{
    QString error;
    if (!validateStep(&error)) {
        QMessageBox::warning(this, windowTitle(), error);
        return;
    }
    collectStep();
    if (m_current < 0) {
        accept();
        return;
    }
    const int next = nextVisibleStep(m_current);
    m_history << m_current;
    if (next < 0) {
        accept();
        return;
    }
    showStep(next);
}

void FomodDialog::goBack()
{
    if (m_history.isEmpty()) {
        return;
    }
    collectStep();
    showStep(m_history.takeLast());
}

QList<Fomod::FileItem> FomodDialog::files() const
{
    return Fomod::filesToInstall(m_module, m_selections, m_base);
}
