#include "runtime.h"

#include "drivers.h"
#include "project.h"
#include <QDateTime>
#include <QJsonArray>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QTimeZone>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
QString qualityName(Quality quality)
{
    switch (quality) {
    case Quality::Waiting: return "waiting";
    case Quality::Good: return "good";
    case Quality::Stale: return "stale";
    case Quality::Error: return "error";
    }
    return "error";
}

QDateTime utc(qint64 time)
{
    return QDateTime::fromMSecsSinceEpoch(time, QTimeZone::UTC);
}
}

Runtime::Runtime(QObject *parent) : QObject(parent)
{
    timer_.setInterval(200);
    connect(&timer_, &QTimer::timeout, this, [this] {
        flush();
        if (std::exchange(dirty_, false))
            emit updated();
    });
    timer_.start();
}

Runtime::~Runtime()
{
    stop();
    closeArchive();
}

bool Runtime::openArchive(const QString &path, const QString &projectId, QString &error)
{
    if (running_) {
        error = "Остановите опрос перед сменой архива.";
        return false;
    }
    if (!flush()) {
        error = "Не удалось сохранить накопленные данные архива.";
        return false;
    }
    const auto connection = Project::id();
    auto next = QSqlDatabase::addDatabase("QSQLITE", connection);
    next.setDatabaseName(path.isEmpty() ? QString(":memory:") : path);
    next.setConnectOptions("QSQLITE_BUSY_TIMEOUT=500");
    bool ready = next.open();
    if (!ready) {
        error = next.lastError().text();
    } else {
        QSqlQuery query(next);
        ready = query.exec("CREATE TABLE IF NOT EXISTS samples ("
                           "id INTEGER PRIMARY KEY, owner TEXT NOT NULL, tag TEXT NOT NULL, "
                           "time INTEGER NOT NULL, value REAL, quality TEXT NOT NULL)")
            && query.exec("CREATE INDEX IF NOT EXISTS samples_lookup ON samples(owner, tag, time, id)")
            && query.exec("CREATE TABLE IF NOT EXISTS events ("
                          "id INTEGER PRIMARY KEY, owner TEXT NOT NULL, tag TEXT NOT NULL, "
                          "name TEXT NOT NULL, time INTEGER NOT NULL, type TEXT NOT NULL, detail TEXT NOT NULL)")
            && query.exec("CREATE INDEX IF NOT EXISTS events_lookup ON events(owner, time, id)");
        if (!ready)
            error = query.lastError().text();
    }
    if (!ready) {
        next.close();
        next = {};
        QSqlDatabase::removeDatabase(connection);
        return false;
    }
    closeArchive();
    db_ = next;
    owner_ = projectId;
    const bool clearedAlarms = !activeAlarms_.isEmpty();
    activeAlarms_.clear();
    readings_.clear();
    statuses_.clear();
    dirty_ = false;
    error.clear();
    emit updated();
    if (clearedAlarms)
        emit alarmsChanged();
    emit archiveChanged();
    return true;
}

void Runtime::closeArchive()
{
    if (!db_.isValid())
        return;
    const auto name = db_.connectionName();
    db_.close();
    db_ = {};
    QSqlDatabase::removeDatabase(name);
}

void Runtime::start(const QJsonObject &project)
{
    if (running_)
        return;
    tags_.clear();
    readings_.clear();
    statuses_.clear();
    sourceSettings_.clear();
    sourceEpochs_.clear();
    const bool clearedAlarms = !activeAlarms_.isEmpty();
    activeAlarms_.clear();
    lastArchived_.clear();
    QHash<QString, QList<QJsonObject>> bySource;
    for (const auto &value : project.value("tags").toArray()) {
        const auto tag = value.toObject();
        const auto id = tag.value("id").toString();
        tags_.insert(id, tag);
        bySource[tag.value("source").toString()].append(tag);
        readings_.insert(id, QVariantMap{{"quality", "waiting"}, {"hasValue", false}});
    }
    running_ = true;
    for (const auto &value : project.value("sources").toArray()) {
        const auto source = value.toObject();
        const auto id = source.value("id").toString();
        const auto *definition = findDriver(source.value("driver").toString());
        if (!definition)
            continue;
        auto *driver = definition->create(source, bySource.value(id), this);
        sources_.insert(id, driver);
        sourceSettings_.insert(id, source.value("settings").toObject());
        connect(driver, &DataSource::sample, this, &Runtime::accept);
        connect(driver, &DataSource::statusChanged, this, [this, id](const QString &state, const QString &detail) {
            if ((state == "disconnected" || state == "connected") && statuses_.value(id).toMap().value("state") != state)
                ++sourceEpochs_[id];
            statuses_.insert(id, QVariantMap{{"state", state}, {"detail", detail}});
            dirty_ = true;
        });
        connect(driver, &DataSource::readFinished, this,
                [this](const QString &id, bool accepted, const QString &detail, quint64 token, double value) {
            log(id, accepted ? "read_ok" : "read_error", detail);
            if (token) {
                QMetaObject::invokeMethod(this, [this, token, accepted, value, detail] {
                    finishWidgetRead(token, accepted, value, detail);
                }, Qt::QueuedConnection);
            } else if (!accepted) {
                emit warning(QString("Не удалось прочитать тег «%1». %2").arg(tags_.value(id).value("name").toString(), detail));
            }
        });
        connect(driver, &DataSource::writeFinished, this,
                [this](const QString &id, bool accepted, const QString &detail) {
            log(id, accepted ? "write_ok" : "write_error", detail);
            emit writeResult(id, accepted, detail);
        });
        driver->start();
    }
    emit updated();
    if (clearedAlarms)
        emit alarmsChanged();
}

