#include "demo.h"

#include "project.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>

DemoServer::DemoServer(QObject *parent) : QObject(parent)
{
    server_.setServerAddress(1);
    server_.setMap({
        {QModbusDataUnit::Coils, {QModbusDataUnit::Coils, 0, quint16(16)}},
        {QModbusDataUnit::DiscreteInputs, {QModbusDataUnit::DiscreteInputs, 0, quint16(16)}},
        {QModbusDataUnit::InputRegisters, {QModbusDataUnit::InputRegisters, 0, quint16(16)}},
        {QModbusDataUnit::HoldingRegisters, {QModbusDataUnit::HoldingRegisters, 0, quint16(16)}}
    });
    server_.setData(QModbusDataUnit::Coils, 0, 1);
    server_.setData(QModbusDataUnit::HoldingRegisters, 0, 1200);
    server_.setData(QModbusDataUnit::HoldingRegisters, 1, 1);
    timer_.setInterval(200);
    connect(&timer_, &QTimer::timeout, this, &DemoServer::tick);
    tick();
}

bool DemoServer::listen(int port, QString &error)
{
    server_.setConnectionParameter(QModbusDevice::NetworkAddressParameter, "127.0.0.1");
    server_.setConnectionParameter(QModbusDevice::NetworkPortParameter, port);
    if (!server_.connectDevice()) {
        error = server_.errorString();
        return false;
    }
    timer_.start();
    return true;
}

void DemoServer::setPaused(bool paused)
{
    if (paused) {
        timer_.stop();
        server_.disconnectDevice();
    } else {
        server_.connectDevice();
        timer_.start();
    }
}

void DemoServer::tick()
{
    quint16 command = 0;
    quint16 setpoint = 0;
    quint16 valve = 0;
    quint16 mode = 0;
    server_.data(QModbusDataUnit::Coils, 0, &command);
    server_.data(QModbusDataUnit::Coils, 1, &valve);
    server_.data(QModbusDataUnit::HoldingRegisters, 0, &setpoint);
    server_.data(QModbusDataUnit::HoldingRegisters, 1, &mode);
    const bool running = command && mode;
    const auto correction = mode == 2 ? std::lround((60 - level_) * 15) : 0L;
    const auto speed = running ? std::clamp(setpoint + correction, 0L, 3000L) : 0L;
    ++ticks_;
    const auto input = speed / 1500.0;
    level_ = std::clamp(level_ + (input - 0.58 + std::sin(ticks_ / 12.0) * 0.24) * 0.24, 0.0, 100.0);
    const auto pressure = running ? std::lround(170 + speed * 0.09 + std::sin(ticks_ / 7.0) * 28) : 0;
    server_.setData(QModbusDataUnit::InputRegisters, 0, quint16(std::lround(level_ * 10)));
    server_.setData(QModbusDataUnit::InputRegisters, 1, quint16(pressure));
    server_.setData(QModbusDataUnit::InputRegisters, 2, quint16(speed));
    server_.setData(QModbusDataUnit::InputRegisters, 3, quint16((35 + ticks_ * 3) % 360));
    const auto temperature = 24.0 + speed * 0.019 + std::sin(ticks_ / 15.0) * 2.2;
    server_.setData(QModbusDataUnit::InputRegisters, 4, quint16(std::lround(temperature * 10)));
    server_.setData(QModbusDataUnit::DiscreteInputs, 0, running);
    server_.setData(QModbusDataUnit::DiscreteInputs, 1, valve != 0);
}

