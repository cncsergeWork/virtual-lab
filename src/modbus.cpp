#include "modbus.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QRegularExpression>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

namespace {
bool integer(const QJsonValue &value, int min, int max)
{
    return value.isDouble() && value.toDouble() == value.toInt(min - 1)
        && value.toInt() >= min && value.toInt() <= max;
}

bool registerBit(QModbusDataUnit::RegisterType table)
{
    return table == QModbusDataUnit::Coils || table == QModbusDataUnit::DiscreteInputs;
}

quint16 swapBytes(quint16 word)
{
    return static_cast<quint16>((word >> 8) | (word << 8));
}

void orderWords(QList<quint16> &words, const QString &order)
{
    if (order == "BADC" || order == "DCBA") {
        for (auto &word : words)
            word = swapBytes(word);
    }
    if (order == "CDAB" || order == "DCBA")
        std::reverse(words.begin(), words.end());
}
}

QModbusDataUnit::RegisterType Modbus::table(const QJsonObject &address)
{
    const auto name = address.value("table").toString();
    if (name == "coils") return QModbusDataUnit::Coils;
    if (name == "discrete") return QModbusDataUnit::DiscreteInputs;
    if (name == "input") return QModbusDataUnit::InputRegisters;
    if (name == "holding") return QModbusDataUnit::HoldingRegisters;
    return QModbusDataUnit::Invalid;
}

int Modbus::width(const QJsonObject &address)
{
    const auto type = address.value("type").toString();
    return type == "u32" || type == "i32" || type == "f32" ? 2 : 1;
}

QString Modbus::validateSource(const QJsonObject &source)
{
    const auto settings = source.value("settings").toObject();
    const auto host = settings.value("host").toString();
    const static QRegularExpression hostname("^[a-zA-Z0-9_](?:[a-zA-Z0-9_.-]{0,252})$");
    if (host.isEmpty() || (QHostAddress(host).isNull() && !hostname.match(host).hasMatch()))
        return QCoreApplication::translate("Modbus", "Укажите IP-адрес или имя узла без номера порта.");
    if (!integer(settings.value("port"), 1, 65535))
        return QCoreApplication::translate("Modbus", "Порт должен быть целым числом от 1 до 65535.");
    if (!integer(settings.value("pollMs"), 100, 60000) || !integer(settings.value("timeoutMs"), 100, 30000))
        return QCoreApplication::translate("Modbus", "Период опроса должен быть 100–60000 мс, тайм-аут — 100–30000 мс.");
    if (!settings.value("allowWrite").isBool())
        return QCoreApplication::translate("Modbus", "Укажите, разрешена ли запись.");
    if (settings.value("simulator").toBool() && host != "127.0.0.1")
        return QCoreApplication::translate("Modbus", "Встроенный симулятор работает только на 127.0.0.1.");
    return {};
}

QString Modbus::validateTag(const QJsonObject &tag)
{
    const auto address = tag.value("address").toObject();
    const auto registerType = table(address);
    const auto type = address.value("type").toString();
    const auto order = address.value("order").toString();
    if (registerType == QModbusDataUnit::Invalid)
        return QCoreApplication::translate("Modbus", "Выберите таблицу Modbus.");
    if (!integer(address.value("unit"), 1, 255)
        || !integer(address.value("offset"), 0, 65536 - width(address)))
        return QCoreApplication::translate("Modbus", "Unit ID должен быть 1–255, адрес — смещением с нуля в пределах таблицы.");
    if (registerBit(registerType) ? type != "bool"
        : (type != "u16" && type != "i16" && type != "u32" && type != "i32" && type != "f32"))
        return QCoreApplication::translate("Modbus", "Выбранный тип данных несовместим с таблицей Modbus.");
    if (order != "ABCD" && order != "BADC" && order != "CDAB" && order != "DCBA")
        return QCoreApplication::translate("Modbus", "Выберите порядок байтов и слов.");
    const auto scale = tag.value("scale");
    const auto bias = tag.value("bias");
    if (!scale.isDouble() || !bias.isDouble() || !std::isfinite(scale.toDouble())
        || scale.toDouble() == 0 || !std::isfinite(bias.toDouble()))
        return QCoreApplication::translate("Modbus", "Масштаб должен быть конечным и ненулевым, смещение — конечным.");
    if (type == "bool" && (scale.toDouble() != 1 || bias.toDouble() != 0))
        return QCoreApplication::translate("Modbus", "Для битового тега задайте масштаб 1 и смещение 0.");
    if (!tag.value("writable").isBool() ||
        (tag.value("writable").toBool() && registerType != QModbusDataUnit::HoldingRegisters
         && registerType != QModbusDataUnit::Coils))
        return QCoreApplication::translate("Modbus", "Запись доступна только для Coils и Holding Registers.");
    return {};
}

