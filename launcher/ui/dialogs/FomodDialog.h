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

#include "game/Fomod.h"

class QAbstractButton;
class QLabel;
class QPushButton;
class QScrollArea;
class QTextBrowser;
class QVBoxLayout;

/// Walks the user through a FOMOD installer's steps and collects the files to install
class FomodDialog : public QDialog {
    Q_OBJECT
   public:
    FomodDialog(Fomod::Module module, QString root, Fomod::Context context, const QString& modName, QWidget* parent = nullptr);
    ~FomodDialog() override;

    /// Files to install, valid after the dialog was accepted
    QList<Fomod::FileItem> files() const;
    /// The options picked, valid after the dialog was accepted
    QList<Fomod::Selection> selections() const { return m_selections; }

   private:
    void showStep(int index);
    void collectStep();
    bool validateStep(QString* error) const;
    int nextVisibleStep(int after) const;
    Fomod::Context contextBefore(int step) const;
    void showPluginInfo(int group, int plugin);
    void goNext();
    void goBack();

    Fomod::Module m_module;
    QString m_root;
    Fomod::Context m_base;
    QList<Fomod::Selection> m_selections;
    QList<int> m_history;
    int m_current = -1;

    QLabel* m_stepLabel = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_groups = nullptr;
    QVBoxLayout* m_groupsLayout = nullptr;
    QLabel* m_image = nullptr;
    QTextBrowser* m_description = nullptr;
    QPushButton* m_back = nullptr;
    QPushButton* m_next = nullptr;
    QList<QList<QAbstractButton*>> m_buttons;
};
