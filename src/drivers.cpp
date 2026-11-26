#include "drivers.h"

#include "modbus.h"
#include "engine.h"
#include <QHash>

const Driver *findDriver(const QString &name)
{
    static const QHash<QString, Driver> drivers{
        {"modbus-tcp", {Modbus::validateSource, Modbus::validateTag,
            [](const QJsonObject &source, const QList<QJsonObject> &tags, QObject *parent) -> DataSource * {
                return new ModbusSource(source, tags, parent);
            }}},
        {"engine-json", {Engine::validateSource, Engine::validateTag,
            [](const QJsonObject &source, const QList<QJsonObject> &tags, QObject *parent) -> DataSource * {
                return new EngineSource(source, tags, parent);
            }}}
    };
    const auto found = drivers.constFind(name);
    return found == drivers.cend() ? nullptr : &found.value();
}