bool Modbus::decode(const QJsonObject &tag, const QList<quint16> &input, double &value)
{
    const auto address = tag.value("address").toObject();
    if (input.size() != width(address))
        return false;
    auto words = input;
    orderWords(words, address.value("order").toString());
    const auto type = address.value("type").toString();
    if (type == "bool")
        value = words.first() != 0 ? 1 : 0;
    else if (type == "u16")
        value = words.first();
    else if (type == "i16")
        value = std::bit_cast<qint16>(words.first());
    else {
        if (words.size() != 2)
            return false;
        const quint32 raw = (static_cast<quint32>(words[0]) << 16) | words[1];
        if (type == "u32") value = raw;
        else if (type == "i32") value = std::bit_cast<qint32>(raw);
        else if (type == "f32") value = std::bit_cast<float>(raw);
        else return false;
    }
    value = value * tag.value("scale").toDouble() + tag.value("bias").toDouble();
    return std::isfinite(value);
}

bool Modbus::encode(const QJsonObject &tag, double value, QList<quint16> &words, QString &error)
{
    const auto address = tag.value("address").toObject();
    const auto type = address.value("type").toString();
    const double raw = (value - tag.value("bias").toDouble()) / tag.value("scale").toDouble();
    const double rounded = std::round(raw);
    if (!std::isfinite(raw)) {
        error = QCoreApplication::translate("Modbus", "Значение выходит за диапазон типа.");
        return false;
    }
    quint32 bits = 0;
    if (type == "f32") {
        if (std::abs(raw) > std::numeric_limits<float>::max()) {
            error = QCoreApplication::translate("Modbus", "Значение выходит за диапазон Float32.");
            return false;
        }
        bits = std::bit_cast<quint32>(static_cast<float>(raw));
    } else {
        double min = 0;
        double max = 1;
        if (type == "u16") max = 65535;
        else if (type == "i16") { min = -32768; max = 32767; }
        else if (type == "u32") max = 4294967295.0;
        else if (type == "i32") { min = -2147483648.0; max = 2147483647.0; }
        if (std::abs(raw - rounded) > 0.000001 || rounded < min || rounded > max) {
            error = QCoreApplication::translate("Modbus", "Значение не представимо целым числом с заданным масштабом и типом.");
            return false;
        }
        bits = static_cast<quint32>(static_cast<qint64>(rounded));
    }
    words = {static_cast<quint16>(bits)};
    if (width(address) == 2)
        words.prepend(static_cast<quint16>(bits >> 16));
    orderWords(words, address.value("order").toString());
    error.clear();
    return true;
}

