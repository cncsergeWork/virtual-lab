#pragma once

#include <QJsonObject>
#include <QModbusTcpServer>
#include <QTimer>

class DemoServer final : public QObject
{
    Q_OBJECT
public:
    explicit DemoServer(QObject *parent = nullptr);
    bool listen(int port, QString &error);
    void setPaused(bool paused);
    static QJsonObject project();

private:
    void tick();
    QModbusTcpServer server_;
    QTimer timer_;
    double level_ = 58;
    int ticks_ = 0;
};
