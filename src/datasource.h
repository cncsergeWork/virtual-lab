#pragma once

#include <QJsonObject>
#include <QObject>
#include <QVariant>

enum class Quality { Waiting, Good, Stale, Error };

class DataSource : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool supportsRead() const { return false; }
    virtual QString read(const QJsonObject &, quint64)
    {
        return QStringLiteral("Источник передаёт данные сам. Внеочередное чтение недоступно.");
    }
    virtual QString write(const QJsonObject &tag, double value) = 0;

signals:
    void statusChanged(const QString &state, const QString &detail);
    void sample(const QString &tagId, const QVariant &value, Quality quality, const QString &detail);
    void readFinished(const QString &tagId, bool accepted, const QString &detail, quint64 token, double value);
    void writeFinished(const QString &tagId, bool accepted, const QString &detail);
};
