#pragma once

#include "datasource.h"
#include <QHash>
#include <QSqlDatabase>
#include <QTimer>
#include <QVariant>

class Runtime final : public QObject
{
    Q_OBJECT
public:
    explicit Runtime(QObject *parent = nullptr);
    ~Runtime() override;

    bool openArchive(const QString &path, const QString &projectId, QString &error);
    void start(const QJsonObject &project);
    void stop();
    bool running() const { return running_; }
    QVariantMap readings() const { return readings_; }
    QVariantMap statuses() const { return statuses_; }
    QVariantList alarms() const;
    QVariantList events();
    QVariantList series(const QString &tag, qint64 from, qint64 to, int buckets);
    QVariantMap archiveBounds(const QStringList &tags);
    QVariantMap archiveSamples(const QStringList &tags, qint64 time);
    QString exportCsv(const QString &path, const QString &tag, qint64 from, qint64 to);
    QString requestRead(const QString &tagId);
    QString requestWrite(const QString &tagId, double value);
    void runWidgetActions(const QString &widgetId, const QVariantList &rows);
    QString acknowledge(const QString &tagId);

signals:
    void updated();
    void alarmsChanged();
    void archiveChanged();
    void warning(const QString &message);
    void writeResult(const QString &tagId, bool accepted, const QString &message);

private:
    struct Row {
        QString tag;
        qint64 time;
        QVariant value;
        QString quality;
    };
    struct Event {
        QString tag;
        QString name;
        qint64 time;
        QString type;
        QString detail;
    };
    struct WidgetAction {
        QString tag;
        QString valueTag;
        double value = 0;
        quint64 targetEpoch = 0;
        quint64 valueEpoch = 0;
        bool read = false;
    };
    struct WidgetQueue {
        QList<WidgetAction> actions;
        bool waitingForRead = false;
    };
    struct WidgetRead {
        QString widgetId;
        WidgetAction action;
    };

    void accept(const QString &tagId, const QVariant &value, Quality quality, const QString &detail);
    void evaluateAlarm(const QJsonObject &tag, double value, qint64 now);
    void log(const QString &tagId, const QString &type, const QString &detail);
    bool flush();
    void closeArchive();
    QString requestRead(const QString &tagId, quint64 token);
    quint64 sourceEpoch(const QString &tagId) const;
    bool actionStillConnected(const WidgetAction &action) const;
    void advanceWidgetActions(const QString &widgetId);
    void finishWidgetRead(quint64 token, bool accepted, double value, const QString &detail);
    void reportWidgetError(const WidgetAction &action, const QString &detail);

    QHash<QString, DataSource *> sources_;
    QHash<QString, QJsonObject> sourceSettings_;
    QHash<QString, QJsonObject> tags_;
    QHash<QString, QVariantMap> activeAlarms_;
    QHash<QString, Row> lastArchived_;
    QHash<QString, quint64> sourceEpochs_;
    QHash<QString, WidgetQueue> widgetQueues_;
    QHash<quint64, WidgetRead> widgetReads_;
    QVariantMap readings_;
    QVariantMap statuses_;
    QList<Row> rows_;
    QList<Event> events_;
    QSqlDatabase db_;
    QString owner_;
    QTimer timer_;
    quint64 nextReadToken_ = 0;
    bool running_ = false;
    bool dirty_ = false;
    bool archiveError_ = false;
};
