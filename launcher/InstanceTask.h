#pragma once
#include <QDir>
#include "settings/SettingsObject.h"
#include "tasks/Task.h"

class InstanceTask : public Task {
    Q_OBJECT
   public:
    InstanceTask() = default;
    ~InstanceTask() override = default;

    void setParentSettings(SettingsObject* settings) { m_globalSettings = settings; }
    void setStagingPath(const QString& stagingPath) { m_stagingPath = stagingPath; }

    void setIcon(const QString& icon) { m_instIcon = icon; }
    void setGroup(const QString& group) { m_instGroup = group; }
    QString group() const { return m_instGroup; }

    /// The instance folder to create the instance in; empty = the primary one
    void setTargetDir(const QString& dir) { m_targetDir = dir; }
    QString targetDir() const { return m_targetDir; }

    QString modifiedName() const;
    QString originalName() const;
    QString name() const;
    QString version() const;

    void setName(const QString& name) { m_modifiedName = name; }
    void setOriginalName(const QString& name, const QString& version);

   protected: /* data */
    SettingsObject* m_globalSettings{};
    QString m_instIcon;
    QString m_instGroup;
    QString m_targetDir;
    QString m_stagingPath;
    QString m_originalName;
    QString m_originalVersion;
    QString m_modifiedName;
};