ModbusSource::ModbusSource(const QJsonObject &source, const QList<QJsonObject> &tags, QObject *parent)
    : DataSource(parent), tags_(tags), groups_(groupTags(tags))
{
    const auto settings = source.value("settings").toObject();
    auto host = settings.value("host").toString();
    if (QHostAddress(host).protocol() == QAbstractSocket::IPv6Protocol)
        host = '[' + host + ']';
    client_.setConnectionParameter(QModbusDevice::NetworkAddressParameter, host);
    client_.setConnectionParameter(QModbusDevice::NetworkPortParameter, settings.value("port").toInt());
    client_.setTimeout(settings.value("timeoutMs").toInt());
    client_.setNumberOfRetries(0);
    pollTimer_.setInterval(settings.value("pollMs").toInt());
    connect(&pollTimer_, &QTimer::timeout, this, &ModbusSource::poll);
    connect(&client_, &QModbusDevice::stateChanged, this, [this](auto state) {
        if (!running_)
            return;
        if (state == QModbusDevice::ConnectedState) {
            retryMs_ = 250;
            emit statusChanged("connected", {});
            QMetaObject::invokeMethod(this, &ModbusSource::poll, Qt::QueuedConnection);
        } else if (state == QModbusDevice::UnconnectedState) {
            const auto detail = QCoreApplication::translate("Modbus", "Нет связи с устройством.");
            cancelRequests(detail);
            emit statusChanged("disconnected", detail);
            report(tags_, Quality::Stale, detail);
        }
    });
    connect(&client_, &QModbusDevice::errorOccurred, this, [this](auto error) {
        if (!running_ || error != QModbusDevice::ConnectionError)
            return;
        const auto detail = client_.errorString();
        cancelRequests(detail);
        emit statusChanged("disconnected", detail);
        report(tags_, Quality::Stale, detail);
    });
}

ModbusSource::Group ModbusSource::tagGroup(const QJsonObject &tag)
{
    const auto address = tag.value("address").toObject();
    return {address.value("unit").toInt(), Modbus::table(address),
            address.value("offset").toInt(), Modbus::width(address), {tag}};
}

QList<ModbusSource::Group> ModbusSource::groupTags(const QList<QJsonObject> &tags)
{
    QList<Group> requests;
    for (const auto &tag : tags)
        requests.append(tagGroup(tag));
    std::stable_sort(requests.begin(), requests.end(), [](const Group &a, const Group &b) {
        return std::tie(a.unit, a.table, a.first) < std::tie(b.unit, b.table, b.first);
    });
    QList<Group> groups;
    for (const auto &request : requests) {
        if (!groups.isEmpty()) {
            auto &last = groups.last();
            const auto end = std::max(last.first + last.count, request.first + request.count);
            const int limit = registerBit(last.table) ? 2000 : 125;
            if (last.unit == request.unit && last.table == request.table
                && request.first <= last.first + last.count && end - last.first <= limit) {
                last.count = end - last.first;
                last.tags.append(request.tags);
                continue;
            }
        }
        groups.append(request);
    }
    return groups;
}

void ModbusSource::start()
{
    if (running_)
        return;
    running_ = true;
    clock_.restart();
    nextConnect_ = 0;
    pollTimer_.start();
    poll();
}

void ModbusSource::stop()
{
    running_ = false;
    pollTimer_.stop();
    cancelRequests(QCoreApplication::translate("Modbus", "Опрос остановлен."));
    client_.disconnectDevice();
}

void ModbusSource::poll()
{
    if (!running_)
        return;
    if (client_.state() == QModbusDevice::UnconnectedState) {
        const auto now = clock_.elapsed();
        if (now < nextConnect_)
            return;
        nextConnect_ = now + retryMs_;
        retryMs_ = std::min(5000, retryMs_ * 2);
        connectDeadline_ = now + std::clamp(client_.timeout(), 2000, 5000);
        emit statusChanged("connecting", {});
        if (!client_.connectDevice()) {
            emit statusChanged("disconnected", client_.errorString());
            report(tags_, Quality::Stale, client_.errorString());
        }
        return;
    }
    if (client_.state() == QModbusDevice::ConnectingState && clock_.elapsed() >= connectDeadline_) {
        client_.disconnectDevice();
        return;
    }
    if (client_.state() != QModbusDevice::ConnectedState || readReply_ || writeReply_)
        return;
    if (pending_.isEmpty())
        pending_ = groups_;
    next();
}

