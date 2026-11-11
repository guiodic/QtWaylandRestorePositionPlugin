// SPDX-License-Identifier: MIT
#include <QGenericPlugin>
#include "persistence.h"

class SessionPersistencePlugin final : public QGenericPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QGenericPluginFactoryInterface_iid FILE "plugin.json")
public:
    QObject *create(const QString &key, const QString &) override
    {
        return key.compare(QStringLiteral("waylandsessionpersistence"), Qt::CaseInsensitive) == 0
            ? createWaylandSessionPersistence() : nullptr;
    }
};
#include "plugin.moc"
