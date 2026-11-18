#include "InstanceTask.h"

QString InstanceTask::name() const
{
    if (!m_modifiedName.isEmpty()) {
        return modifiedName();
    }
    if (!m_originalVersion.isEmpty()) {
        return QString("%1 %2").arg(m_originalName, m_originalVersion);
    }

    return m_originalName;
}

QString InstanceTask::originalName() const
{
    return m_originalName;
}

QString InstanceTask::modifiedName() const
{
    if (!m_modifiedName.isEmpty()) {
        return m_modifiedName;
    }
    return m_originalName;
}

QString InstanceTask::version() const
{
    return m_originalVersion;
}

void InstanceTask::setOriginalName(const QString& name, const QString& version)
{
    m_originalName = name;
    m_originalVersion = version;
}