void ModbusSource::next()
{
    if (!running_ || readReply_ || writeReply_
        || client_.state() != QModbusDevice::ConnectedState)
        return;
    if (!actions_.isEmpty()) {
        const auto action = actions_.takeFirst();
        if (const auto *request = std::get_if<Write>(&action)) {
            const auto error = sendWrite(*request);
            if (!error.isEmpty()) {
                emit writeFinished(request->tagId, false, error);
                cancelQueue(error);
                QMetaObject::invokeMethod(this, &ModbusSource::next, Qt::QueuedConnection);
            }
        } else {
            sendRead(std::get<Group>(action));
        }
        return;
    }
    if (!pending_.isEmpty())
        sendRead(pending_.takeFirst());
}

void ModbusSource::sendRead(const Group &group)
{
    const auto tagId = group.onDemand ? group.tags.first().value("id").toString() : QString{};
    const QModbusDataUnit request(group.table, group.first, static_cast<quint16>(group.count));
    auto *reply = client_.sendReadRequest(request, group.unit);
    if (!reply) {
        auto detail = client_.errorString();
        if (detail.isEmpty())
            detail = QCoreApplication::translate("Modbus", "Не удалось отправить запрос чтения.");
        report(group.tags, Quality::Error, detail);
        if (!tagId.isEmpty())
            emit readFinished(tagId, false, detail, group.readToken, 0);
        QMetaObject::invokeMethod(this, &ModbusSource::next, Qt::QueuedConnection);
        return;
    }
    readReply_ = reply;
    readTagId_ = tagId;
    readToken_ = group.readToken;
    if (reply->isFinished())
        consume(reply, group);
    else
        connect(reply, &QModbusReply::finished, this, [this, reply, group] { consume(reply, group); });
}

void ModbusSource::report(const QList<QJsonObject> &tags, Quality quality, const QString &detail)
{
    for (const auto &tag : tags)
        emit sample(tag.value("id").toString(), 0, quality, detail);
}

void ModbusSource::consume(QModbusReply *reply, const Group &group)
{
    reply->deleteLater();
    if (reply != readReply_)
        return;
    readReply_.clear();
    const auto tagId = std::exchange(readTagId_, {});
    const auto token = std::exchange(readToken_, 0);
    if (!running_)
        return;
    const auto complete = [this, &tagId, token](bool accepted, const QString &detail, double value = 0) {
        if (!tagId.isEmpty())
            emit readFinished(tagId, accepted, detail, token, value);
    };
    const auto values = reply->result().values();
    const auto maxCount = registerBit(group.table) ? (group.count + 7) / 8 * 8 : group.count;
    if (reply->error() == QModbusDevice::NoError && values.size() >= group.count && values.size() <= maxCount) {
        for (const auto &tag : group.tags) {
            const auto address = tag.value("address").toObject();
            const auto words = values.sliced(address.value("offset").toInt() - group.first, Modbus::width(address));
            double value = 0;
            const bool valid = Modbus::decode(tag, words, value);
            const auto detail = valid ? QString{} : QCoreApplication::translate("Modbus", "Некорректное числовое значение в ответе.");
            emit sample(tag.value("id").toString(), value, valid ? Quality::Good : Quality::Error, detail);
            complete(valid, valid ? QCoreApplication::translate("Modbus", "Значение прочитано.") : detail, value);
        }
    } else if (reply->error() == QModbusDevice::ProtocolError && group.tags.size() > 1
               && (reply->rawResult().exceptionCode() == QModbusPdu::IllegalDataAddress
                   || reply->rawResult().exceptionCode() == QModbusPdu::IllegalDataValue)) {
        for (auto it = group.tags.crbegin(); it != group.tags.crend(); ++it)
            pending_.prepend(tagGroup(*it));
    } else if (reply->error() == QModbusDevice::TimeoutError || reply->error() == QModbusDevice::ReplyAbortedError) {
        const auto detail = reply->errorString().isEmpty() ? QCoreApplication::translate("Modbus", "Нет ответа на чтение.") : reply->errorString();
        cancelQueue(detail);
        report(group.tags, Quality::Stale, detail);
        complete(false, detail);
        pending_.removeIf([this, &group, &detail](const Group &request) {
            if (request.unit != group.unit)
                return false;
            report(request.tags, Quality::Stale, detail);
            return true;
        });
    } else {
        const auto detail = reply->errorString().isEmpty() ? QCoreApplication::translate("Modbus", "Неверная длина ответа Modbus.") : reply->errorString();
        report(group.tags, Quality::Error, detail);
        complete(false, detail);
    }
    QMetaObject::invokeMethod(this, &ModbusSource::next, Qt::QueuedConnection);
}

