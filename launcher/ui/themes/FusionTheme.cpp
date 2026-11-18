#include "FusionTheme.h"

#include <QPalette>

#include "rainbow.h"

QString FusionTheme::qtTheme()
{
    return "Fusion";
}

QString FusionTheme::sleekStyleSheet(const QPalette& palette)
{
    const auto window = palette.color(QPalette::Window);
    const auto text = palette.color(QPalette::WindowText);
    const auto accent = palette.color(QPalette::Highlight);
    auto shade = [&](qreal amount) { return Rainbow::mix(window, text, amount).name(); };
    QString sheet = R"(
QToolBar { border: none; spacing: 2px; padding: 3px; }
QToolBar::separator:horizontal { width: 1px; margin: 6px 4px; background: @border; }
QToolBar::separator:vertical { height: 1px; margin: 4px 6px; background: @border; }
QToolButton { border: 1px solid transparent; border-radius: 6px; padding: 3px 5px; background: transparent; }
QToolButton:hover { background: @hover; border-color: @border; }
QToolButton:pressed, QToolButton:checked { background: @accentSoft; border-color: @accentBorder; }
QToolButton[popupMode="1"] { padding-right: 18px; }
QToolButton::menu-button { border: none; border-left: 1px solid @border; width: 16px; }
QToolButton::menu-button:hover { background: @pressed; border-top-right-radius: 6px; border-bottom-right-radius: 6px; }
/* the instance window's Launch: the one main action, in the accent colour */
QToolButton#launchButton { background: @accent; color: @accentText; border: 1px solid @accent; border-radius: 6px; padding: 5px 12px;
    padding-right: 24px; font-weight: 600; }
QToolButton#launchButton:hover { background: @accentHover; border-color: @accentHover; }
QToolButton#launchButton:pressed { background: @accentBorder; }
QToolButton#launchButton:disabled { background: @window; color: @disabledText; border-color: @border; }
QToolButton#launchButton::menu-button { border: none; border-left: 1px solid @accentDeep; width: 18px; }
QPushButton { background: @button; border: 1px solid @border; border-radius: 6px; padding: 5px 14px; }
QPushButton:hover { background: @buttonHover; border-color: @borderStrong; }
QPushButton:pressed { background: @pressed; }
QPushButton:checked { background: @accentSoft; border-color: @accent; }
QPushButton:default { border-color: @accent; }
QPushButton:disabled { color: @disabledText; background: @window; border-color: @border; }
QPushButton:flat { border: none; background: transparent; }
QCheckBox::indicator, QAbstractItemView::indicator { width: 14px; height: 14px; border: 1px solid @borderStrong; border-radius: 4px;
    background: @base; }
QCheckBox::indicator:hover, QAbstractItemView::indicator:hover { border-color: @accent; }
QCheckBox::indicator:checked, QAbstractItemView::indicator:checked { background: @accent; border-color: @accent;
    image: url(:/style/check.svg); }
QCheckBox::indicator:indeterminate, QAbstractItemView::indicator:indeterminate { background: @accent; border-color: @accent;
    image: url(:/style/partial.svg); }
QCheckBox::indicator:disabled, QAbstractItemView::indicator:disabled { border-color: @border; background: @window; }
QCheckBox::indicator:checked:disabled, QAbstractItemView::indicator:checked:disabled { background: @accentSoft; border-color: @accentSoft; }
QRadioButton::indicator { width: 14px; height: 14px; border: 1px solid @borderStrong; border-radius: 8px; background: @base; }
QRadioButton::indicator:hover { border-color: @accent; }
QRadioButton::indicator:checked { border-color: @accent; background: qradialgradient(cx: 0.5, cy: 0.5, radius: 0.5, fx: 0.5, fy: 0.5,
    stop: 0 @accent, stop: 0.5 @accent, stop: 0.6 @base, stop: 1 @base); }
QRadioButton::indicator:disabled { border-color: @border; background: @window; }
QHeaderView::section { background: @window; color: @textDim; border: none; border-right: 1px solid @border;
    border-bottom: 1px solid @border; padding: 4px 6px; }
QAbstractItemView { outline: 0; }
QListView#pageList { border: none; }
QListView#pageList::item { border-radius: 6px; margin: 1px 4px; padding: 2px; }
QListView#pageList::item:hover { background: @hover; }
QListView#pageList::item:selected { background: @accent; color: @accentText; }
QScrollBar:vertical { background: transparent; width: 11px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 0; }
QScrollBar::handle:vertical { background: @handle; border-radius: 3px; min-height: 28px; margin: 2px 3px; }
QScrollBar::handle:horizontal { background: @handle; border-radius: 3px; min-width: 28px; margin: 3px 2px; }
QScrollBar::handle:hover { background: @handleHover; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QMenu { background: @base; border: 1px solid @border; padding: 4px; }
QMenu::item { padding: 5px 22px 5px 12px; border-radius: 4px; }
QMenu::item:selected { background: @accent; color: @accentText; }
QMenu::item:disabled { color: @disabledText; }
QMenu::separator { height: 1px; background: @border; margin: 4px 6px; }
QMenuBar::item:selected { background: @hover; border-radius: 4px; }
QToolTip { background: @tipBase; color: @tipText; border: 1px solid @border; padding: 4px 6px; }
QTabWidget::pane { border: 1px solid @border; border-radius: 6px; top: -1px; }
QTabBar::tab { background: transparent; color: @textDim; border: none; border-bottom: 2px solid transparent; padding: 6px 12px;
    margin-right: 2px; }
QTabBar::tab:hover { color: @text; background: @hover; }
QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent; }
QProgressBar { border: 1px solid @border; border-radius: 5px; background: @base; text-align: center; min-height: 14px; }
QProgressBar::chunk { background: @accent; border-radius: 4px; }
QStatusBar::item { border: none; }
)";
    // longest names first, so "@border" never eats the start of "@borderStrong"
    const QList<std::pair<QString, QString>> colors{
        { "@borderStrong", shade(0.28) },
        { "@border", shade(0.16) },
        { "@hover", shade(0.08) },
        { "@pressed", shade(0.14) },
        { "@buttonHover", Rainbow::mix(palette.color(QPalette::Button), text, 0.06).name() },
        { "@button", palette.color(QPalette::Button).name() },
        { "@accentSoft", Rainbow::mix(window, accent, 0.28).name() },
        { "@accentBorder", Rainbow::mix(window, accent, 0.6).name() },
        { "@accentText", palette.color(QPalette::HighlightedText).name() },
        { "@accentHover", Rainbow::mix(accent, Qt::white, 0.18).name() },
        { "@accentDeep", Rainbow::mix(accent, Qt::black, 0.3).name() },
        { "@accent", accent.name() },
        { "@textDim", Rainbow::mix(text, window, 0.35).name() },
        { "@text", text.name() },
        { "@disabledText", Rainbow::mix(text, window, 0.55).name() },
        { "@window", window.name() },
        { "@base", palette.color(QPalette::Base).name() },
        { "@handleHover", shade(0.4) },
        { "@handle", shade(0.24) },
        { "@tipBase", palette.color(QPalette::ToolTipBase).name() },
        { "@tipText", palette.color(QPalette::ToolTipText).name() },
    };
    for (const auto& [name, color] : colors) {
        sheet.replace(name, color);
    }
    return sheet;
}