void Runtime::stop()
{
    if (!running_)
        return;
    widgetQueues_.clear();
    widgetReads_.clear();
    const auto drivers = std::exchange(sources_, {});
    for (auto *driver : drivers) {
        driver->stop();
        delete driver;
    }
    for (auto it = tags_.cbegin(); it != tags_.cend(); ++it) {
        if (readings_.value(it.key()).toMap().value("hasValue").toBool())
            accept(it.key(), 0, Quality::Stale, "Опрос остановлен.");
    }
    running_ = false;
    statuses_.clear();
    flush();
    emit updated();
}

void Runtime::accept(const QString &tagId, const QVariant &value, Quality quality, const QString &detail)
{
    const auto tag = tags_.value(tagId);
    if (tag.isEmpty())
        return;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const auto state = qualityName(quality);
    auto current = readings_.value(tagId).toMap();
    current.insert("quality", state);
    current.insert("detail", detail);
    if (quality == Quality::Good) {
        current.insert("value", value);
        current.insert("hasValue", true);
        current.insert("time", now);
        if (!tag.value("alarm").isUndefined())
            evaluateAlarm(tag, value.toDouble(), now);
    }
    readings_.insert(tagId, current);
    const auto interval = tag.value("archiveMs").toInt();
    const auto last = lastArchived_.value(tagId);
    if (interval > 0 && ((last.quality != state && (quality == Quality::Good || !last.quality.isEmpty()))
                         || (quality == Quality::Good && now - last.time >= interval))) {
        Row row{tagId, now, quality == Quality::Good ? value : QVariant{}, state};
        rows_.append(row);
        lastArchived_.insert(tagId, row);
    }
    dirty_ = true;
}

void Runtime::evaluateAlarm(const QJsonObject &tag, double value, qint64 now)
{
    const auto alarm = tag.value("alarm").toObject();
    if (alarm.isEmpty())
        return;
    const auto id = tag.value("id").toString();
    const auto limit = alarm.value("limit").toDouble();
    const auto hysteresis = alarm.value("hysteresis").toDouble();
    const bool high = alarm.value("direction").toString() == "high";
    const bool active = activeAlarms_.contains(id);
    if (!active && (high ? value >= limit : value <= limit)) {
        activeAlarms_.insert(id, {{"tag", id}, {"name", tag.value("name").toString()},
                                  {"time", now}, {"ack", false}, {"high", high}, {"limit", limit}});
        log(id, "alarm_on", QString("%1 %2").arg(high ? "Верхний порог" : "Нижний порог", QString::number(limit)));
        emit alarmsChanged();
    } else if (active && (high ? value < limit - hysteresis : value > limit + hysteresis)) {
        activeAlarms_.remove(id);
        log(id, "alarm_off", "Значение вернулось в допустимый диапазон.");
        emit alarmsChanged();
    }
}

QString Runtime::acknowledge(const QString &tagId)
{
    if (!activeAlarms_.contains(tagId))
        return "Эта тревога уже снята.";
    auto &alarm = activeAlarms_[tagId];
    if (!alarm.value("ack").toBool()) {
        alarm.insert("ack", true);
        log(tagId, "alarm_ack", "Тревога квитирована.");
        emit alarmsChanged();
        emit updated();
    }
    return {};
}

