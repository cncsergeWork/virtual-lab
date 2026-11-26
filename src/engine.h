#pragma once

#include "datasource.h"
#include <QElapsedTimer>
#include <QSerialPort>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

namespace Engine {
QString validateSource(const QJsonObject &source);
QString validateTag(const QJsonObject &tag);
QJsonValue field(const QJsonObject &packet, const QStringList &path);

class SerialFrames
{
public:
    QList<QByteArray> read(const QByteArray &chunk);
    void clear();
private:
    QByteArray line_;
    bool discarding_ = false;
};
}

class EngineSource final : public DataSource
{
    Q_OBJECT
public:
    EngineSource(const QJsonObject &source, const QList<QJsonObject> &tags, QObject *parent);
    void start() override;
    void stop() override;
    QString write(const QJsonObject &, double) override;

private:
    struct Tag {
        QString id;
        QStringList path;
        QString type;
        double scale;
        double bias;
    };

    void tick();
    void sendHello();
    void receive(const QByteArray &raw);
    void disconnect(const QString &reason);
    void stale(const QString &reason);

    QUrl url_;
    QString port_;
    int timeout_;
    QList<Tag> tags_;
    QWebSocket socket_;
    QSerialPort serial_;
    Engine::SerialFrames frames_;
    QTimer timer_;
    QTimer serialHello_;
    QElapsedTimer clock_;
    qint64 nextConnect_ = 0;
    qint64 opened_ = -1;
    qint64 lastPacket_ = -1;
    QString device_;
    QString session_;
    double lastTs_ = -1;
    bool running_ = false;
    bool disconnecting_ = false;
    bool stale_ = false;
};
