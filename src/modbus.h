#pragma once

#include "datasource.h"

#include <QElapsedTimer>
#include <QModbusDataUnit>
#include <QModbusReply>
#include <QModbusTcpClient>
#include <QPointer>
#include <QTimer>
#include <variant>

namespace Modbus {
QString validateSource(const QJsonObject &source);
QString validateTag(const QJsonObject &tag);
QModbusDataUnit::RegisterType table(const QJsonObject &address);
int width(const QJsonObject &address);
bool decode(const QJsonObject &tag, const QList<quint16> &words, double &value);
bool encode(const QJsonObject &tag, double value, QList<quint16> &words, QString &error);
}

class ModbusSource final : public DataSource
{
    Q_OBJECT
public:
    ModbusSource(const QJsonObject &source, const QList<QJsonObject> &tags, QObject *parent);
    void start() override;
    void stop() override;
    bool supportsRead() const override { return true; }
    QString read(const QJsonObject &tag, quint64 token) override;
    QString write(const QJsonObject &tag, double value) override;

private:
    struct Group {
        int unit;
        QModbusDataUnit::RegisterType table;
        int first;
        int count;
        QList<QJsonObject> tags;
        bool onDemand = false;
        quint64 readToken = 0;
    };
    struct Write {
        QModbusDataUnit data;
        int unit;
        QString tagId;
    };
    using Action = std::variant<Group, Write>;

    void poll();
    void next();
    void report(const QList<QJsonObject> &tags, Quality quality, const QString &detail);
    void sendRead(const Group &group);
    void consume(QModbusReply *reply, const Group &group);
    QString sendWrite(const Write &request);
    QString enqueue(Action action);
    void cancelQueue(const QString &detail);
    void cancelRequests(const QString &detail);
    static Group tagGroup(const QJsonObject &tag);
    static QList<Group> groupTags(const QList<QJsonObject> &tags);

    QModbusTcpClient client_;
    QTimer pollTimer_;
    QPointer<QModbusReply> readReply_;
    QPointer<QModbusReply> writeReply_;
    QList<QJsonObject> tags_;
    QList<Group> groups_;
    QList<Group> pending_;
    QList<Action> actions_;
    QString readTagId_;
    quint64 readToken_ = 0;
    QString writeTagId_;
    QElapsedTimer clock_;
    qint64 nextConnect_ = 0;
    qint64 connectDeadline_ = 0;
    int retryMs_ = 250;
    bool running_ = false;
};