QVariantList Runtime::alarms() const
{
    auto values = activeAlarms_.values();
    std::sort(values.begin(), values.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value("time").toLongLong() > b.value("time").toLongLong();
    });
    QVariantList result;
    for (const auto &value : values)
        result.append(value);
    return result;
}

QString Runtime::requestRead(const QString &tagId)
{
    return requestRead(tagId, 0);
}

QString Runtime::requestRead(const QString &tagId, quint64 token)
{
    const auto tag = tags_.value(tagId);
    if (tag.isEmpty())
        return "Тег не найден.";
    const auto sourceId = tag.value("source").toString();
    if (!running_ || !sources_.contains(sourceId))
        return "Запустите опрос источника.";
    const auto error = sources_.value(sourceId)->read(tag, token);
    if (error.isEmpty())
        log(tagId, "read_request", "Запрошено внеочередное чтение тега.");
    return error;
}

quint64 Runtime::sourceEpoch(const QString &tagId) const
{
    return sourceEpochs_.value(tags_.value(tagId).value("source").toString());
}

bool Runtime::actionStillConnected(const WidgetAction &action) const
{
    return sourceEpoch(action.tag) == action.targetEpoch
        && (action.valueTag.isEmpty() || sourceEpoch(action.valueTag) == action.valueEpoch);
}

void Runtime::runWidgetActions(const QString &widgetId, const QVariantList &rows)
{
    if (!running_ || widgetId.isEmpty() || rows.isEmpty())
        return;
    QList<WidgetAction> actions;
    for (const auto &entry : rows) {
        const auto row = entry.toMap();
        const auto operation = row.value("action").toString();
        const auto tag = row.value("tag").toString();
        const auto valueTag = row.value("valueTag").toString();
        const bool read = operation == "read";
        bool numeric = false;
        const double value = row.value("value").toDouble(&numeric);
        if (tag.isEmpty() || (!read && operation != "write")
            || (!read && valueTag.isEmpty() && (row.contains("valueTag") || !numeric || !std::isfinite(value)))) {
            emit warning("Проверьте тег и значение действия виджета.");
            continue;
        }
        actions.append(WidgetAction{tag, valueTag, value, sourceEpoch(tag), sourceEpoch(valueTag), read});
    }
    if (actions.isEmpty() || !running_)
        return;
    auto &queue = widgetQueues_[widgetId];
    constexpr qsizetype maxWidgetActions = 128;
    if (queue.actions.size() + actions.size() > maxWidgetActions) {
        if (queue.actions.isEmpty() && !queue.waitingForRead)
            widgetQueues_.remove(widgetId);
        emit warning("Очередь действий виджета заполнена.");
        return;
    }
    queue.actions.append(actions);
    advanceWidgetActions(widgetId);
}

void Runtime::advanceWidgetActions(const QString &widgetId)
{
    while (running_) {
        const auto queue = widgetQueues_.find(widgetId);
        if (queue == widgetQueues_.end() || queue->waitingForRead)
            return;
        if (queue->actions.isEmpty()) {
            widgetQueues_.erase(queue);
            return;
        }
        const auto action = queue->actions.takeFirst();
        if (!actionStillConnected(action)) {
            reportWidgetError(action, "Команда отменена после потери связи.");
            continue;
        }
        if (action.read || action.valueTag.isEmpty()) {
            reportWidgetError(action, action.read ? requestRead(action.tag) : requestWrite(action.tag, action.value));
            continue;
        }

        const auto origin = tags_.value(action.valueTag);
        auto *driver = sources_.value(origin.value("source").toString());
        if (!driver || origin.value("address").toObject().value("type") == "text") {
            reportWidgetError(action, "Выберите числовой или логический тег для значения.");
            continue;
        }
        if (driver->supportsRead()) {
            const auto token = ++nextReadToken_;
            const auto error = requestRead(action.valueTag, token);
            if (error.isEmpty()) {
                queue->waitingForRead = true;
                widgetReads_.insert(token, WidgetRead{widgetId, action});
                return;
            }
            reportWidgetError(action, error);
            continue;
        }

        const auto reading = readings_.value(action.valueTag).toMap();
        bool numeric = false;
        const double value = reading.value("value").toDouble(&numeric);
        if (reading.value("quality") != "good" || !reading.value("hasValue").toBool() || !numeric || !std::isfinite(value)) {
            reportWidgetError(action, "Тег значения пока не получил актуальных данных.");
            continue;
        }
        reportWidgetError(action, requestWrite(action.tag, value));
    }
}