QJsonObject DemoServer::project()
{
    auto project = Project::empty();
    project.insert("name", "Насосная станция");
    const auto source = Project::id();
    project.insert("sources", QJsonArray{QJsonObject{
        {"id", source}, {"name", "Симулятор станции"}, {"driver", "modbus-tcp"},
        {"settings", QJsonObject{{"host", "127.0.0.1"}, {"port", 15020}, {"pollMs", 500}, {"timeoutMs", 800},
                                 {"allowWrite", true}, {"simulator", true}}}}});
    const auto tag = [&source](const QString &name, const QString &units, const QString &table,
                               const QString &type, int offset, double scale, bool writable = false) {
        return QJsonObject{{"id", Project::id()}, {"name", name}, {"source", source}, {"units", units},
                           {"address", QJsonObject{{"table", table}, {"offset", offset}, {"unit", 1},
                                                   {"type", type}, {"order", "ABCD"}}},
                           {"scale", scale}, {"bias", 0}, {"archiveMs", 1000}, {"writable", writable}};
    };
    auto level = tag("Уровень бака", "%", "input", "u16", 0, 0.1);
    level.insert("alarm", QJsonObject{{"direction", "high"}, {"limit", 60}, {"hysteresis", 1}});
    const auto pressure = tag("Давление", "бар", "input", "u16", 1, 0.01);
    const auto speed = tag("Обороты", "об/мин", "input", "u16", 2, 1);
    const auto active = tag("Насос работает", "", "discrete", "bool", 0, 1);
    const auto command = tag("Команда насоса", "", "coils", "bool", 0, 1, true);
    const auto setpoint = tag("Уставка оборотов", "об/мин", "holding", "u16", 0, 1, true);
    const auto heading = tag("Угол положения", "°", "input", "u16", 3, 1);
    const auto temperature = tag("Температура", "°C", "input", "u16", 4, 0.1);
    const auto valveCommand = tag("Команда клапана", "", "coils", "bool", 1, 1, true);
    const auto valveState = tag("Клапан открыт", "", "discrete", "bool", 1, 1);
    const auto mode = tag("Режим регулятора", "", "holding", "u16", 1, 1, true);
    project.insert("tags", QJsonArray{level, pressure, speed, active, command, setpoint, heading, temperature,
                                     valveCommand, valveState, mode});
    const auto main = Project::id();
    const auto commands = Project::id();
    const auto gallery = Project::id();
    const auto widget = [](const QString &kind, const QString &title, const QJsonObject &tag,
                           int x, int y, int w, int h, double max = 100) {
        return QJsonObject{{"id", Project::id()}, {"kind", kind}, {"title", title}, {"tag", tag.value("id").toString()},
                           {"x", x}, {"y", y}, {"w", w}, {"h", h}, {"min", 0}, {"max", max}};
    };
    auto link = widget("link", "Управление насосом", {}, 870, 474, 314, 99);
    link.insert("screen", commands);
    auto galleryJump = widget("link", "Все элементы", {}, 870, 587, 314, 89);
    galleryJump.insert("screen", gallery);
    auto returnLink = widget("link", "Обзор станции", {}, 32, 750, 270, 72);
    returnLink.insert("screen", main);
    auto on = widget("command", "Пуск насоса", command, 32, 196, 310, 132);
    on.insert("command", 1);
    auto off = widget("command", "Останов насоса", command, 366, 196, 310, 132);
    off.insert("command", 0);
    auto boost = widget("command", "Уставка 1800 об/мин", setpoint, 700, 196, 462, 132);
    boost.insert("command", 1800);
    auto holdValve = widget("command", "Удерживать клапан открытым", {}, 320, 750, 270, 72);
    holdValve.insert("command", 1);
    holdValve.insert("actions", QJsonArray{
        QJsonObject{{"event", "press"}, {"action", "write"}, {"tag", valveCommand.value("id")},
                    {"valueMode", "constant"}, {"value", 1}},
        QJsonObject{{"event", "release"}, {"action", "write"}, {"tag", valveCommand.value("id")},
                    {"valueMode", "constant"}, {"value", 0}}});
    auto readSetpoint = widget("command", "Прочитать уставку", {}, 608, 750, 270, 72);
    readSetpoint.insert("command", 0);
    readSetpoint.insert("actions", QJsonArray{
        QJsonObject{{"event", "click"}, {"action", "read"}, {"tag", setpoint.value("id")}}});
    auto copySpeed = widget("command", "Обороты в уставку", {}, 896, 750, 272, 72);
    copySpeed.insert("command", 0);
    copySpeed.insert("actions", QJsonArray{
        QJsonObject{{"event", "press"}, {"action", "write"}, {"tag", setpoint.value("id")},
                    {"valueMode", "tag"}, {"valueTag", speed.value("id")}}});
    auto overviewTrend = widget("trend", "Параметры станции", {}, 16, 240, 840, 436);
    overviewTrend.insert("appearance", QJsonObject{{"background", "#f4f7f8"}});
    overviewTrend.insert("trend", QJsonObject{{"seconds", 60}, {"axis", "individual"}, {"legend", true}, {"toolbar", true},
        {"series", QJsonArray{
            QJsonObject{{"tag", level.value("id")}, {"color", "#008975"}, {"side", "left"}, {"style", "line"},
                        {"min", 0}, {"max", 100}},
            QJsonObject{{"tag", pressure.value("id")}, {"color", "#bd6200"}, {"side", "right"}, {"style", "line"},
                        {"min", 0}, {"max", 6}, {"divisions", 6}},
            QJsonObject{{"tag", speed.value("id")}, {"color", "#ae3e70"}, {"side", "right"}, {"style", "line"},
                        {"min", 0}, {"max", 3000}, {"divisions", 6}}}}});
    auto pressureDisplay = widget("value", "Давление", pressure, 360, 90, 250, 136);
    pressureDisplay.insert("appearance", QJsonObject{{"rules", QJsonArray{
        QJsonObject{{"op", "gte"}, {"limit", 2}, {"background", "#e7f6ed"}, {"value", "#176044"}},
        QJsonObject{{"op", "lt"}, {"limit", 1}, {"background", "#fff1db"}, {"value", "#985018"}}}}});
    const auto instrument = [&widget](const QString &kind, const QString &title, const QJsonObject &tag,
                                       int x, int y, double max, int decimals, const QString &color, int w = 362, int h = 252) {
        auto item = widget(kind, title, tag, x, y, w, h, max);
        item.insert("decimals", decimals);
        item.insert("accent", color);
        return item;
    };
    const auto control = [&widget, &setpoint](const QString &kind, const QString &title, int x, int y, int w, int h) {
        auto item = widget(kind, title, setpoint, x, y, w, h, 3000);
        item.insert("step", 50);
        item.insert("decimals", 0);
        return item;
    };
    const auto binary = [&widget](const QString &kind, const QString &title, const QJsonObject &output,
                                  const QJsonObject &feedback, int x, int y, int w, int h) {
        auto item = widget(kind, title, output, x, y, w, h);
        item.insert("feedback", feedback.value("id"));
        return item;
    };
    const auto selector = [&widget, &mode](int x, int y, int w, int h) {
        auto item = widget("selector", "Режим насоса", mode, x, y, w, h);
        item.insert("positions", QJsonArray{QJsonObject{{"label", "Стоп"}, {"value", 0}},
                                            QJsonObject{{"label", "Ручной"}, {"value", 1}},
                                            QJsonObject{{"label", "Авто"}, {"value", 2}}});
        return item;
    };
    auto schematic = widget("image", "Схема контура", {}, 964, 72, 304, 148);
    schematic.insert("image", Project::demoImage);
    auto galleryPressure = widget("value", "Давление линии", pressure, 16, 72, 304, 148);
    galleryPressure.insert("appearance", pressureDisplay.value("appearance"));
    auto galleryTrend = widget("trend", "Параметры станции", {}, 648, 230, 620, 205);
    galleryTrend.insert("appearance", overviewTrend.value("appearance"));
    galleryTrend.insert("trend", overviewTrend.value("trend"));
    auto galleryCommand = widget("command", "Команда · Пуск насоса", command, 1280, 645, 304, 153);
    galleryCommand.insert("command", 1);
    auto galleryReturn = widget("link", "Обзор станции", {}, 1280, 12, 304, 52);
    galleryReturn.insert("screen", main);
    const auto typeId = Project::id();
    const auto valuePort = QJsonObject{{"id", Project::id()}, {"name", "Показание"}, {"type", "number"}};
    const auto commandPort = QJsonObject{{"id", Project::id()}, {"name", "Команда"}, {"type", "bool"}};
    const auto feedbackPort = QJsonObject{{"id", Project::id()}, {"name", "Обратная связь"}, {"type", "bool"}};
    const auto panelHeading = widget("label", "Контроль узла", {}, 14, 9, 484, 52);
    const auto panelValue = widget("value", "Показание", valuePort, 14, 68, 484, 86);
    const auto panelFeedback = widget("indicator", "Обратная связь", feedbackPort, 14, 276, 484, 92);
    auto panelAlarm = widget("label", "АВАРИЯ", {}, 350, 9, 148, 52);
    panelAlarm.insert("appearance", QJsonObject{{"background", "#586476"}, {"value", "#ffffff"}});
    const auto property = [](const QString &name, const QJsonObject &item, const QString &kind) {
        return QJsonObject{{"id", Project::id()}, {"name", name}, {"item", item.value("id")}, {"kind", kind}};
    };
    const auto headingText = property("Заголовок панели", panelHeading, "title");
    const auto valueBackground = property("Фон показания", panelValue, "background");
    const auto valueColor = property("Цвет показания", panelValue, "value");
    auto showFeedback = property("Показывать индикатор", panelFeedback, "visible");
    showFeedback.insert("defaultVisible", true);
    const auto headingColor = property("Цвет заголовка", panelHeading, "captionColor");
    auto showAlarm = property("Показывать аварию", panelAlarm, "visible");
    showAlarm.insert("defaultVisible", false);
    const auto alarmColor = property("Фон аварийной плашки", panelAlarm, "background");
    project.insert("faceplates", QJsonArray{QJsonObject{
        {"id", typeId}, {"name", "Панель оборудования"}, {"width", 512}, {"height", 382},
        {"ports", QJsonArray{valuePort, commandPort, feedbackPort}},
        {"properties", QJsonArray{headingText, valueBackground, valueColor, showFeedback, headingColor, showAlarm, alarmColor}},
        {"items", QJsonArray{
            panelHeading, panelValue,
            binary("switch", "Управление", commandPort, feedbackPort, 14, 166, 484, 100), panelFeedback, panelAlarm}}}});
    const auto panel = [&](const QString &title, int x, const QJsonObject &value, const QJsonObject &output,
                           const QJsonObject &feedback, const QJsonObject &overrides,
                           const QJsonObject &propertyBindings = {}) {
        auto instance = widget("faceplate", title, {}, x, 163, 544, 398);
        instance.insert("faceplate", typeId);
        instance.insert("bindings", QJsonObject{{valuePort.value("id").toString(), value.value("id")},
            {commandPort.value("id").toString(), output.value("id")}, {feedbackPort.value("id").toString(), feedback.value("id")}});
        instance.insert("appearance", QJsonObject{{"background", "#e3edf3"}});
        instance.insert("overrides", overrides);
        if (!propertyBindings.isEmpty())
            instance.insert("propertyBindings", propertyBindings);
        return instance;
    };
    const auto pumpTypeId = Project::id();
    const auto pumpPort = [](const QString &name, const QString &type) {
        return QJsonObject{{"id", Project::id()}, {"name", name}, {"type", type}};
    };
    const auto runPort = pumpPort("Насос работает", "bool");
    const auto levelPort = pumpPort("Уровень бака", "number");
    const auto outputPort = pumpPort("Команда насоса", "bool");
    auto pumpBase = widget("shape", "Фон панели", {}, 0, 0, 870, 430);
    pumpBase.insert("appearance", QJsonObject{{"background", "#f8fbfd"}, {"value", "#c7d9e5"}});
    pumpBase.insert("locked", true);
    auto pumpArt = widget("image", "Корпус насоса", {}, 20, 68, 516, 250);
    pumpArt.insert("image", Project::demoPumpIdle);
    pumpArt.insert("locked", true);
    auto pumpRunArt = widget("image", "Зелёный слой при работе", runPort, 20, 68, 516, 250);
    pumpRunArt.insert("image", Project::demoPumpRunning);
    pumpRunArt.insert("locked", true);
    pumpRunArt.insert("visible", false);
    pumpRunArt.insert("appearance", QJsonObject{{"rules", QJsonArray{
        QJsonObject{{"op", "eq"}, {"limit", 1}, {"visible", true}}}}});
    const auto pumpTitle = widget("label", "Насос P-101", {}, 28, 18, 462, 56);
    auto pumpState = widget("label", "Нет сигнала работы", runPort, 572, 88, 265, 62);
    pumpState.insert("appearance", QJsonObject{{"background", "#e7eff4"}, {"value", "#38566a"}, {"rules", QJsonArray{
        QJsonObject{{"op", "eq"}, {"limit", 0}, {"text", "Остановлен"}},
        QJsonObject{{"op", "eq"}, {"limit", 1}, {"text", "В работе"}, {"background", "#d6f5e7"}, {"value", "#14684d"}}}}});
    auto pumpFault = widget("label", "Высокий уровень", levelPort, 572, 166, 265, 52);
    pumpFault.insert("visible", false);
    pumpFault.insert("appearance", QJsonObject{{"background", "#fce7eb"}, {"value", "#a12742"}, {"rules", QJsonArray{
        QJsonObject{{"op", "gte"}, {"limit", 60}, {"visible", true}}}}});
    pumpFault.insert("animation", QJsonObject{{"kind", "blink"}, {"tag", levelPort.value("id")},
        {"op", "gte"}, {"limit", 60}, {"periodMs", 1000}, {"dimPercent", 20}});
    auto impeller = widget("image", "Крыльчатка", {}, 302, 159, 84, 84);
    impeller.insert("image", Project::demoPumpImpeller);
    impeller.insert("locked", true);
    impeller.insert("animation", QJsonObject{{"kind", "rotate"}, {"tag", runPort.value("id")},
        {"op", "eq"}, {"limit", 1}, {"periodMs", 850}});
    auto startLabel = widget("label", "Пуск", {}, 572, 262, 265, 58);
    startLabel.insert("appearance", QJsonObject{{"background", "#0b786d"}, {"value", "#ffffff"}});
    auto startArea = widget("hitArea", "Пуск насоса", {}, 572, 262, 265, 58);
    auto stopButton = widget("command", "Стоп", outputPort, 572, 334, 265, 58);
    stopButton.insert("command", 0);
    const auto pumpAction = [&outputPort](int value) {
        return QJsonArray{QJsonObject{{"event", "click"}, {"action", "write"}, {"tag", outputPort.value("id")},
                                     {"valueMode", "constant"}, {"value", value}}};
    };
    startArea.insert("actions", pumpAction(1));
    stopButton.insert("actions", pumpAction(0));
    auto pumpInstance = widget("faceplate", "Свой насос · P-101", {}, 165, 118, 870, 430);
    pumpInstance.insert("faceplate", pumpTypeId);
    pumpInstance.insert("bindings", QJsonObject{{runPort.value("id").toString(), active.value("id")},
        {levelPort.value("id").toString(), level.value("id")}, {outputPort.value("id").toString(), command.value("id")}});
    auto types = project.value("faceplates").toArray();
    types.append(QJsonObject{{"id", pumpTypeId}, {"name", "Насос · конструктор"}, {"width", 870}, {"height", 430},
        {"ports", QJsonArray{runPort, levelPort, outputPort}},
        {"properties", QJsonArray{property("Имя насоса", pumpTitle, "title"), property("Цвет панели", pumpBase, "background")}},
        {"items", QJsonArray{pumpBase, pumpTitle, pumpArt, pumpRunArt, pumpState, pumpFault, startLabel, startArea, stopButton,
                             impeller}}});
    project.insert("faceplates", types);
    project.insert("screens", QJsonArray{
        QJsonObject{{"id", main}, {"name", "Обзор станции"}, {"items", QJsonArray{
            widget("label", "Параметры процесса", {}, 16, 10, 500, 66),
            widget("gauge", "Уровень бака", level, 16, 90, 330, 136),
            pressureDisplay,
            widget("value", "Обороты насоса", speed, 624, 90, 250, 136),
            widget("indicator", "Состояние насоса", active, 888, 90, 296, 136),
            overviewTrend,
            widget("value", "Уставка оборотов", setpoint, 870, 240, 314, 220), link, galleryJump}}},
        QJsonObject{{"id", commands}, {"name", "Управление"}, {"width", 1200}, {"height", 850}, {"items", QJsonArray{
            widget("label", "Управление насосом", {}, 32, 20, 500, 72),
            widget("indicator", "Сигнал работы", active, 32, 108, 310, 66),
            widget("value", "Текущая уставка", setpoint, 366, 108, 310, 66),
            on, off, boost,
            binary("switch", "Тумблер насоса", command, active, 32, 350, 360, 150),
            binary("valve", "Клапан линии", valveCommand, valveState, 420, 350, 360, 150),
            selector(808, 350, 360, 150),
            control("setpoint", "Ввод оборотов", 32, 522, 360, 202),
            control("slider", "Ползунок оборотов", 420, 522, 360, 202),
            control("knob", "Регулятор оборотов", 808, 522, 360, 202), returnLink, holdValve, readSetpoint, copySpeed}}},
        QJsonObject{{"id", Project::id()}, {"name", "Приборы"}, {"items", QJsonArray{
            widget("label", "Приборы станции", {}, 32, 18, 600, 72),
            instrument("dial", "Давление линии", pressure, 32, 102, 6, 2, "#63dce2"),
            instrument("column", "Температура", temperature, 419, 102, 100, 1, "#ff9d75"),
            instrument("ring", "Заполнение бака", level, 806, 102, 100, 1, "#65dfb4"),
            instrument("segments", "Обороты насоса", speed, 32, 378, 3000, 0, "#99e4ff"),
            instrument("signal", "Сигнал работы", active, 419, 378, 1, 0, "#72e790"),
            instrument("compass", "Угол положения", heading, 806, 378, 360, 0, "#ecbd70")}}},
        QJsonObject{{"id", gallery}, {"name", "Все элементы"}, {"width", 1600}, {"height", 1010}, {"items", QJsonArray{
            widget("label", "Все элементы", {}, 16, 12, 750, 54),
            galleryPressure,
            widget("indicator", "Индикатор насоса", active, 332, 72, 304, 148),
            widget("gauge", "Уровень бака", level, 648, 72, 304, 148),
            schematic,
            widget("alarm", "Тревога по уровню", level, 1280, 72, 304, 148),
            widget("tank", "Заполнение бака", level, 16, 230, 304, 205),
            widget("pump", "Состояние насоса", active, 332, 230, 304, 205),
            galleryTrend,
            instrument("bargraph", "Линейная шкала давления", pressure, 1280, 230, 6, 1, "#63dce2", 304, 205),
            instrument("dial", "Манометр", pressure, 16, 445, 6, 2, "#63dce2", 304, 190),
            instrument("column", "Термометр", temperature, 332, 445, 100, 1, "#ff9d75", 304, 190),
            instrument("ring", "Сегментная шкала", level, 648, 445, 100, 1, "#65dfb4", 304, 190),
            instrument("compass", "Компас", heading, 964, 445, 360, 0, "#ecbd70", 304, 190),
            instrument("segments", "Цифровое табло", speed, 1280, 445, 3000, 0, "#99e4ff", 304, 190),
            instrument("signal", "Сигнальная лампа", active, 16, 645, 1, 0, "#72e790", 304, 153),
            widget("motor", "Двигатель насоса", active, 332, 645, 304, 153),
            binary("valve", "Клапан линии", valveCommand, valveState, 648, 645, 304, 153),
            widget("pipe", "Трубопровод", valveState, 950, 700, 318, 52),
            galleryCommand,
            binary("switch", "Тумблер насоса", command, active, 16, 808, 304, 186),
            control("setpoint", "Ввод оборотов", 332, 808, 304, 186),
            control("slider", "Ползунок оборотов", 648, 808, 304, 186),
            control("knob", "Поворотный регулятор", 964, 808, 304, 186),
            selector(1280, 808, 304, 186), galleryReturn}}},
        QJsonObject{{"id", Project::id()}, {"name", "Типовые панели"}, {"items", QJsonArray{
            widget("label", "Типовые панели", {}, 32, 15, 520, 62),
            widget("label", "Насос", {}, 32, 101, 544, 52),
            widget("label", "Клапан", {}, 624, 101, 544, 52),
            panel("Панель насоса", 32, speed, command, active,
                {{headingText.value("id").toString(), "Параметры насоса"},
                 {headingColor.value("id").toString(), "#124c65"},
                 {valueBackground.value("id").toString(), "#e7f6ee"}},
                {{showAlarm.value("id").toString(), QJsonObject{{"tag", level.value("id")},
                    {"op", "alarm"}, {"value", true}, {"otherwise", false}}},
                 {alarmColor.value("id").toString(), QJsonObject{{"tag", level.value("id")},
                    {"op", "alarm"}, {"value", "#b32636"}}}}),
            panel("Панель клапана", 624, pressure, valveCommand, valveState,
                {{valueBackground.value("id").toString(), "#fff2df"}, {valueColor.value("id").toString(), "#994922"},
                 {showFeedback.value("id").toString(), false}})}}},
        QJsonObject{{"id", Project::id()}, {"name", "Свой насос"}, {"items", QJsonArray{
            widget("label", "Насос из конструктора", {}, 165, 15, 800, 58),
            widget("label", "Редактор · двойной щелчок по насосу открывает его слои", {}, 165, 65, 870, 52),
            pumpInstance,
            widget("label", "При уровне от 60 % мигает аварийная плашка", {}, 165, 558, 870, 52)}}}});
    return project;
}
