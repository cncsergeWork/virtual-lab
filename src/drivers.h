#pragma once

#include "datasource.h"

struct Driver
{
    QString (*validateSource)(const QJsonObject &source);
    QString (*validateTag)(const QJsonObject &tag);
    DataSource *(*create)(const QJsonObject &source, const QList<QJsonObject> &tags, QObject *parent);
};

const Driver *findDriver(const QString &name);