void Runtime::finishWidgetRead(quint64 token, bool accepted, double value, const QString &detail)
{
    const auto pending = widgetReads_.take(token);
    if (pending.widgetId.isEmpty())
        return;
    const auto queue = widgetQueues_.find(pending.widgetId);
    if (queue == widgetQueues_.end())
        return;
    queue->waitingForRead = false;
    const auto &action = pending.action;
    if (!actionStillConnected(action))
        reportWidgetError(action, "Команда отменена после потери связи.");
    else if (!accepted || !std::isfinite(value))
        reportWidgetError(action, detail.isEmpty() ? "Не получено актуальное значение." : detail);
    else
        reportWidgetError(action, requestWrite(action.tag, value));
    advanceWidgetActions(pending.widgetId);
}

void Runtime::reportWidgetError(const WidgetAction &action, const QString &detail)
{
    if (detail.isEmpty())
        return;
    if (action.valueTag.isEmpty()) {
        emit warning(detail);
        return;
    }
    const auto message = QString("Не удалось записать тег «%1» из «%2». %3")
        .arg(tags_.value(action.tag).value("name").toString(), tags_.value(action.valueTag).value("name").toString(), detail);
    log(action.tag, "write_error", message);
    emit warning(message);
}

QString Runtime::requestWrite(const QString &tagId, double value)
{
    const auto tag = tags_.value(tagId);
    const auto sourceId = tag.value("source").toString();
    if (!running_ || !sources_.contains(sourceId))
        return "Запустите опрос источника.";
    if (!tag.value("writable").toBool() || !sourceSettings_.value(sourceId).value("allowWrite").toBool())
        return "Разрешите запись в настройках источника и тега.";
    if (readings_.value(tagId).toMap().value("quality").toString() != "good")
        return "Запись доступна после успешного чтения тега.";
    const auto error = sources_.value(sourceId)->write(tag, value);
    if (error.isEmpty())
        log(tagId, "write_request", QString("Запрошена запись значения %1.").arg(QString::number(value, 'g', 12)));
    return error;
}

void Runtime::log(const QString &tagId, const QString &type, const QString &detail)
{
    events_.append({tagId, tags_.value(tagId).value("name").toString(),
                    QDateTime::currentMSecsSinceEpoch(), type, detail});
}

bool Runtime::flush()
{
    if (rows_.isEmpty() && events_.isEmpty())
        return true;
    bool ok = db_.isOpen() && db_.transaction();
    if (ok) {
        QSqlQuery query(db_);
        ok = query.prepare("INSERT INTO samples(owner,tag,time,value,quality) VALUES(?,?,?,?,?)");
        for (const auto &row : std::as_const(rows_)) {
            if (!ok)
                break;
            query.bindValue(0, owner_);
            query.bindValue(1, row.tag);
            query.bindValue(2, row.time);
            query.bindValue(3, row.value);
            query.bindValue(4, row.quality);
            ok = query.exec();
        }
        if (ok)
            ok = query.prepare("INSERT INTO events(owner,tag,name,time,type,detail) VALUES(?,?,?,?,?,?)");
        for (const auto &event : std::as_const(events_)) {
            if (!ok)
                break;
            query.bindValue(0, owner_);
            query.bindValue(1, event.tag);
            query.bindValue(2, event.name);
            query.bindValue(3, event.time);
            query.bindValue(4, event.type);
            query.bindValue(5, event.detail);
            ok = query.exec();
        }
        query.finish();
        ok = ok && db_.commit();
        if (!ok)
            db_.rollback();
    }
    if (!ok) {
        if (!std::exchange(archiveError_, true))
            emit warning("Ошибка архива SQLite. Проверьте доступ к папке проекта и свободное место.");
        return false;
    }
    archiveError_ = false;
    rows_.clear();
    events_.clear();
    emit archiveChanged();
    return true;
}

QVariantList Runtime::events()
{
    QVariantList result;
    if (!db_.isOpen())
        return result;
    QSqlQuery query(db_);
    query.prepare("SELECT tag,name,time,type,detail FROM events WHERE owner=? ORDER BY time DESC,id DESC LIMIT 300");
    query.addBindValue(owner_);
    if (query.exec()) {
        while (query.next())
            result.append(QVariantMap{{"tag", query.value(0)}, {"name", query.value(1)},
                                      {"time", query.value(2)}, {"type", query.value(3)}, {"detail", query.value(4)}});
    }
    return result;
}

