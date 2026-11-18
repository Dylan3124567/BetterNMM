#pragma once

#include <QFileInfo>
#include <QWidget>
#include <optional>

#include "Result.h"

namespace GuiUtil {
void setClipboardText(QString text);
QStringList browseForFiles(const QString& context,
                           const QString& caption,
                           const QString& filter,
                           const QString& defaultPath,
                           QWidget* parentWidget);
QString browseForFile(const QString& context,
                      const QString& caption,
                      const QString& filter,
                      const QString& defaultPath,
                      QWidget* parentWidget);
}  // namespace GuiUtil