QString ModbusSource::enqueue(Action action)
{
    if (!running_ || client_.state() != QModbusDevice::ConnectedState)
        return QCoreApplication::translate("Modbus", "Источник не подключён.");
    constexpr int maxQueuedActions = 64;
    if (actions_.size() >= maxQueuedActions)
        return QCoreApplication::translate("Modbus", "Очередь команд Modbus заполнена.");
    actions_.append(std::move(action));
    if (!readReply_ && !writeReply_ && actions_.size() == 1)
        QMetaObject::invokeMethod(this, &ModbusSource::next, Qt::QueuedConnection);
    return {};
}

QString ModbusSource::read(const QJsonObject &tag, quint64 token)
{
    auto group = tagGroup(tag);
    group.onDemand = true;
    group.readToken = token;
    return enqueue(std::move(group));
}

QString ModbusSource::write(const QJsonObject &tag, double value)
{
    QList<quint16> words;
    QString error;
    if (!Modbus::encode(tag, value, words, error))
        return error;
    const auto address = tag.value("address").toObject();
    return enqueue(Write{QModbusDataUnit(Modbus::table(address), address.value("offset").toInt(), words),
                         address.value("unit").toInt(), tag.value("id").toString()});
}

void ModbusSource::cancelQueue(const QString &detail)
{
    const auto reason = QCoreApplication::translate("Modbus", "Команда отменена до отправки. %1").arg(detail);
    const auto actions = std::exchange(actions_, {});
    for (const auto &action : actions) {
        if (const auto *request = std::get_if<Write>(&action))
            emit writeFinished(request->tagId, false, reason);
        else {
            const auto &group = std::get<Group>(action);
            emit readFinished(group.tags.first().value("id").toString(), false, reason, group.readToken, 0);
        }
    }
}

void ModbusSource::cancelRequests(const QString &detail)
{
    pending_.clear();
    readReply_.clear();
    writeReply_.clear();
    const auto readTagId = std::exchange(readTagId_, {});
    const auto token = std::exchange(readToken_, 0);
    const auto writeTagId = std::exchange(writeTagId_, {});
    if (!readTagId.isEmpty())
        emit readFinished(readTagId, false, detail, token, 0);
    if (!writeTagId.isEmpty())
        emit writeFinished(writeTagId, false, QCoreApplication::translate("Modbus", "Результат записи неизвестен. %1").arg(detail));
    cancelQueue(detail);
}

QString ModbusSource::sendWrite(const Write &request)
{
    auto *reply = client_.sendWriteRequest(request.data, request.unit);
    if (!reply)
        return client_.errorString().isEmpty() ? QCoreApplication::translate("Modbus", "Не удалось отправить запрос.") : client_.errorString();
    writeReply_ = reply;
    writeTagId_ = request.tagId;
    const auto finish = [this, reply] {
        reply->deleteLater();
        if (reply != writeReply_)
            return;
        writeReply_.clear();
        const auto tagId = std::exchange(writeTagId_, {});
        const auto error = reply->error();
        const auto accepted = error == QModbusDevice::NoError;
        const auto detail = accepted ? QCoreApplication::translate("Modbus", "Устройство подтвердило запись.")
            : (error == QModbusDevice::ProtocolError ? reply->errorString()
                : QCoreApplication::translate("Modbus", "Результат записи неизвестен. %1").arg(reply->errorString()));
        emit writeFinished(tagId, accepted, detail);
        if (!accepted && error != QModbusDevice::ProtocolError)
            cancelQueue(detail);
        QMetaObject::invokeMethod(this, &ModbusSource::poll, Qt::QueuedConnection);
    };
    if (reply->isFinished())
        QTimer::singleShot(0, this, finish);
    else
        connect(reply, &QModbusReply::finished, this, finish);
    return {};
}