QVariantList Runtime::series(const QString &tag, qint64 from, qint64 to, int buckets)
{
    QVariantList result;
    if (!db_.isOpen() || tag.isEmpty() || from >= to)
        return result;
    const auto step = std::max<qint64>(1, (to - from) / std::clamp(buckets, 16, 1500));
    QSqlQuery query(db_);
    query.prepare("SELECT MIN(time), MIN(value), MAX(value), COUNT(*)-COUNT(value), AVG(value) "
                  "FROM samples WHERE owner=? AND tag=? AND time BETWEEN ? AND ? "
                  "GROUP BY (time - ?) / ? ORDER BY MIN(time)");
    query.addBindValue(owner_);
    query.addBindValue(tag);
    query.addBindValue(from);
    query.addBindValue(to);
    query.addBindValue(from);
    query.addBindValue(step);
    if (query.exec()) {
        while (query.next())
            result.append(QVariantMap{{"time", query.value(0)}, {"min", query.value(1)}, {"max", query.value(2)},
                                      {"gap", query.value(3).toInt() > 0}, {"hasValue", !query.isNull(4)},
                                      {"value", query.value(4)}});
    }
    return result;
}

QVariantMap Runtime::archiveBounds(const QStringList &tags)
{
    if (!db_.isOpen() || tags.isEmpty())
        return {};
    QSqlQuery first(db_), last(db_);
    if (!first.prepare("SELECT time FROM samples WHERE owner=? AND tag=? ORDER BY time,id LIMIT 1")
        || !last.prepare("SELECT time FROM samples WHERE owner=? AND tag=? ORDER BY time DESC,id DESC LIMIT 1"))
        return {};
    qint64 lower = 0, upper = 0;
    bool found = false;
    for (const auto &tag : tags) {
        first.bindValue(0, owner_);
        first.bindValue(1, tag);
        last.bindValue(0, owner_);
        last.bindValue(1, tag);
        if (first.exec() && first.next() && last.exec() && last.next()) {
            const auto start = first.value(0).toLongLong();
            const auto end = last.value(0).toLongLong();
            lower = found ? std::min(lower, start) : start;
            upper = found ? std::max(upper, end) : end;
            found = true;
        }
        first.finish();
        last.finish();
    }
    return found ? QVariantMap{{"from", lower}, {"to", upper}} : QVariantMap{};
}

QVariantMap Runtime::archiveSamples(const QStringList &tags, qint64 time)
{
    QVariantMap result;
    if (!db_.isOpen() || tags.isEmpty())
        return result;
    QSqlQuery query(db_);
    if (!query.prepare("SELECT time,value,quality FROM samples WHERE owner=? AND tag=? AND time<=? "
                       "ORDER BY time DESC,id DESC LIMIT 1"))
        return result;
    for (const auto &tag : tags) {
        query.bindValue(0, owner_);
        query.bindValue(1, tag);
        query.bindValue(2, time);
        if (query.exec() && query.next()) {
            QVariantMap sample{{"time", query.value(0)}, {"quality", query.value(2)}, {"hasValue", !query.isNull(1)}};
            if (!query.isNull(1))
                sample.insert("value", query.value(1));
            result.insert(tag, sample);
        }
        query.finish();
    }
    return result;
}

QString Runtime::exportCsv(const QString &path, const QString &tag, qint64 from, qint64 to)
{
    if (tag.isEmpty() || from >= to || !db_.isOpen())
        return "Выберите тег и диапазон архива.";
    if (!flush())
        return "Не удалось сохранить накопленные данные архива.";
    QSqlQuery query(db_);
    query.setForwardOnly(true);
    query.prepare("SELECT time,value,quality FROM samples WHERE owner=? AND tag=? AND time BETWEEN ? AND ? "
                  "ORDER BY time,id");
    query.addBindValue(owner_);
    query.addBindValue(tag);
    query.addBindValue(from);
    query.addBindValue(to);
    if (!query.exec())
        return query.lastError().text();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return file.errorString();
    QTextStream stream(&file);
    stream << "timestamp_utc,value,quality\r\n";
    while (query.next()) {
        stream << utc(query.value(0).toLongLong()).toString(Qt::ISODateWithMs) << ',';
        if (!query.isNull(1))
            stream << QString::number(query.value(1).toDouble(), 'g', 17);
        stream << ',' << query.value(2).toString() << "\r\n";
    }
    if (query.lastError().isValid())
        return query.lastError().text();
    stream.flush();
    if (stream.status() != QTextStream::Ok || !file.commit())
        return file.errorString();
    return {};
}
