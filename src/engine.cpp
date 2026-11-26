#include "engine.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>

namespace {
constexpr int packetLimit = 4096;
const QByteArray hello = R"({"type":"hello","version":1,"client":"engine-local-monitor"})";

bool validPath(const QString &path)
{
    static const QRegularExpression pattern("^[A-Za-z_][A-Za-z0-9_]*(?:\\.[A-Za-z_][A-Za-z0-9_]*)*$");
    return path.size() <= 192 && pattern.match(path).hasMatch();
}
}

QString Engine::validateSource(const QJsonObject &source)
{
    const auto settings = source.value("settings").toObject();
    const auto transport = settings.value("transport").toString();
    const auto timeout = settings.value("timeoutMs");
    if (!timeout.isDouble() || timeout.toDouble() != timeout.toInt()
        || timeout.toInt() < 500 || timeout.toInt() > 30000)
        return "Тайм-аут телеметрии должен быть от 500 до 30000 мс.";
    if (settings.value("allowWrite").toBool() || settings.value("simulator").toBool())
        return "Источник JSON-телеметрии работает в режиме чтения.";
    if (transport == "websocket") {
        const QUrl url(settings.value("url").toString(), QUrl::StrictMode);
        if (!url.isValid() || (url.scheme() != "ws" && url.scheme() != "wss")
            || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasFragment() || url.port() == 0)
            return "Введите полный адрес ws:// или wss:// без учётных данных и фрагмента.";
    } else if (transport == "serial") {
        static const QRegularExpression port("^(COM[1-9][0-9]{0,3}|/dev/[A-Za-z0-9._/-]+)$",
                                              QRegularExpression::CaseInsensitiveOption);
        if (!port.match(settings.value("portName").toString()).hasMatch())
            return "Введите имя последовательного порта, например COM7.";
    } else {
        return "Выберите WebSocket или USB COM.";
    }
    return {};
}

QString Engine::validateTag(const QJsonObject &tag)
{
    const auto address = tag.value("address").toObject();
    const auto type = address.value("type").toString();
    if (!validPath(address.value("path").toString()))
        return "Введите путь к JSON-полю, например values.rpm.";
    if (type != "number" && type != "bool" && type != "text")
        return "Выберите число, логическое значение или текст.";
    const auto scale = tag.value("scale");
    const auto bias = tag.value("bias");
    if (!scale.isDouble() || !bias.isDouble() || !std::isfinite(scale.toDouble())
        || scale.toDouble() == 0 || !std::isfinite(bias.toDouble()))
        return "Проверьте масштаб и смещение.";
    if (tag.value("writable").toBool())
        return "Теги JSON-телеметрии предназначены для чтения.";
    if (type != "number" && (scale.toDouble() != 1 || bias.toDouble() != 0))
        return "Для логических и текстовых полей используйте масштаб 1 и смещение 0.";
    if (type == "text" && (tag.value("archiveMs").toInt() != 0 || !tag.value("alarm").isUndefined()))
        return "Текстовое поле доступно в таблице и виджете значения.";
    return {};
}

QJsonValue Engine::field(const QJsonObject &packet, const QStringList &path)
{
    QJsonValue value(packet);
    for (const auto &key : path) {
        if (!value.isObject())
            return QJsonValue::Undefined;
        value = value.toObject().value(key);
    }
    return value;
}

QList<QByteArray> Engine::SerialFrames::read(const QByteArray &chunk)
{
    QList<QByteArray> packets;
    for (const char byte : chunk) {
        if (byte == '\n') {
            if (!discarding_ && line_.startsWith('@')) {
                if (line_.endsWith('\r')) line_.chop(1);
                if (line_.size() <= packetLimit + 1) packets.append(line_.mid(1));
            }
            clear();
        } else if (!discarding_) {
            if (line_.size() < packetLimit + 2) line_.append(byte);
            else { discarding_ = true; line_.clear(); }
        }
    }
    return packets;
}

void Engine::SerialFrames::clear()
{
    line_.clear();
    discarding_ = false;
}

EngineSource::EngineSource(const QJsonObject &source, const QList<QJsonObject> &tags, QObject *parent)
    : DataSource(parent), timeout_(source.value("settings").toObject().value("timeoutMs").toInt())
{
    const auto settings = source.value("settings").toObject();
    if (settings.value("transport").toString() == "websocket")
        url_ = QUrl(settings.value("url").toString());
    else
        port_ = settings.value("portName").toString();
    for (const auto &tag : tags) {
        const auto addr = tag.value("address").toObject();
        tags_.append({tag.value("id").toString(), addr.value("path").toString().split('.'),
                      addr.value("type").toString(), tag.value("scale").toDouble(), tag.value("bias").toDouble()});
    }
    socket_.setMaxAllowedIncomingMessageSize(packetLimit);
    socket_.setMaxAllowedIncomingFrameSize(packetLimit);
    connect(&socket_, &QWebSocket::connected, this, &EngineSource::sendHello);
    connect(&socket_, &QWebSocket::disconnected, this, [this] { disconnect("WebSocket закрыт."); });
    connect(&socket_, &QWebSocket::textMessageReceived, this, [this](const QString &text) { receive(text.toUtf8()); });
    connect(&socket_, &QWebSocket::errorOccurred, this, [this](auto) { disconnect(socket_.errorString()); });
    connect(&serial_, &QSerialPort::readyRead, this, [this] {
        for (const auto &packet : frames_.read(serial_.readAll())) receive(packet);
    });
    connect(&serial_, &QSerialPort::errorOccurred, this, [this](auto error) {
        if (error != QSerialPort::NoError && serial_.isOpen()) disconnect(serial_.errorString());
    });
    serialHello_.setSingleShot(true);
    connect(&serialHello_, &QTimer::timeout, this, &EngineSource::sendHello);
    timer_.setInterval(125);
    connect(&timer_, &QTimer::timeout, this, &EngineSource::tick);
}

