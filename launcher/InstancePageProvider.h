#pragma once
#include "game/GameInstance.h"
#include "ui/pages/BasePage.h"
#include "ui/pages/BasePageProvider.h"
#include "ui/pages/instance/GameFilesPage.h"
#include "ui/pages/instance/GameSettingsPage.h"
#include "ui/pages/instance/HealthPage.h"
#include "ui/pages/instance/LogPage.h"
#include "ui/pages/instance/ModsPage.h"
#include "ui/pages/instance/NotesPage.h"
#include "ui/pages/instance/OtherLogsPage.h"
#include "ui/pages/instance/PluginsPage.h"
#include "ui/pages/instance/SavesPage.h"

class InstancePageProvider : protected QObject, public BasePageProvider {
    Q_OBJECT
   public:
    explicit InstancePageProvider(GameInstance* parent) { inst = parent; }

    virtual ~InstancePageProvider() = default;
    virtual QList<BasePage*> getPages() override
    {
        QList<BasePage*> values;
        values.append(new LogPage(inst));
        values.append(new ModsPage(inst));
        values.append(new PluginsPage(inst));
        values.append(new HealthPage(inst));
        values.append(new SavesPage(inst));
        values.append(new GameFilesPage(inst));
        values.append(new NotesPage(inst));
        values.append(new GameSettingsPage(inst));
        values.append(new OtherLogsPage("logs", tr("Other Logs"), "Other-Logs", inst));
        return values;
    }

    virtual QString dialogTitle() override { return tr("Edit Instance (%1)").arg(inst->name()); }

   protected:
    GameInstance* inst;
};
