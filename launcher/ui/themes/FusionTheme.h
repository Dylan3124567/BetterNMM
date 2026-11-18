#pragma once

#include "ITheme.h"

class FusionTheme : public ITheme {
   public:
    virtual ~FusionTheme() {}

    QString qtTheme() override;

   protected:
    /// BetterNMM's look on top of Fusion, in the colours of @p palette: flat toolbars with rounded buttons, slim
    /// scrollbars, underlined tabs, accent progress bars
    static QString sleekStyleSheet(const QPalette& palette);
};