void EngineSource::start()
{
    if (running_) return;
    running_ = true;
    clock_.start();
    nextConnect_ = 0;
    timer_.start();
    tick();
}

void EngineSource::stop()
{
    running_ = false;
    timer_.stop();
    disconnect({});
}

QString EngineSource::write(const QJsonObject &, double)
{
    return "Источник JSON-телеметрии работает в режиме чтения.";
}

void EngineSource::tick()
{
    const auto now = clock_.elapsed();
    if (opened_ >= 0) {
        if (lastPacket_ < 0 && now - opened_ >= 8000)
            disconnect("После подключения не получена JSON-телеметрия.");
        else if (lastPacket_ >= 0 && now - lastPacket_ >= timeout_)
            disconnect("Истёк тайм-аут JSON-телеметрии.");
        return;
    }
    if (now < nextConnect_) return;
    opened_ = now;
    emit statusChanged("connecting", "Ожидается идентификация источника JSON-телеметрии.");
    if (port_.isEmpty()) {
        socket_.open(url_);
        return;
    }
    serial_.setPortName(port_);
    serial_.setBaudRate(QSerialPort::Baud115200);
    serial_.setDataBits(QSerialPort::Data8);
    serial_.setParity(QSerialPort::NoParity);
    serial_.setStopBits(QSerialPort::OneStop);
    serial_.setFlowControl(QSerialPort::NoFlowControl);
    if (serial_.open(QIODevice::ReadWrite)) serialHello_.start(1400);
    else disconnect(serial_.errorString());
}

void EngineSource::sendHello()
{
    if (!running_ || opened_ < 0) return;
    if (serial_.isOpen()) {
        frames_.clear();
        serial_.clear(QSerialPort::Input);
        serial_.write(hello + '\n');
    } else {
        socket_.sendTextMessage(QString::fromUtf8(hello));
    }
}

void EngineSource::receive(const QByteArray &raw)
{
    if (!running_ || opened_ < 0 || raw.size() > packetLimit) return;
    const auto packet = QJsonDocument::fromJson(raw).object();
    if (packet.value("version").toDouble() != 1) return;
    const auto type = packet.value("type").toString();
    if (type == "identity") {
        static const QRegularExpression format("^engine-[a-f0-9]{1,16}$");
        const auto id = packet.value("deviceId").toString();
        if (!format.match(id).hasMatch()) return;
        device_ = id;
        return;
    }
    if (device_.isEmpty() || type != "telemetry" || !packet.value("values").isObject()) return;
    const auto session = packet.value("session").toString();
    const auto ts = packet.value("ts").toDouble(-1);
    if (session.isEmpty() || session.size() > 100 || !std::isfinite(ts) || ts < 0) return;
    const bool wraparound = lastTs_ >= 4026531840.0 && ts < 268435456.0;
    if (session == session_ && ts <= lastTs_ && !wraparound) return;
    if (!session_.isEmpty() && session != session_) stale("Началась новая сессия источника.");
    if (lastPacket_ < 0) emit statusChanged("connected", device_);
    lastPacket_ = clock_.elapsed();
    session_ = session;
    lastTs_ = ts;
    stale_ = false;
    for (const auto &tag : tags_) {
        const auto rawValue = Engine::field(packet, tag.path);
        if (rawValue.isNull() || rawValue.isUndefined()) {
            emit sample(tag.id, {}, Quality::Stale, "Поле пока недоступно.");
        } else if (tag.type == "text" && rawValue.isString()) {
            emit sample(tag.id, rawValue.toString(), Quality::Good, {});
        } else if (tag.type == "bool" && (rawValue.isBool() ||
                   (rawValue.isDouble() && (rawValue.toDouble() == 0 || rawValue.toDouble() == 1)))) {
            emit sample(tag.id, rawValue.isBool() ? double(rawValue.toBool()) : rawValue.toDouble(), Quality::Good, {});
        } else if (tag.type == "number" && rawValue.isDouble()
                   && std::isfinite(rawValue.toDouble() * tag.scale + tag.bias)) {
            emit sample(tag.id, rawValue.toDouble() * tag.scale + tag.bias, Quality::Good, {});
        } else {
            emit sample(tag.id, {}, Quality::Error, "Тип значения JSON не совпадает с настройкой тега.");
        }
    }
}

void EngineSource::stale(const QString &reason)
{
    if (stale_) return;
    stale_ = true;
    for (const auto &tag : tags_) emit sample(tag.id, {}, Quality::Stale, reason);
}

void EngineSource::disconnect(const QString &reason)
{
    if (disconnecting_) return;
    disconnecting_ = true;
    opened_ = -1;
    lastPacket_ = -1;
    lastTs_ = -1;
    device_.clear();
    session_.clear();
    frames_.clear();
    serialHello_.stop();
    if (serial_.isOpen()) serial_.close();
    if (socket_.state() != QAbstractSocket::UnconnectedState) socket_.abort();
    if (running_) {
        nextConnect_ = clock_.elapsed() + 1000;
        emit statusChanged("disconnected", reason);
        stale(reason);
    }
    disconnecting_ = false;
}
