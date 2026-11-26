#include "demo.h"
#include "engine.h"
#include "modbus.h"
#include "project.h"
#include "runtime.h"
#include "studio.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QModbusTcpServer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QWebSocketServer>
#include <QPointer>
#include <QtTest>
#include <memory>
#include <vector>

namespace {
QJsonObject source(const QString &name, int port, bool write = false)
{
    return {{"id", Project::id()}, {"name", name}, {"driver", "modbus-tcp"},
            {"settings", QJsonObject{{"host", "127.0.0.1"}, {"port", port}, {"pollMs", 100},
                                     {"timeoutMs", 200}, {"allowWrite", write}}}};
}

QJsonObject tag(const QJsonObject &source, const QString &name, const QString &table,
                const QString &type, int offset, bool write = false)
{
    return {{"id", Project::id()}, {"source", source.value("id")}, {"name", name}, {"units", "bar"},
            {"scale", 1}, {"bias", 0}, {"archiveMs", 100}, {"writable", write},
            {"address", QJsonObject{{"unit", 1}, {"offset", offset}, {"table", table},
                                    {"type", type}, {"order", "ABCD"}}}};
}

QString id(const QJsonObject &object) { return object.value("id").toString(); }

bool listen(QModbusTcpServer &server, int &port)
{
    QTcpServer reserve;
    if (!reserve.listen(QHostAddress::LocalHost, 0))
        return false;
    port = reserve.serverPort();
    reserve.close();
    server.setServerAddress(1);
    server.setConnectionParameter(QModbusDevice::NetworkAddressParameter, "127.0.0.1");
    server.setConnectionParameter(QModbusDevice::NetworkPortParameter, port);
    server.setMap({{QModbusDataUnit::HoldingRegisters, {QModbusDataUnit::HoldingRegisters, 0, quint16(4)}},
                   {QModbusDataUnit::Coils, {QModbusDataUnit::Coils, 0, quint16(2)}},
                   {QModbusDataUnit::DiscreteInputs, {QModbusDataUnit::DiscreteInputs, 0, quint16(2)}},
                   {QModbusDataUnit::InputRegisters, {QModbusDataUnit::InputRegisters, 0, quint16(4)}}});
    return server.connectDevice();
}
}

class CoreTest final : public QObject
{
    Q_OBJECT
private slots:
    void projectRoundTrip();
    void registerConversion();
    void persistenceAndEditing();
    void multipleSourcesAndArchive();
    void parallelSourcesLoad();
    void timeoutAndRecovery();
    void modbusOrderedActions();
    void widgetWritesFromTag();
    void engineCatalogAndFrames();
    void engineWebSocket();
    void widgetAppearance();
    void screenSettings();
    void faceplateStudio();
    void faceplateDuplication();
    void faceplateTransfer();
    void faceplateImportReplacement();
    void faceplateTypeChange();
    void faceplateInstanceProperties();
    void faceplatePropertyBindings();
    void operatorWidgetSettings();
    void faceplateSchema();
    void customWidgetConstructor();
    void writeConfirmationSettings();
    void widgetActionSettings();
};

void CoreTest::projectRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("проект.scada.json");
    const auto example = DemoServer::project();
    QString error;
    QVERIFY2(Project::save(path, example, error), qPrintable(error));
    QJsonObject pulled;
    QVERIFY2(Project::load(path, pulled, error), qPrintable(error));
    QCOMPARE(pulled, example);
    pulled.insert("version", 2);
    QVERIFY(!Project::save(path, pulled, error));
    QVERIFY(Project::load(path, pulled, error));
    QCOMPARE(pulled, example);
    auto tags = pulled.value("tags").toArray();
    auto first = tags.first().toObject();
    first.insert("source", Project::id());
    tags.replace(0, first);
    pulled.insert("tags", tags);
    QVERIFY(!Project::validate(pulled).isEmpty());
    QFile malformed(dir.filePath("broken.scada.json"));
    QVERIFY(malformed.open(QIODevice::WriteOnly));
    QCOMPARE(malformed.write("{broken"), 7);
    malformed.close();
    const auto before = pulled;
    QVERIFY(!Project::load(malformed.fileName(), pulled, error));
    QCOMPARE(pulled, before);
}

void CoreTest::registerConversion()
{
    const auto config = source("A", 502);
    double value = 0;
    auto signed16 = tag(config, "температура", "input", "i16", 1);
    signed16.insert("scale", 0.1);
    signed16.insert("bias", -2);
    QVERIFY(Modbus::decode(signed16, {0xff9c}, value));
    QCOMPARE(value, -12.0);
    QVERIFY(Modbus::decode(tag(config, "бит", "coils", "bool", 0), {1}, value));
    QCOMPARE(value, 1.0);
    QVERIFY(Modbus::decode(tag(config, "длинный", "holding", "i32", 0), {0xffff, 0xff38}, value));
    QCOMPARE(value, -200.0);
    auto large = tag(config, "счётчик", "holding", "u32", 0, true);
    QVERIFY(Modbus::decode(large, {0xffff, 0xffff}, value));
    QCOMPARE(value, 4294967295.0);
    auto floatTag = tag(config, "давление", "holding", "f32", 0, true);
    for (const auto &order : {"ABCD", "BADC", "CDAB", "DCBA"}) {
        auto address = floatTag.value("address").toObject();
        address.insert("order", order);
        floatTag.insert("address", address);
        QList<quint16> words;
        QString error;
        QVERIFY2(Modbus::encode(floatTag, 123.5, words, error), qPrintable(error));
        if (QString(order) == "ABCD") QCOMPARE(words, (QList<quint16>{0x42f7, 0}));
        if (QString(order) == "BADC") QCOMPARE(words, (QList<quint16>{0xf742, 0}));
        if (QString(order) == "CDAB") QCOMPARE(words, (QList<quint16>{0, 0x42f7}));
        if (QString(order) == "DCBA") QCOMPARE(words, (QList<quint16>{0, 0xf742}));
        QVERIFY(Modbus::decode(floatTag, words, value));
        QCOMPARE(value, 123.5);
    }
    QVERIFY(!Modbus::decode(tag(config, "nan", "holding", "f32", 0), {0x7fc0, 0}, value));
    QList<quint16> encoded;
    QString error;
    QVERIFY(!Modbus::encode(large, 4294967296.0, encoded, error));
    QVERIFY(!Modbus::encode(large, -1, encoded, error));
    QVERIFY(!Modbus::encode(large, 0.5, encoded, error));
    QVERIFY(!Modbus::validateTag(tag(config, "плохой адрес", "input", "f32", 65535)).isEmpty());
    QVERIFY(!Modbus::validateTag(tag(config, "только чтение", "input", "u16", 0, true)).isEmpty());
}

void CoreTest::persistenceAndEditing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Studio studio;
    studio.newProject(false);
    auto newSource = source("Источники A", 15002);
    newSource.remove("id");
    QVERIFY(studio.setSource(newSource.toVariantMap()));
    const auto sourceCopy = QJsonObject::fromVariantMap(studio.sources().first().toMap());
    auto newTag = tag(sourceCopy, "Тег A", "input", "u16", 1);
    newTag.remove("id");
    QVERIFY(studio.setTag(newTag.toVariantMap()));
    const auto tagId = studio.tags().first().toMap().value("id").toString();
    QVERIFY(!studio.removeSource(id(sourceCopy)));
    studio.setScreen({}, "Диагностика");
    const auto screen = studio.screenId();
    studio.addWidget("value", 1150, 690);
    QVERIFY(studio.configureWidget({{"tag", tagId}, {"title", "Показание А"}}));
    const QVariantList rules{QVariantMap{{"op", "gte"}, {"limit", 10}, {"value", "#981c18"}}};
    QVERIFY(studio.configureWidget({{"appearance", QVariantMap{{"tag", tagId}, {"background", "#dedede"}, {"rules", rules}}}}));
    studio.moveWidget(studio.selectedId(), 40, 60, 240, 120);
    QCOMPARE(studio.widget().value("x").toInt(), 40);
    studio.duplicateWidget();
    QCOMPARE(studio.items().size(), 2);
    studio.undo();
    QCOMPARE(studio.items().size(), 1);
    studio.redo();
    QCOMPARE(studio.items().size(), 2);
    const auto first = studio.items().at(0).toMap();
    const auto second = studio.items().at(1).toMap();
    studio.selectWidget(first.value("id").toString());
    studio.selectWidget(second.value("id").toString(), true);
    QCOMPARE(studio.selectedIds().size(), 2);
    studio.groupWidgets(true);
    studio.selectWidget({});
    studio.selectWidget(second.value("id").toString());
    QCOMPARE(studio.selectedIds().size(), 2);
    studio.moveWidget(studio.selectedId(), 100, 110, second.value("w").toDouble(), second.value("h").toDouble());
    QCOMPARE(studio.items().first().toMap().value("x").toInt(), 80);
    QCOMPARE(studio.items().last().toMap().value("x").toInt(), 100);
    studio.alignWidgets(true);
    QCOMPARE(studio.items().first().toMap().value("y"), studio.items().last().toMap().value("y"));
    QVERIFY(studio.saveTemplate("Группа датчиков"));
    studio.insertTemplate(studio.templates().first().toMap().value("id").toString());
    QCOMPARE(studio.items().size(), 4);
    QCOMPARE(studio.selectedIds().size(), 2);
    QVERIFY(studio.widget().value("tag").toString().isEmpty());
    QVERIFY(!studio.widget().value("appearance").toMap().contains("tag"));
    QCOMPARE(studio.widget().value("appearance").toMap().value("rules").toList(), rules);
    studio.removeWidget();
    QCOMPARE(studio.items().size(), 2);
    const auto path = QUrl::fromLocalFile(dir.filePath("установка.scada.json"));
    QVERIFY(studio.save(path));
    QVERIFY(!studio.modified());
    studio.newProject(false);
    QVERIFY(studio.open(path));
    studio.selectScreen(screen);
    QCOMPARE(studio.items().size(), 2);
    QCOMPARE(studio.items().first().toMap().value("tag").toString(), tagId);
    QCOMPARE(studio.templates().size(), 1);
    QVERIFY(studio.removeTag(tagId));
    QVERIFY(studio.items().first().toMap().value("tag").toString().isEmpty());
    QVERIFY(!studio.items().first().toMap().value("appearance").toMap().contains("tag"));
}

void CoreTest::multipleSourcesAndArchive()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QModbusTcpServer first, second;
    int firstPort = 0, secondPort = 0;
    QVERIFY(listen(first, firstPort));
    QVERIFY(listen(second, secondPort));
    const auto a = source("A", firstPort, true);
    const auto b = source("B", secondPort);
    auto level = tag(a, "Уровень", "holding", "u16", 0, true);
    level.insert("alarm", QJsonObject{{"direction", "high"}, {"limit", 70}, {"hysteresis", 5}});
    const auto other = tag(b, "Второй", "input", "u16", 0);
    const auto valid = tag(a, "Валидный", "holding", "u16", 3);
    const auto invalid = tag(a, "За пределом", "holding", "u16", 4);
    const auto coil = tag(a, "Бит", "coils", "bool", 0, true);
    const auto discrete = tag(a, "Вход", "discrete", "bool", 0);
    auto project = Project::empty();
    project.insert("sources", QJsonArray{a, b});
    project.insert("tags", QJsonArray{level, other, valid, invalid, coil, discrete});
    QCOMPARE(Project::validate(project), QString{});
    QVERIFY(first.setData(QModbusDataUnit::HoldingRegisters, 0, 74));
    QVERIFY(first.setData(QModbusDataUnit::HoldingRegisters, 3, 321));
    QVERIFY(first.setData(QModbusDataUnit::DiscreteInputs, 0, 1));
    QVERIFY(second.setData(QModbusDataUnit::InputRegisters, 0, 19));
    Runtime runtime;
    QString error;
    const auto database = dir.filePath("history.sqlite");
    QVERIFY2(runtime.openArchive(database, id(project), error), qPrintable(error));
    const auto from = QDateTime::currentMSecsSinceEpoch() - 1000;
    const auto read = [&](const QJsonObject &tag) { return runtime.readings().value(id(tag)).toMap(); };
    QSignalSpy written(&runtime, &Runtime::writeResult);
    QSignalSpy alarmChanges(&runtime, &Runtime::alarmsChanged);
    runtime.start(project);
    QTRY_COMPARE_WITH_TIMEOUT(read(level).value("value").toInt(), 74, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(read(other).value("value").toInt(), 19, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(read(discrete).value("value").toInt(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(read(valid).value("value").toInt(), 321, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(read(invalid).value("quality").toString(), QString("error"), 3000);
    QCOMPARE(runtime.alarms().size(), 1);
    QCOMPARE(alarmChanges.size(), 1);
    QCOMPARE(runtime.acknowledge(id(level)), QString{});
    QVERIFY(runtime.alarms().first().toMap().value("ack").toBool());
    QCOMPARE(alarmChanges.size(), 2);
    QVERIFY(!runtime.requestWrite(id(other), 10).isEmpty());
    QCOMPARE(runtime.requestWrite(id(coil), 1), QString{});
    QTRY_COMPARE_WITH_TIMEOUT(written.size(), 1, 2500);
    QVERIFY(written.at(0).at(1).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(read(coil).value("value").toInt(), 1, 2500);
    QCOMPARE(runtime.requestWrite(id(level), 66), QString{});
    QTRY_COMPARE_WITH_TIMEOUT(read(level).value("value").toInt(), 66, 2500);
    QCOMPARE(runtime.alarms().size(), 1);
    QCOMPARE(alarmChanges.size(), 2);
    QCOMPARE(runtime.requestWrite(id(level), 64), QString{});
    QTRY_COMPARE_WITH_TIMEOUT(read(level).value("value").toInt(), 64, 2500);
    QTRY_COMPARE_WITH_TIMEOUT(runtime.alarms().size(), 0, 2500);
    QCOMPARE(alarmChanges.size(), 3);
    first.disconnectDevice();
    QTRY_COMPARE_WITH_TIMEOUT(read(level).value("quality").toString(), QString("stale"), 3000);
    QCOMPARE(read(level).value("value").toInt(), 64);
    QVERIFY(second.setData(QModbusDataUnit::InputRegisters, 0, 102));
    QTRY_COMPARE_WITH_TIMEOUT(read(other).value("value").toInt(), 102, 3000);
    QVERIFY(first.connectDevice());
    QTRY_COMPARE_WITH_TIMEOUT(read(level).value("quality").toString(), QString("good"), 3000);
    runtime.stop();
    const auto to = QDateTime::currentMSecsSinceEpoch() + 1000;
    const auto csv = dir.filePath("archive.csv");
    QCOMPARE(runtime.exportCsv(csv, id(level), from, to), QString{});
    QFile exported(csv);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    const auto content = exported.readAll();
    QVERIFY(content.startsWith("timestamp_utc,value,quality\r\n"));
    QVERIFY(content.contains(",74,good"));
    QVERIFY(content.contains(",,stale"));
    Runtime reopened;
    QVERIFY(reopened.openArchive(database, id(project), error));
    const auto plot = reopened.series(id(level), from, to, 1500);
    QVERIFY(!plot.isEmpty());
    const auto bounds = reopened.archiveBounds({id(level), id(other)});
    QVERIFY(bounds.value("to").toLongLong() > bounds.value("from").toLongLong());
    const auto levelBounds = reopened.archiveBounds({id(level)});
    const auto firstTime = levelBounds.value("from").toLongLong();
    const auto stoppedAt = levelBounds.value("to").toLongLong();
    const auto endPlot = reopened.series(id(level), stoppedAt, stoppedAt + 1, 1500);
    QCOMPARE(endPlot.size(), 1);
    const auto endPoint = endPlot.first().toMap();
    QVERIFY(endPoint.value("gap").toBool());
    QVERIFY(!endPoint.value("hasValue").toBool());
    QVERIFY(reopened.archiveSamples({id(level)}, firstTime - 1).isEmpty());
    const auto firstSample = reopened.archiveSamples({id(level)}, firstTime).value(id(level)).toMap();
    QCOMPARE(firstSample.value("value").toInt(), 74);
    QCOMPARE(firstSample.value("time").toLongLong(), firstTime);
    const auto endSamples = reopened.archiveSamples({id(level), id(other)}, to);
    QCOMPARE(endSamples.size(), 2);
    QCOMPARE(endSamples.value(id(level)).toMap().value("quality").toString(), QString("stale"));
    QVERIFY(!endSamples.value(id(level)).toMap().value("hasValue").toBool());
    const auto events = reopened.events();
    QVERIFY(events.size() >= 9);
    QVERIFY(reopened.openArchive(database, Project::id(), error));
    QVERIFY(reopened.series(id(level), from, to, 1500).isEmpty());
    QVERIFY(reopened.archiveBounds({id(level)}).isEmpty());
    QVERIFY(reopened.archiveSamples({id(level)}, to).isEmpty());
}

void CoreTest::parallelSourcesLoad()
{
    constexpr int sources = 12;
    constexpr int tagsPerSource = 16;
    QJsonArray allSources;
    QJsonArray allTags;
    std::vector<std::unique_ptr<QModbusTcpServer>> servers;
    for (int i = 0; i < sources; ++i) {
        auto server = std::make_unique<QModbusTcpServer>();
        int port = 0;
        QVERIFY(listen(*server, port));
        QVERIFY(server->setMap({{QModbusDataUnit::HoldingRegisters,
                                 {QModbusDataUnit::HoldingRegisters, 0, quint16(tagsPerSource)}}}));
        const auto endpoint = source(QString("Источник %1").arg(i), port);
        allSources.append(endpoint);
        for (int j = 0; j < tagsPerSource; ++j) {
            QVERIFY(server->setData(QModbusDataUnit::HoldingRegisters, j, quint16(i * 100 + j)));
            auto point = tag(endpoint, QString("Сигнал %1.%2").arg(i).arg(j), "holding", "u16", j);
            point.insert("archiveMs", 0);
            allTags.append(point);
        }
        servers.push_back(std::move(server));
    }
    auto project = Project::empty();
    project.insert("sources", allSources);
    project.insert("tags", allTags);
    QCOMPARE(Project::validate(project), QString{});
    Runtime runtime;
    QString error;
    QVERIFY(runtime.openArchive({}, id(project), error));
    runtime.start(project);
    const auto complete = [&] {
        const auto readings = runtime.readings();
        for (const auto &value : readings) {
            if (value.toMap().value("quality") != "good")
                return false;
        }
        return readings.size() == sources * tagsPerSource;
    };
    QTRY_VERIFY_WITH_TIMEOUT(complete(), 3500);
    const auto result = runtime.readings();
    for (int i = 0; i < sources; ++i) {
        for (int j = 0; j < tagsPerSource; ++j) {
            const auto point = allTags.at(i * tagsPerSource + j).toObject();
            QCOMPARE(result.value(id(point)).toMap().value("value").toInt(), i * 100 + j);
        }
    }
}

void CoreTest::modbusOrderedActions()
{
    QModbusTcpServer server;
    int port = 0;
    QVERIFY(listen(server, port));
    auto endpoint = source("Пульт", port, true);
    auto settings = endpoint.value("settings").toObject();
    settings.insert("pollMs", 60000);
    endpoint.insert("settings", settings);
    const auto command = tag(endpoint, "Команда", "coils", "bool", 0, true);
    const auto feedback = tag(endpoint, "Обратная связь", "input", "u16", 0);
    const auto neighbor = tag(endpoint, "Соседний регистр", "input", "u16", 1);
    QVERIFY(server.setData(QModbusDataUnit::InputRegisters, 0, 10));
    QVERIFY(server.setData(QModbusDataUnit::InputRegisters, 1, 20));
    auto project = Project::empty();
    project.insert("sources", QJsonArray{endpoint});
    project.insert("tags", QJsonArray{command, feedback, neighbor});

    QList<int> coilChanges;
    QList<int> feedbackOnWriteResult;
    Runtime runtime;
    QString error;
    QVERIFY2(runtime.openArchive({}, id(project), error), qPrintable(error));
    runtime.start(project);
    const auto value = [&](const QJsonObject &tag) {
        return runtime.readings().value(id(tag)).toMap().value("value").toInt();
    };
    QTRY_COMPARE_WITH_TIMEOUT(value(neighbor), 20, 2000);
    QCOMPARE(value(feedback), 10);
    QVERIFY(server.setData(QModbusDataUnit::InputRegisters, 0, 50));
    QVERIFY(server.setData(QModbusDataUnit::InputRegisters, 1, 60));
    QCOMPARE(runtime.requestRead(id(feedback)), QString{});
    QTRY_COMPARE_WITH_TIMEOUT(value(feedback), 50, 2000);
    QCOMPARE(value(neighbor), 20);

    connect(&server, &QModbusTcpServer::dataWritten, &runtime, [&](auto table, int address, int) {
        if (table != QModbusDataUnit::Coils || address != 0)
            return;
        quint16 coil = 0;
        if (server.data(table, address, &coil)) {
            coilChanges.append(coil != 0 ? 1 : 0);
            server.setData(QModbusDataUnit::InputRegisters, 0, coil ? 101 : 102);
        }
    });
    connect(&runtime, &Runtime::writeResult, this, [&](const QString &tag, bool accepted, const QString &) {
        if (accepted && tag == id(command))
            feedbackOnWriteResult.append(value(feedback));
    });
    QSignalSpy results(&runtime, &Runtime::writeResult);
    QCOMPARE(runtime.requestWrite(id(command), 1), QString{});
    QCOMPARE(runtime.requestRead(id(feedback)), QString{});
    QCOMPARE(runtime.requestWrite(id(command), 0), QString{});
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 2, 2000);
    QCOMPARE(coilChanges, (QList<int>{1, 0}));
    QCOMPARE(feedbackOnWriteResult, (QList<int>{50, 101}));

    QCOMPARE(runtime.requestWrite(id(command), 1), QString{});
    QCOMPARE(runtime.requestWrite(id(command), 0), QString{});
    runtime.stop();
    QCOMPARE(results.size(), 4);
    QVERIFY(!results.at(2).at(1).toBool());
    QVERIFY(!results.at(3).at(1).toBool());
    QVERIFY(!runtime.requestRead(id(feedback)).isEmpty());
    runtime.start(project);
    QTRY_COMPARE_WITH_TIMEOUT(value(neighbor), 60, 2000);
    QCOMPARE(coilChanges, (QList<int>{1, 0}));
}

void CoreTest::widgetWritesFromTag()
{
    QModbusTcpServer originServer;
    QModbusTcpServer targetServer;
    int originPort = 0;
    int targetPort = 0;
    QVERIFY(listen(originServer, originPort));
    QVERIFY(listen(targetServer, targetPort));
    const auto slowSource = [](QJsonObject endpoint) {
        auto settings = endpoint.value("settings").toObject();
        settings.insert("pollMs", 60000);
        endpoint.insert("settings", settings);
        return endpoint;
    };
    const auto originSource = slowSource(source("Датчики", originPort));
    const auto targetSource = slowSource(source("Уставки", targetPort, true));
    auto origin = tag(originSource, "Обороты", "input", "u16", 0);
    origin.insert("scale", 2);
    origin.insert("bias", 10);
    auto target = tag(targetSource, "Уставка", "holding", "u16", 0, true);
    target.insert("scale", 0.5);
    const auto local = tag(targetSource, "Локальное значение", "input", "u16", 0);
    const auto floatTag = tag(originSource, "Плавающее значение", "input", "f32", 2);
    QVERIFY(originServer.setData(QModbusDataUnit::InputRegisters, 0, 10));
    QVERIFY(originServer.setData(QModbusDataUnit::InputRegisters, 2, 0x3f80));
    QVERIFY(targetServer.setData(QModbusDataUnit::InputRegisters, 0, 3));
    QVERIFY(targetServer.setData(QModbusDataUnit::HoldingRegisters, 0, 300));
    auto project = Project::empty();
    project.insert("sources", QJsonArray{originSource, targetSource});
    project.insert("tags", QJsonArray{origin, target, local, floatTag});

    QList<int> rawWrites;
    Runtime runtime;
    connect(&targetServer, &QModbusTcpServer::dataWritten, &runtime, [&](auto table, int address, int) {
        quint16 value = 0;
        if (table == QModbusDataUnit::HoldingRegisters && address == 0 && targetServer.data(table, address, &value))
            rawWrites.append(value);
    });
    QString error;
    QVERIFY2(runtime.openArchive({}, id(project), error), qPrintable(error));
    runtime.start(project);
    const auto reading = [&](const QJsonObject &point) { return runtime.readings().value(id(point)).toMap(); };
    QTRY_COMPARE_WITH_TIMEOUT(reading(origin).value("value").toDouble(), 30.0, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(reading(target).value("value").toDouble(), 150.0, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(reading(local).value("value").toDouble(), 3.0, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(reading(floatTag).value("value").toDouble(), 1.0, 2000);
    const auto copyFrom = [&](const QJsonObject &point) {
        return QVariantMap{{"action", "write"}, {"tag", id(target)}, {"valueTag", id(point)}};
    };
    const auto write = [&](double value) {
        return QVariantMap{{"action", "write"}, {"tag", id(target)}, {"value", value}};
    };
    QSignalSpy results(&runtime, &Runtime::writeResult);
    QSignalSpy warnings(&runtime, &Runtime::warning);

    QVERIFY(originServer.setData(QModbusDataUnit::InputRegisters, 0, 30));
    QCOMPARE(reading(origin).value("value").toDouble(), 30.0);
    runtime.runWidgetActions("Кнопка", {copyFrom(origin)});
    runtime.runWidgetActions("Кнопка", {write(25)});
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 2, 2000);
    QCOMPARE(rawWrites, (QList<int>{140, 50}));
    QCOMPARE(reading(origin).value("value").toDouble(), 70.0);

    QVERIFY(targetServer.setData(QModbusDataUnit::InputRegisters, 0, 55));
    runtime.runWidgetActions("Кнопка", {copyFrom(local)});
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 3, 2000);
    QCOMPARE(rawWrites.last(), 110);

    QVERIFY(originServer.setData(QModbusDataUnit::InputRegisters, 2, 0x7fc0));
    runtime.runWidgetActions("Кнопка", {copyFrom(floatTag), write(30)});
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 4, 2000);
    QCOMPARE(rawWrites, (QList<int>{140, 50, 110, 60}));
    QCOMPARE(reading(floatTag).value("quality").toString(), QString("error"));
    QCOMPARE(warnings.size(), 1);

    runtime.runWidgetActions("Кнопка", {copyFrom(origin), write(40)});
    runtime.stop();
    runtime.start(project);
    QTRY_COMPARE_WITH_TIMEOUT(reading(target).value("value").toDouble(), 30.0, 2000);
    QCOMPARE(rawWrites, (QList<int>{140, 50, 110, 60}));
}

void CoreTest::timeoutAndRecovery()
{
    QTcpServer silent;
    QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
    const auto port = silent.serverPort();
    const auto config = source("Silent", port);
    const auto point = tag(config, "Регистры", "holding", "u16", 0);
    auto project = Project::empty();
    project.insert("sources", QJsonArray{config});
    project.insert("tags", QJsonArray{point});
    Runtime runtime;
    QString error;
    QVERIFY(runtime.openArchive({}, id(project), error));
    runtime.start(project);
    QTRY_COMPARE_WITH_TIMEOUT(runtime.readings().value(id(point)).toMap().value("quality").toString(), QString("stale"), 2500);
    QVERIFY(silent.hasPendingConnections());
    auto *socket = silent.nextPendingConnection();
    socket->abort();
    delete socket;
    silent.close();
    QModbusTcpServer server;
    server.setServerAddress(1);
    server.setConnectionParameter(QModbusDevice::NetworkAddressParameter, "127.0.0.1");
    server.setConnectionParameter(QModbusDevice::NetworkPortParameter, port);
    server.setMap({{QModbusDataUnit::HoldingRegisters, {QModbusDataUnit::HoldingRegisters, 0, quint16(2)}}});
    QVERIFY(server.setData(QModbusDataUnit::HoldingRegisters, 0, 456));
    QVERIFY(server.connectDevice());
    QTRY_COMPARE_WITH_TIMEOUT(runtime.readings().value(id(point)).toMap().value("value").toInt(), 456, 3000);
}

namespace {
QJsonObject engineSource(int port)
{
    return {{"id", Project::id()}, {"name", "ESP"}, {"driver", "engine-json"},
            {"settings", QJsonObject{{"transport", "websocket"}, {"url", QString("ws://127.0.0.1:%1/ws").arg(port)},
                                     {"timeoutMs", 650}, {"allowWrite", false}}}};
}

QJsonObject engineTag(const QJsonObject &endpoint, const QString &name, const QString &path, const QString &type, int archive = 200)
{
    return {{"id", Project::id()}, {"source", endpoint.value("id")}, {"name", name}, {"units", ""},
            {"scale", 1}, {"bias", 0}, {"archiveMs", archive}, {"writable", false},
            {"address", QJsonObject{{"type", type}, {"path", path}}}};
}
}

void CoreTest::engineCatalogAndFrames()
{
    Engine::SerialFrames frames;
    QVERIFY(frames.read("boot diagnostic\r\n@{\"type\":").isEmpty());
    QCOMPARE(frames.read("\"identity\"}\r\nnoise\n@{}\n"), (QList<QByteArray>{"{\"type\":\"identity\"}", "{}"}));
    QVERIFY(frames.read(QByteArray("@") + QByteArray(5000, 'a')).isEmpty());
    QCOMPARE(frames.read("\n@{\"v\":1}\n"), (QList<QByteArray>{"{\"v\":1}"}));
    const auto v = Engine::field(QJsonObject{{"values", QJsonObject{{"rpm", 1200}}}}, {"values", "rpm"});
    QCOMPARE(v.toInt(), 1200);
    Studio studio;
    studio.newProject(false);
    auto endpoint = engineSource(15115);
    endpoint.remove("id");
    QVERIFY(studio.setSource(endpoint.toVariantMap()));
    const auto first = studio.sources().first().toMap().value("id").toString();
    QVERIFY(studio.importEngineTags(first));
    const auto count = studio.tags().size();
    QVERIFY(count >= 25);
    QVERIFY(studio.importEngineTags(first));
    QCOMPARE(studio.tags().size(), count);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto table = QUrl::fromLocalFile(temp.filePath("table.json"));
    QVERIFY(studio.exportEngineTags(first, table));
    endpoint.insert("name", "ESP второй");
    QVERIFY(studio.setSource(endpoint.toVariantMap()));
    const auto second = studio.sources().last().toMap().value("id").toString();
    QVERIFY(studio.importEngineTags(second, table));
    QCOMPARE(studio.tags().size(), count * 2);
}

void CoreTest::engineWebSocket()
{
    QWebSocketServer server("ESP fixture", QWebSocketServer::NonSecureMode);
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QPointer<QWebSocket> socket;
    QStringList messages;
    int connections = 0;
    connect(&server, &QWebSocketServer::newConnection, this, [&] {
        ++connections;
        socket = server.nextPendingConnection();
        socket->setParent(&server);
        connect(socket, &QWebSocket::textMessageReceived, this, [&](const QString &message) {
            messages.append(message);
            socket->sendTextMessage(R"({"type":"identity","version":1,"deviceId":"engine-abcd"})");
            socket->sendTextMessage(R"({"type":"telemetry","version":1,"session":"boot-a","ts":1,"state":"idle","values":{"rpm":3600,"spark1":true,"temp1":null}})");
        });
    });
    const auto endpoint = engineSource(server.serverPort());
    const auto rpm = engineTag(endpoint, "RPM", "values.rpm", "number");
    const auto spark = engineTag(endpoint, "Spark", "values.spark1", "bool");
    const auto temperature = engineTag(endpoint, "Temperature", "values.temp1", "number");
    const auto state = engineTag(endpoint, "State", "state", "text", 0);
    auto config = Project::empty();
    config.insert("sources", QJsonArray{endpoint});
    config.insert("tags", QJsonArray{rpm, spark, temperature, state});
    QCOMPARE(Project::validate(config), QString{});
    Runtime runtime;
    QString error;
    QVERIFY(runtime.openArchive({}, id(config), error));
    const auto reading = [&](const auto &tag) { return runtime.readings().value(id(tag)).toMap(); };
    runtime.start(config);
    QTRY_COMPARE_WITH_TIMEOUT(reading(rpm).value("value").toInt(), 3600, 2000);
    QCOMPARE(reading(spark).value("value").toInt(), 1);
    QCOMPARE(reading(state).value("value").toString(), QString("idle"));
    QCOMPARE(reading(temperature).value("quality").toString(), QString("stale"));
    QCOMPARE(messages.size(), 1);
    QVERIFY(messages.first().contains("engine-local-monitor"));
    QVERIFY(!runtime.requestWrite(id(rpm), 500).isEmpty());
    QVERIFY(!runtime.requestRead(id(rpm)).isEmpty());
    socket->sendTextMessage(R"({"type":"telemetry","version":1,"session":"boot-a","ts":1,"values":{"rpm":1}})");
    QTest::qWait(100);
    QCOMPARE(reading(rpm).value("value").toInt(), 3600);
    QTRY_COMPARE_WITH_TIMEOUT(reading(rpm).value("quality").toString(), QString("stale"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(connections >= 2 && reading(rpm).value("quality").toString() == "good", 3500);
    runtime.stop();
    QVERIFY(!runtime.archiveBounds({id(rpm)}).isEmpty());
    QVERIFY(runtime.archiveBounds({id(state)}).isEmpty());
}

void CoreTest::widgetAppearance()
{
    const QVariantMap base{{"background", "#ffffff"}, {"value", "#222222"}};
    auto config = base;
    config.insert("rules", QVariantList{
        QVariantMap{{"op", "between"}, {"limit", 10}, {"max", 20}, {"value", "#ffee00"}},
        QVariantMap{{"op", "gte"}, {"limit", 10}, {"background", "#dd0000"}, {"value", "#ffffff"}}});
    const auto check = [&](const QVariant &value, const QString &quality = "good") {
        return Project::widgetColors(config, {{"value", value}, {"quality", quality}, {"hasValue", true}});
    };
    QCOMPARE(check(10).value("value").toString(), QString("#ffee00"));
    QCOMPARE(check(20).value("background").toString(), QString("#ffffff"));
    QCOMPARE(check(21).value("background").toString(), QString("#dd0000"));
    QCOMPARE(check(-5), base);
    QCOMPARE(check(21, "stale"), base);
    QCOMPARE(check(21, "error"), base);
    QCOMPARE(check(QString("21")), base);
    auto range = DemoServer::project();
    auto screens = range.value("screens").toArray();
    auto screen = screens.first().toObject();
    auto items = screen.value("items").toArray();
    auto item = items.first().toObject();
    item.insert("appearance", QJsonObject{{"background", "lavender"}});
    items.replace(0, item);
    screen.insert("items", items);
    screens.replace(0, screen);
    range.insert("screens", screens);
    QVERIFY(!Project::validate(range).isEmpty());
    config.insert("rules", QVariantList{QVariantMap{{"op", "eq"}, {"limit", 0.3}, {"value", "#ffee00"}}});
    QCOMPARE(check(0.1 + 0.2).value("value").toString(), QString("#ffee00"));
}

void CoreTest::screenSettings()
{
    Studio studio;
    studio.newProject(false);
    const auto screenId = studio.screenId();
    QCOMPARE(Project::screenSize(QJsonObject::fromVariantMap(studio.screens().first().toMap())), QSize(1200, 692));
    studio.addWidget("segments", 800, 500);
    QVERIFY(studio.setScreenProperties(screenId, {{"width", 2400}, {"height", 1384}, {"background", "#214466"},
                                           {"gridStep", 20}}, true, {}));
    QCOMPARE(studio.widget().value("x").toInt(), 1600);
    QCOMPARE(studio.widget().value("y").toInt(), 1000);
    QCOMPARE(studio.widget().value("w").toInt(), 620);
    QVERIFY(studio.saveTemplate("Панель приборов"));
    QVERIFY(!studio.setScreenProperties(screenId, {{"width", 10}}, false, {}));
    QVERIFY(studio.setScreenProperties(screenId, {{"width", 320}, {"height", 200}}, false, {}));
    QCOMPARE(studio.widget().value("x").toInt(), 0);
    QCOMPARE(studio.widget().value("w").toInt(), 320);
    studio.insertTemplate(studio.templates().first().toMap().value("id").toString());
    QCOMPARE(studio.items().size(), 2);
    QVERIFY(studio.widget().value("w").toInt() <= 320);

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto before = QUrl::fromLocalFile(temp.filePath("before.scada.json"));
    QVERIFY(studio.save(before));
    const auto png = temp.filePath("background.png");
    QFile image(png);
    QVERIFY(image.open(QIODevice::WriteOnly));
    QVERIFY(image.write(QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a7ioAAAAASUVORK5CYII=")) > 0);
    image.close();
    QVERIFY(studio.setScreenProperties(screenId, {{"backgroundFit", "contain"}}, false, QUrl::fromLocalFile(png)));
    const auto relative = studio.screens().first().toMap().value("backgroundImage").toString();
    QVERIFY(relative.startsWith("images/"));
    QVERIFY(QFile::exists(studio.imageUrl(relative).toLocalFile()));
    QVERIFY(QDir(temp.path()).mkpath("copy"));
    const auto copied = QUrl::fromLocalFile(temp.filePath("copy/after.scada.json"));
    QVERIFY(studio.save(copied));
    QVERIFY(studio.open(copied));
    const auto screen = studio.screens().first().toMap();
    QCOMPARE(screen.value("width").toInt(), 320);
    QCOMPARE(screen.value("height").toInt(), 200);
    QCOMPARE(screen.value("background").toString(), QString("#214466"));
    QCOMPARE(screen.value("gridStep").toInt(), 20);
    QCOMPARE(screen.value("backgroundFit").toString(), QString("contain"));
    QVERIFY(QFile::exists(temp.filePath("copy/" + relative)));
}

void CoreTest::faceplateStudio()
{
    Studio studio;
    studio.newProject(false);
    auto endpoint = source("Станция", 502, true);
    endpoint.remove("id");
    QVERIFY(studio.setSource(endpoint.toVariantMap()));
    endpoint = QJsonObject::fromVariantMap(studio.sources().first().toMap());
    const auto addTag = [&](const QString &name, const QString &table, int offset, bool writable) -> QString {
        auto point = tag(endpoint, name, table, "u16", offset, writable);
        point.remove("id");
        if (!studio.setTag(point.toVariantMap()))
            return {};
        return studio.tags().last().toMap().value("id").toString();
    };
    const auto reading = addTag("Обороты A", "input", 0, false);
    const auto otherReading = addTag("Обороты B", "input", 1, false);
    const auto setpoint = addTag("Уставка", "holding", 0, true);
    QVERIFY(!reading.isEmpty() && !otherReading.isEmpty() && !setpoint.isEmpty());

    studio.addWidget("value", 30, 40);
    const auto valueId = studio.selectedId();
    QVERIFY(studio.configureWidget({{"tag", reading}, {"appearance", QVariantMap{{"tag", reading}}}}));
    studio.addWidget("command", 310, 40);
    const auto commandId = studio.selectedId();
    const QVariantMap write{{"event", "click"}, {"action", "write"}, {"tag", setpoint},
                            {"valueMode", "tag"}, {"valueTag", reading}};
    QVERIFY(studio.configureWidget({{"tag", setpoint}, {"actions", QVariantList{write}}}));
    studio.selectWidget(valueId);
    studio.selectWidget(commandId, true);
    QVERIFY(studio.createFaceplate("Насос"));
    QCOMPARE(studio.items().size(), 1);
    const auto first = studio.widget();
    const auto firstId = studio.selectedId();
    const auto type = studio.faceplates().first().toMap();
    const auto typeId = type.value("id").toString();
    QCOMPARE(first.value("faceplate").toString(), typeId);
    QVERIFY(studio.renameFaceplate(typeId, "  Насосная панель  "));
    QCOMPARE(studio.faceplates().first().toMap().value("id").toString(), typeId);
    QCOMPARE(studio.faceplates().first().toMap().value("name").toString(), QString("Насосная панель"));
    QCOMPARE(studio.widget(), first);
    QCOMPARE(type.value("ports").toList().size(), 2);
    const auto bindings = first.value("bindings").toMap();
    const auto readingPort = bindings.key(reading);
    const auto outputPort = bindings.key(setpoint);
    QVERIFY(!readingPort.isEmpty() && !outputPort.isEmpty());
    const auto original = studio.resolveFaceplate(first);
    QCOMPARE(original.size(), 2);
    QCOMPARE(original.first().toMap().value("tag").toString(), reading);
    QCOMPARE(original.first().toMap().value("x").toInt(), 0);
    QCOMPARE(original.last().toMap().value("x").toInt(), 280);
    QCOMPARE(original.last().toMap().value("actions").toList().first().toMap(), write);

    QVERIFY(studio.enterComposite(firstId));
    QCOMPARE(studio.compositeDepth(), 1);
    QCOMPARE(studio.canvasSettings().value("name").toString(), QString("Насосная панель"));
    QCOMPARE(studio.editorTags().size(), 2);
    QCOMPARE(studio.items().first().toMap().value("tag").toString(), readingPort);
    studio.selectWidget(valueId);
    QVERIFY(studio.configureWidget({{"title", "Скорость"}}));
    QVERIFY(!studio.setFaceplateSize(80, 52));
    QVERIFY(studio.setFaceplateSize(540, 175));
    QCOMPARE(studio.canvasSettings().value("width").toInt(), 540);
    studio.leaveComposite();
    QCOMPARE(studio.selectedId(), firstId);
    QCOMPARE(studio.widget().value("w").toInt(), 504);

    studio.insertFaceplate(typeId);
    const auto secondId = studio.selectedId();
    QVERIFY(secondId != firstId);
    QVERIFY(studio.configureWidget({{"bindings", QVariantMap{{readingPort, otherReading}, {outputPort, setpoint}}}}));
    auto second = studio.resolveFaceplate(studio.widget());
    QCOMPARE(second.size(), 2);
    QCOMPARE(second.first().toMap().value("title").toString(), QString("Скорость"));
    QCOMPARE(second.first().toMap().value("tag").toString(), otherReading);
    QCOMPARE(second.last().toMap().value("actions").toList().first().toMap().value("valueTag").toString(), otherReading);
    QVERIFY(second.last().toMap().value("id") != original.last().toMap().value("id"));
    QVERIFY(!studio.removeFaceplate(typeId));
    QCOMPARE(studio.faceplates().size(), 1);

    QVERIFY(studio.saveTemplate("Насос без привязок"));
    studio.insertTemplate(studio.templates().first().toMap().value("id").toString());
    QCOMPARE(studio.widget().value("faceplate").toString(), typeId);
    QVERIFY(studio.widget().value("bindings").toMap().isEmpty());
    QVERIFY(studio.removeTag(otherReading));
    studio.selectWidget(secondId);
    QVERIFY(!studio.widget().value("bindings").toMap().contains(readingPort));
    QCOMPARE(studio.widget().value("bindings").toMap().value(outputPort).toString(), setpoint);
    QVERIFY(studio.enterComposite(typeId));
    auto ports = studio.faceplates().first().toMap().value("ports").toList();
    ports.removeIf([&](const auto &port) { return port.toMap().value("id").toString() == readingPort; });
    QVERIFY(studio.setFaceplatePorts(ports));
    QVERIFY(studio.items().first().toMap().value("tag").toString().isEmpty());
    const auto remainingWrite = studio.items().last().toMap().value("actions").toList().first().toMap();
    QVERIFY(remainingWrite.value("valueTag").toString().isEmpty());
    QCOMPARE(remainingWrite.value("tag").toString(), outputPort);
    studio.closeComposite();
    studio.selectWidget(firstId);
    QVERIFY(!studio.widget().value("bindings").toMap().contains(readingPort));

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto path = QUrl::fromLocalFile(temp.filePath("faceplate.scada.json"));
    QVERIFY(studio.save(path));
    QVERIFY(studio.open(path));
    QCOMPARE(studio.faceplates().first().toMap().value("id").toString(), typeId);
    QCOMPARE(studio.faceplates().first().toMap().value("name").toString(), QString("Насосная панель"));
    QCOMPARE(studio.items().size(), 3);
    QVERIFY(studio.createFaceplate("Пустой тип"));
    QCOMPARE(studio.compositeDepth(), 1);
    QVERIFY(studio.items().isEmpty());
    const auto unusedId = studio.canvasSettings().value("id").toString();
    QVERIFY(!studio.renameFaceplate(unusedId, "НАСОСНАЯ ПАНЕЛЬ"));
    QVERIFY(!studio.renameFaceplate(unusedId, " "));
    QCOMPARE(studio.canvasSettings().value("name").toString(), QString("Пустой тип"));
    studio.undo();
    QCOMPARE(studio.compositeDepth(), 0);
    QCOMPARE(studio.faceplates().size(), 1);

    const auto instances = studio.items();
    for (const auto &instance : instances) {
        studio.selectWidget(instance.toMap().value("id").toString());
        studio.removeWidget();
    }
    QVERIFY(studio.items().isEmpty());
    QVERIFY(!studio.removeFaceplate(typeId));
    studio.removeTemplate(studio.templates().first().toMap().value("id").toString());
    QVERIFY(studio.removeFaceplate(typeId));
    QVERIFY(studio.faceplates().isEmpty());
    studio.undo();
    QCOMPARE(studio.faceplates().first().toMap().value("id").toString(), typeId);
}

void CoreTest::faceplateDuplication()
{
    Studio studio;
    const auto initialTypes = studio.faceplates().size();
    const auto typeId = studio.faceplates().first().toMap().value("id").toString();
    QVERIFY(studio.enterComposite(typeId));
    const auto children = studio.items();
    const auto heading = children.at(0).toMap();
    const auto measurement = children.at(1).toMap();
    const auto control = children.at(2).toMap();
    const auto measurementPort = measurement.value("tag");
    studio.selectWidget(heading.value("id").toString());
    studio.selectWidget(measurement.value("id").toString(), true);
    studio.groupWidgets(true);
    studio.selectWidget(measurement.value("id").toString());
    QVERIFY(studio.configureWidget({{"appearance", QVariantMap{{"tag", measurementPort}, {"value", "#105571"}}}}));
    studio.selectWidget(control.value("id").toString());
    const QVariantMap action{{"event", "click"}, {"action", "write"}, {"tag", control.value("tag")},
                             {"valueMode", "tag"}, {"valueTag", control.value("feedback")}};
    QVERIFY(studio.configureWidget({{"actions", QVariantList{action}}}));
    studio.addWidget("trend", 20, 20);
    const QVariantMap series{{"tag", measurementPort}, {"color", "#008877"}, {"side", "left"}, {"style", "line"}};
    QVERIFY(studio.configureWidget({{"trend", QVariantMap{{"seconds", 120}, {"axis", "individual"},
        {"legend", true}, {"toolbar", true}, {"series", QVariantList{series}}}}}));
    studio.leaveComposite();

    const auto original = studio.faceplates().first().toMap();
    const auto screenItems = studio.items();
    const auto copyId = studio.duplicateFaceplate(typeId);
    QVERIFY(!copyId.isEmpty() && copyId != typeId);
    const auto copy = studio.faceplates().last().toMap();
    const auto originalName = original.value("name").toString();
    QCOMPARE(copy.value("name").toString(), originalName + " (копия)");
    QCOMPARE(copy.value("width"), original.value("width"));
    QCOMPARE(copy.value("height"), original.value("height"));
    QCOMPARE(studio.items(), screenItems);

    const auto originalPorts = original.value("ports").toList();
    const auto copyPorts = copy.value("ports").toList();
    QCOMPARE(copyPorts.size(), originalPorts.size());
    QHash<QString, QString> ports;
    for (qsizetype i = 0; i < originalPorts.size(); ++i) {
        auto expected = originalPorts.at(i).toMap();
        const auto port = copyPorts.at(i).toMap();
        QVERIFY(port.value("id") != expected.value("id"));
        ports.insert(expected.value("id").toString(), port.value("id").toString());
        expected["originId"] = expected.value("id");
        expected["id"] = port.value("id");
        QCOMPARE(port, expected);
    }

    const auto originalItems = original.value("items").toList();
    const auto copyItems = copy.value("items").toList();
    QCOMPARE(copyItems.size(), originalItems.size());
    QHash<QString, QString> widgets;
    for (qsizetype i = 0; i < originalItems.size(); ++i) {
        const auto item = copyItems.at(i).toMap();
        const auto source = originalItems.at(i).toMap();
        QVERIFY(item.value("id") != source.value("id"));
        widgets.insert(source.value("id").toString(), item.value("id").toString());
        for (const auto *key : {"kind", "title", "x", "y", "w", "h"})
            QCOMPARE(item.value(key), source.value(key));
    }
    const auto valueCopy = copyItems.at(1).toMap();
    QCOMPARE(valueCopy.value("tag").toString(), ports.value(measurementPort.toString()));
    const auto appearance = valueCopy.value("appearance").toMap();
    QCOMPARE(appearance.value("tag"), valueCopy.value("tag"));
    QCOMPARE(appearance.value("value").toString(), QString("#105571"));
    const auto group = valueCopy.value("group");
    QVERIFY(group.isValid() && group != originalItems.at(1).toMap().value("group"));
    QCOMPARE(copyItems.first().toMap().value("group"), group);
    const auto controlCopy = copyItems.at(2).toMap();
    QCOMPARE(controlCopy.value("feedback").toString(), ports.value(control.value("feedback").toString()));
    auto expectedAction = action;
    expectedAction["tag"] = ports.value(action.value("tag").toString());
    expectedAction["valueTag"] = ports.value(action.value("valueTag").toString());
    QCOMPARE(controlCopy.value("actions").toList().first().toMap(), expectedAction);
    auto expectedSeries = series;
    expectedSeries["tag"] = ports.value(measurementPort.toString());
    const auto trend = copyItems.last().toMap().value("trend").toMap();
    QCOMPARE(trend.value("seconds").toInt(), 120);
    QCOMPARE(trend.value("series").toList().first().toMap(), expectedSeries);

    const auto originalProperties = original.value("properties").toList();
    const auto copyProperties = copy.value("properties").toList();
    QCOMPARE(copyProperties.size(), originalProperties.size());
    for (qsizetype i = 0; i < originalProperties.size(); ++i) {
        auto expected = originalProperties.at(i).toMap();
        const auto property = copyProperties.at(i).toMap();
        QVERIFY(property.value("id") != expected.value("id"));
        expected["originId"] = expected.value("id");
        expected["id"] = property.value("id");
        expected["item"] = widgets.value(expected.value("item").toString());
        QCOMPARE(property, expected);
    }

    QVERIFY(!studio.duplicateFaceplate(typeId).isEmpty());
    QCOMPARE(studio.faceplates().last().toMap().value("name").toString(), originalName + " (копия 2)");
    studio.undo();
    QCOMPARE(studio.faceplates().size(), initialTypes + 1);
    const auto library = studio.faceplates();
    QVERIFY(studio.duplicateFaceplate(typeId, " ").isEmpty());
    QVERIFY(studio.duplicateFaceplate(typeId, originalName.toUpper()).isEmpty());
    QCOMPARE(studio.faceplates(), library);
    const auto namedCopyId = studio.duplicateFaceplate(typeId, "  Копия для стенда  ");
    QVERIFY(!namedCopyId.isEmpty() && namedCopyId != typeId && namedCopyId != copyId);
    QCOMPARE(studio.faceplates().last().toMap().value("name").toString(), QString("Копия для стенда"));
    QCOMPARE(studio.items(), screenItems);
    studio.undo();
    QCOMPARE(studio.faceplates(), library);
    QVERIFY(studio.enterComposite(copyId));
    studio.selectWidget(copyItems.first().toMap().value("id").toString());
    QVERIFY(studio.configureWidget({{"title", "Подпись в копии"}}));
    studio.leaveComposite();
    QCOMPARE(studio.faceplates().first().toMap(), original);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto path = QUrl::fromLocalFile(temp.filePath("duplicated-faceplates.scada.json"));
    QVERIFY(studio.save(path));
    QVERIFY(studio.open(path));
    QVERIFY(studio.enterComposite(copyId));
    QCOMPARE(studio.items().first().toMap().value("title").toString(), QString("Подпись в копии"));
}

void CoreTest::faceplateTransfer()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QDir root(temp.path());
    QVERIFY(root.mkpath("target"));
    Studio source;
    QVERIFY(source.save(QUrl::fromLocalFile(temp.filePath("source.scada.json"))));
    const auto sourceId = source.faceplates().first().toMap().value("id").toString();
    const auto availableScreen = source.screens().at(1).toMap();
    const auto missingScreen = source.screens().at(2).toMap();
    QVERIFY(source.enterComposite(sourceId));
    source.addWidget("link", 15, 18);
    QVERIFY(source.configureWidget({{"screen", availableScreen.value("id")}}));
    source.addWidget("link", 20, 38);
    QVERIFY(source.configureWidget({{"screen", missingScreen.value("id")}}));
    source.addWidget("image", 20, 60);
    const auto imageInput = temp.filePath("picture.png");
    QFile embeddedSource(Project::demoImage);
    QVERIFY(embeddedSource.open(QIODevice::ReadOnly));
    const auto expectedBytes = embeddedSource.readAll();
    QVERIFY(!expectedBytes.isEmpty());
    QFile picture(imageInput);
    QVERIFY(picture.open(QIODevice::WriteOnly));
    QCOMPARE(picture.write(expectedBytes), expectedBytes.size());
    picture.close();
    source.setImage(QUrl::fromLocalFile(imageInput));
    const auto sourceImage = source.widget().value("image").toString();
    QVERIFY(!sourceImage.isEmpty());
    const auto original = source.faceplates().first().toMap();
    const auto bundle = QUrl::fromLocalFile(temp.filePath("exported.faceplate.json"));
    QVERIFY(source.exportFaceplate(sourceId, QUrl::fromLocalFile(temp.filePath("exported"))));
    QVERIFY(QFile::remove(source.imageUrl(sourceImage).toLocalFile()));

    Studio target;
    target.newProject(false);
    target.setScreen({}, availableScreen.value("name").toString());
    const auto destinationScreen = target.screenId();
    QVERIFY(destinationScreen != availableScreen.value("id").toString());
    QSignalSpy notices(&target, &Studio::notice);
    const auto importedId = target.importFaceplate(bundle);
    QVERIFY2(!importedId.isEmpty(), qPrintable(notices.isEmpty() ? QString{} : notices.last().first().toString()));
    QVERIFY(importedId != sourceId);
    QVERIFY(notices.last().first().toString().contains("Переходов без подходящего экрана: 1"));
    const auto imported = target.faceplates().first().toMap();
    QCOMPARE(imported.value("name"), original.value("name"));
    QCOMPARE(imported.value("originId").toString(), sourceId);
    const auto originalTags = original.value("ports").toList();
    const auto importedTags = imported.value("ports").toList();
    QCOMPARE(importedTags.size(), originalTags.size());
    for (qsizetype i = 0; i < importedTags.size(); ++i) {
        const auto port = importedTags.at(i).toMap();
        auto expected = originalTags.at(i).toMap();
        QVERIFY(port.value("id") != expected.value("id"));
        expected["originId"] = expected.value("id");
        expected["id"] = port.value("id");
        QCOMPARE(port, expected);
    }
    const auto originalProperties = original.value("properties").toList();
    const auto importedProperties = imported.value("properties").toList();
    QCOMPARE(importedProperties.size(), originalProperties.size());
    QCOMPARE(importedProperties.at(5).toMap().value("defaultVisible"), QVariant(false));
    const auto originalItems = original.value("items").toList();
    const auto items = imported.value("items").toList();
    QCOMPARE(items.size(), originalItems.size());
    QCOMPARE(items.at(1).toMap().value("tag"), importedTags.at(0).toMap().value("id"));
    QCOMPARE(importedProperties.first().toMap().value("item"), items.first().toMap().value("id"));
    QCOMPARE(items.at(items.size() - 3).toMap().value("screen").toString(), destinationScreen);
    QVERIFY(items.at(items.size() - 2).toMap().value("screen").toString().isEmpty());
    const auto relative = items.last().toMap().value("image").toString();
    QVERIFY(!relative.isEmpty() && relative != sourceImage);
    const auto fileMatches = [&](const QString &path) {
        QFile image(path);
        return image.open(QIODevice::ReadOnly) && image.readAll() == expectedBytes;
    };
    QVERIFY(fileMatches(target.imageUrl(relative).toLocalFile()));
    QVERIFY(target.path().isEmpty());
    QVERIFY(target.tags().isEmpty());
    QVERIFY(target.sources().isEmpty());

    target.undo();
    QVERIFY(target.faceplates().isEmpty());
    const auto targetPath = QUrl::fromLocalFile(temp.filePath("target/project.scada.json"));
    QVERIFY(target.save(targetPath));
    target.redo();
    QCOMPARE(target.faceplates().first().toMap(), imported);
    QVERIFY(fileMatches(temp.filePath("target/" + relative)));
    QVERIFY(target.save());
    QVERIFY(target.open(targetPath));
    QVERIFY(fileMatches(target.imageUrl(relative).toLocalFile()));
    const auto second = target.importFaceplate(bundle);
    QVERIFY(!second.isEmpty() && second != importedId);
    QCOMPARE(target.faceplates().last().toMap().value("name").toString(),
             original.value("name").toString() + " (копия)");
    QVERIFY(fileMatches(target.imageUrl(target.faceplates().last().toMap().value("items")
        .toList().last().toMap().value("image").toString()).toLocalFile()));

    QFile package(bundle.toLocalFile());
    QVERIFY(package.open(QIODevice::ReadOnly));
    auto incomplete = QJsonDocument::fromJson(package.readAll()).object();
    incomplete.insert("images", QJsonObject{});
    QFile malformed(temp.filePath("incomplete.faceplate.json"));
    QVERIFY(malformed.open(QIODevice::WriteOnly));
    QVERIFY(malformed.write(QJsonDocument(incomplete).toJson()) > 0);
    malformed.close();
    const auto beforeRejected = target.faceplates();
    QVERIFY(target.importFaceplate(QUrl::fromLocalFile(malformed.fileName())).isEmpty());
    QCOMPARE(target.faceplates(), beforeRejected);
}

void CoreTest::faceplateImportReplacement()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto bundle = QUrl::fromLocalFile(temp.filePath("replacement.faceplate.json"));
    const auto originalBundle = QUrl::fromLocalFile(temp.filePath("original.faceplate.json"));
    Studio target;
    const auto beforeType = target.faceplates().first().toMap();
    const auto initialTypes = target.faceplates().size();
    const auto targetId = beforeType.value("id").toString();
    const auto panelScreen = target.screens().at(4).toMap();
    target.selectScreen(panelScreen.value("id").toString());
    const auto pump = target.items().at(3).toMap();
    target.selectWidget(pump.value("id").toString());
    QVERIFY(target.saveTemplate("Переносимая панель"));
    target.setScreen({}, "Другой экран");
    target.insertFaceplate(targetId);
    QVERIFY(target.configureWidget({{"bindings", pump.value("bindings")}, {"overrides", pump.value("overrides")},
                                     {"propertyBindings", pump.value("propertyBindings")}}));
    const auto beforeScreens = target.screens();
    const auto beforeTemplates = target.templates();
    QVERIFY(target.exportFaceplate(targetId, bundle));
    QVERIFY(QFile::copy(bundle.toLocalFile(), originalBundle.toLocalFile()));

    Studio supplier;
    supplier.newProject(false);
    const auto newId = supplier.importFaceplate(bundle, "  Обновление панели  ");
    QVERIFY(!newId.isEmpty());
    QVERIFY(supplier.importFaceplate(bundle, "обновление панели").isEmpty());
    QVERIFY(supplier.enterComposite(newId));
    auto revised = supplier.faceplates().first().toMap();
    const auto renameFirst = [](QVariantList entries, const QString &name) {
        auto field = entries.first().toMap();
        field["name"] = name;
        entries[0] = field;
        return entries;
    };
    QVERIFY(supplier.setFaceplatePorts(renameFirst(revised.value("ports").toList(), "Показание на стенде")));
    QVERIFY(supplier.setFaceplateProperties(renameFirst(revised.value("properties").toList(), "Подпись стенда")));
    supplier.addWidget("label", 20, 20);
    QVERIFY(supplier.configureWidget({{"title", "Из новой версии"}}));
    QVERIFY(supplier.exportFaceplate(newId, bundle));
    const auto options = target.faceplateImportOptions(bundle);
    QCOMPARE(options.value("name").toString(), QString("Обновление панели"));
    QCOMPARE(options.value("suggestedName"), options.value("name"));

    QSignalSpy changes(&target, &Studio::projectChanged);
    QCOMPARE(target.importFaceplate(bundle, {}, targetId), targetId);
    QCOMPARE(changes.count(), 1);
    QCOMPARE(target.faceplates().size(), initialTypes);
    revised = target.faceplates().first().toMap();
    QCOMPARE(revised.value("name"), beforeType.value("name"));
    QCOMPARE(revised.value("items").toList().last().toMap().value("title").toString(), QString("Из новой версии"));
    const auto mapFields = [&](QVariantList layouts) {
        for (auto &entry : layouts) {
            auto layout = entry.toMap();
            auto items = layout.value("items").toList();
            for (auto &record : items) {
                auto item = record.toMap();
                if (item.value("faceplate") != targetId)
                    continue;
                for (const auto *field : {"bindings", "overrides", "propertyBindings"}) {
                    if (!item.contains(field))
                        continue;
                    const auto oldValues = item.value(field).toMap();
                    const auto definitions = revised.value(QLatin1StringView(field) == "bindings" ?
                        "ports" : "properties").toList();
                    QVariantMap mapped;
                    for (const auto &definition : definitions) {
                        const auto row = definition.toMap();
                        const auto oldId = row.value("originId").toString();
                        if (oldValues.contains(oldId))
                            mapped.insert(row.value("id").toString(), oldValues.value(oldId));
                    }
                    item[field] = mapped;
                }
                record = item;
            }
            layout["items"] = items;
            entry = layout;
        }
        return layouts;
    };
    QCOMPARE(target.screens(), mapFields(beforeScreens));
    QCOMPARE(target.templates(), mapFields(beforeTemplates));
    target.undo();
    QCOMPARE(target.faceplates().first().toMap(), beforeType);
    QCOMPARE(target.screens(), beforeScreens);
    QCOMPARE(target.templates(), beforeTemplates);
    target.redo();
    QCOMPARE(target.faceplates().first().toMap(), revised);
    target.undo();

    auto incomplete = supplier.faceplates().first().toMap().value("ports").toList();
    incomplete.removeFirst();
    QVERIFY(supplier.setFaceplatePorts(incomplete));
    QVERIFY(supplier.exportFaceplate(newId, bundle));
    changes.clear();
    QVERIFY(target.importFaceplate(bundle, {}, targetId).isEmpty());
    QCOMPARE(changes.count(), 0);
    QCOMPARE(target.faceplates().first().toMap(), beforeType);
    QCOMPARE(target.screens(), beforeScreens);
    QCOMPARE(target.templates(), beforeTemplates);

    Studio unrelated;
    QVERIFY(unrelated.exportFaceplate(unrelated.faceplates().first().toMap().value("id").toString(), bundle));
    QCOMPARE(target.importFaceplate(bundle, {}, targetId), targetId);
    QCOMPARE(target.faceplates().size(), initialTypes);
    const auto sourcePort = beforeType.value("ports").toList().first().toMap().value("id").toString();
    QCOMPARE(target.resolveFaceplate(target.widget()).at(1).toMap().value("tag"),
             pump.value("bindings").toMap().value(sourcePort));
    target.undo();
    const auto unrelatedType = unrelated.faceplates().first().toMap();
    const auto unrelatedId = unrelatedType.value("id").toString();
    const auto newPorts = unrelatedType.value("ports").toList();
    const auto newProperties = unrelatedType.value("properties").toList();
    QVERIFY(unrelated.enterComposite(unrelatedId));
    QVERIFY(unrelated.setFaceplatePorts(renameFirst(newPorts, "Показание из файла")));
    QVERIFY(unrelated.setFaceplateProperties(renameFirst(newProperties, "Заголовок из файла")));
    QVERIFY(unrelated.exportFaceplate(unrelatedId, bundle));
    const auto settings = target.faceplateImportOptions(bundle);
    const auto rows = target.faceplateReplacementFields(targetId, settings.value("definition").toMap());
    const auto sourceProperty = beforeType.value("properties").toList().first().toMap().value("id").toString();
    const auto destinationPort = newPorts.first().toMap().value("id").toString();
    const auto destinationProperty = newProperties.first().toMap().value("id").toString();
    for (const auto &id : {sourcePort, sourceProperty}) {
        const auto row = std::find_if(rows.cbegin(), rows.cend(), [&id](const QVariant &entry) {
            return entry.toMap().value("id").toString() == id;
        });
        QVERIFY(row != rows.cend());
        QVERIFY(row->toMap().value("automatic").toString().isEmpty());
        QCOMPARE(row->toMap().value("choices").toList().size(), 1);
    }
    QVERIFY(target.importFaceplate(bundle, {}, targetId).isEmpty());
    const auto commandPort = newPorts.at(1).toMap().value("id").toString();
    const auto oldFeedback = beforeType.value("ports").toList().at(2).toMap().value("id").toString();
    const QVariantMap manual{{sourcePort, destinationPort}, {sourceProperty, destinationProperty}};
    auto invalid = manual;
    invalid[sourcePort] = commandPort;
    QVERIFY(target.importFaceplate(bundle, {}, targetId, invalid).isEmpty());
    invalid = manual;
    invalid[oldFeedback] = commandPort;
    QVERIFY(target.importFaceplate(bundle, {}, targetId, invalid).isEmpty());
    QCOMPARE(target.screens(), beforeScreens);
    QCOMPARE(target.templates(), beforeTemplates);
    QCOMPARE(target.importFaceplate(bundle, {}, targetId, manual), targetId);
    const auto resolved = target.resolveFaceplate(target.widget());
    QCOMPARE(resolved.at(1).toMap().value("tag"), pump.value("bindings").toMap().value(sourcePort));
    QCOMPARE(resolved.first().toMap().value("title"), pump.value("overrides").toMap().value(sourceProperty));
    QCOMPARE(target.templates().first().toMap().value("items").toList().first().toMap()
        .value("propertyBindings").toMap().size(), 2);

    const auto projectPath = QUrl::fromLocalFile(temp.filePath("remembered-import.scada.json"));
    const auto currentScreen = target.screenId();
    const auto currentInstance = target.selectedId();
    QVERIFY(target.save(projectPath));

    Studio reopened;
    QVERIFY(reopened.open(projectPath));
    reopened.selectScreen(currentScreen);
    reopened.selectWidget(currentInstance);
    const auto suggestionFor = [&](const QUrl &file, const QString &fieldId) {
        const auto options = reopened.faceplateImportOptions(file);
        const auto fields = reopened.faceplateReplacementFields(targetId, options.value("definition").toMap());
        for (const auto &entry : fields) {
            const auto row = entry.toMap();
            if (row.value("id").toString() == fieldId)
                return row.value("automatic").toString();
        }
        return QString{};
    };
    const auto firstField = [&reopened](const char *group) {
        return reopened.faceplates().first().toMap().value(group).toList().first().toMap().value("id").toString();
    };
    QCOMPARE(suggestionFor(originalBundle, firstField("ports")), sourcePort);
    QCOMPARE(suggestionFor(originalBundle, firstField("properties")), sourceProperty);
    QVERIFY(reopened.importFaceplate(originalBundle, {}, targetId) == targetId);
    QCOMPARE(reopened.resolveFaceplate(reopened.widget()).at(1).toMap().value("tag"),
             pump.value("bindings").toMap().value(sourcePort));

    const auto updatedSupplier = unrelated.duplicateFaceplate(unrelatedId);
    QVERIFY(!updatedSupplier.isEmpty());
    const auto updatedType = unrelated.faceplates().last().toMap();
    const auto updatedPort = updatedType.value("ports").toList().first().toMap().value("id").toString();
    const auto updatedProperty = updatedType.value("properties").toList().first().toMap().value("id").toString();
    QVERIFY(updatedPort != destinationPort && updatedProperty != destinationProperty);
    QVERIFY(unrelated.exportFaceplate(updatedSupplier, bundle));
    QCOMPARE(suggestionFor(bundle, firstField("ports")), updatedPort);
    QCOMPARE(suggestionFor(bundle, firstField("properties")), updatedProperty);
    QCOMPARE(reopened.importFaceplate(bundle, {}, targetId), targetId);
    const auto roundTrip = reopened.resolveFaceplate(reopened.widget());
    QCOMPARE(roundTrip.at(1).toMap().value("tag"), resolved.at(1).toMap().value("tag"));
    QCOMPARE(roundTrip.first().toMap().value("title"), resolved.first().toMap().value("title"));
    reopened.undo();

    QVERIFY(unrelated.enterComposite(updatedSupplier));
    auto remainingPorts = updatedType.value("ports").toList();
    remainingPorts.removeFirst();
    QVERIFY(unrelated.setFaceplatePorts(remainingPorts));
    QVERIFY(unrelated.exportFaceplate(updatedSupplier, bundle));
    QVERIFY(suggestionFor(bundle, firstField("ports")).isEmpty());
    QVERIFY(reopened.importFaceplate(bundle, {}, targetId).isEmpty());

    const auto localPorts = reopened.faceplates().first().toMap().value("ports").toList();
    const auto originalCommand = beforeType.value("ports").toList().at(1).toMap().value("id");
    const QVariantMap swap{{localPorts.at(1).toMap().value("id").toString(), oldFeedback},
                          {localPorts.at(2).toMap().value("id").toString(), originalCommand}};
    const auto beforeSwap = reopened.resolveFaceplate(reopened.widget()).at(2).toMap();
    QCOMPARE(reopened.importFaceplate(originalBundle, {}, targetId, swap), targetId);
    const auto swapped = reopened.resolveFaceplate(reopened.widget()).at(2).toMap();
    QCOMPARE(swapped.value("tag"), beforeSwap.value("feedback"));
    QCOMPARE(swapped.value("feedback"), beforeSwap.value("tag"));
    QCOMPARE(reopened.importFaceplate(originalBundle, {}, targetId), targetId);
    const auto repeated = reopened.resolveFaceplate(reopened.widget()).at(2).toMap();
    QCOMPARE(repeated.value("tag"), swapped.value("tag"));
    QCOMPARE(repeated.value("feedback"), swapped.value("feedback"));
}

void CoreTest::faceplateTypeChange()
{
    Studio studio;
    const auto source = studio.faceplates().first().toMap();
    const auto sourceId = source.value("id").toString();
    const auto copyId = studio.duplicateFaceplate(sourceId);
    QVERIFY(!copyId.isEmpty());
    QVERIFY(studio.enterComposite(copyId));
    const auto renameFirst = [](QVariantList definitions) {
        auto first = definitions.first().toMap();
        first["name"] = "Переименованное поле";
        first.remove("originId");
        definitions[0] = first;
        return definitions;
    };
    auto target = studio.faceplates().last().toMap();
    QVERIFY(studio.setFaceplatePorts(renameFirst(target.value("ports").toList())));
    QVERIFY(studio.setFaceplateProperties(renameFirst(target.value("properties").toList())));
    studio.leaveComposite();
    target = studio.faceplates().last().toMap();

    const auto panels = studio.screens().at(4).toMap().value("id").toString();
    studio.selectScreen(panels);
    const auto original = studio.items().at(3).toMap();
    const auto instanceId = original.value("id").toString();
    studio.selectWidget(instanceId);
    QCOMPARE(original.value("faceplate").toString(), sourceId);
    const auto beforeSwitch = studio.resolveFaceplate(original);

    const auto libraryBeforeCopy = studio.faceplates();
    const auto layoutBeforeCopy = studio.items();
    QSignalSpy changes(&studio, &Studio::projectChanged);
    QCOMPARE(studio.suggestFaceplateCopyName(sourceId), source.value("name").toString() + " (копия 2)");
    QVERIFY(!studio.duplicateFaceplateForInstance(instanceId, " "));
    QVERIFY(!studio.duplicateFaceplateForInstance(instanceId, source.value("name").toString().toUpper()));
    QCOMPARE(changes.count(), 0);
    QCOMPARE(studio.faceplates(), libraryBeforeCopy);
    QCOMPARE(studio.items(), layoutBeforeCopy);
    QVERIFY(studio.duplicateFaceplateForInstance(instanceId, "  Панель насоса для стенда  "));
    QCOMPARE(changes.count(), 1);
    const auto copyForInstance = studio.faceplates().last().toMap();
    QCOMPARE(studio.faceplates().size(), libraryBeforeCopy.size() + 1);
    QCOMPARE(copyForInstance.value("originId").toString(), sourceId);
    QCOMPARE(studio.compositeDepth(), 1);
    QCOMPARE(studio.canvasSettings().value("id"), copyForInstance.value("id"));
    QCOMPARE(studio.canvasSettings().value("name").toString(), QString("Панель насоса для стенда"));
    studio.leaveComposite();
    QCOMPARE(studio.widget().value("faceplate"), copyForInstance.value("id"));
    QCOMPARE(studio.selectedId(), instanceId);
    QCOMPARE(studio.items().at(4), layoutBeforeCopy.at(4));
    const auto switchedInstance = studio.widget();
    const auto resolvedCopy = studio.resolveFaceplate(switchedInstance);
    QCOMPARE(resolvedCopy.size(), beforeSwitch.size());
    for (qsizetype i = 0; i < beforeSwitch.size(); ++i) {
        auto child = beforeSwitch.at(i).toMap();
        child["id"] = resolvedCopy.at(i).toMap().value("id");
        QCOMPARE(resolvedCopy.at(i).toMap(), child);
    }
    QVERIFY(studio.enterComposite(instanceId));
    studio.undo();
    QCOMPARE(studio.compositeDepth(), 0);
    QCOMPARE(studio.faceplates(), libraryBeforeCopy);
    QCOMPARE(studio.items(), layoutBeforeCopy);
    studio.selectWidget(instanceId);
    studio.redo();
    QCOMPARE(studio.faceplates().last().toMap(), copyForInstance);
    QCOMPARE(studio.widget(), switchedInstance);
    studio.undo();

    const auto preview = studio.previewFaceplateTypeChange(instanceId, copyId);
    QVERIFY2(preview.value("allowed").toBool(), qPrintable(preview.value("error").toString()));
    QVERIFY(preview.value("related").toBool());
    QCOMPARE(preview.value("tags").toInt(), 3);
    QCOMPARE(preview.value("properties").toInt(), 5);
    QVERIFY(studio.changeFaceplateType(instanceId, copyId));
    QCOMPARE(studio.selectedId(), instanceId);

    auto expected = original;
    expected["faceplate"] = copyId;
    for (const auto *field : {"bindings", "overrides", "propertyBindings"}) {
        const bool tags = QLatin1StringView(field) == "bindings";
        const auto definitions = target.value(tags ? "ports" : "properties").toList();
        const auto previous = original.value(field).toMap();
        QVariantMap mapped;
        for (const auto &entry : definitions) {
            const auto definition = entry.toMap();
            const auto oldId = definition.value("originId").toString();
            if (previous.contains(oldId))
                mapped.insert(definition.value("id").toString(), previous.value(oldId));
        }
        QCOMPARE(mapped.size(), previous.size());
        expected[field] = mapped;
    }
    QCOMPARE(studio.widget(), expected);
    const auto afterSwitch = studio.resolveFaceplate(studio.widget());
    QCOMPARE(afterSwitch.size(), beforeSwitch.size());
    for (qsizetype i = 0; i < beforeSwitch.size(); ++i) {
        auto child = beforeSwitch.at(i).toMap();
        child["id"] = afterSwitch.at(i).toMap().value("id");
        QCOMPARE(afterSwitch.at(i).toMap(), child);
    }
    studio.undo();
    QCOMPARE(studio.widget(), original);
    studio.redo();
    QCOMPARE(studio.widget(), expected);

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto path = QUrl::fromLocalFile(temp.filePath("switched.scada.json"));
    QVERIFY(studio.save(path));
    QVERIFY(studio.open(path));
    studio.selectScreen(panels);
    studio.selectWidget(instanceId);
    QCOMPARE(studio.widget(), expected);
    QVERIFY(studio.changeFaceplateType(instanceId, sourceId));
    QCOMPARE(studio.widget(), original);

    auto ports = target.value("ports").toList();
    auto replacement = ports.takeFirst().toMap();
    replacement.remove("id");
    replacement.remove("originId");
    replacement["name"] = source.value("ports").toList().first().toMap().value("name");
    ports.append(replacement);
    QVERIFY(studio.enterComposite(copyId));
    QVERIFY(studio.setFaceplatePorts(ports));
    studio.leaveComposite();
    const auto conflict = studio.previewFaceplateTypeChange(instanceId, copyId);
    QVERIFY(!conflict.value("allowed").toBool());
    QVERIFY(conflict.value("error").toString().contains(replacement.value("name").toString()));
    QVERIFY(!studio.changeFaceplateType(instanceId, copyId));
    studio.selectWidget(instanceId);
    QCOMPARE(studio.widget(), original);

    const auto legacyId = studio.duplicateFaceplate(sourceId);
    QVERIFY(!legacyId.isEmpty() && studio.save(path));
    QJsonObject legacyProject;
    QString error;
    QVERIFY(Project::load(path.toLocalFile(), legacyProject, error));
    auto types = legacyProject.value("faceplates").toArray();
    auto legacy = types.last().toObject();
    legacy.remove("originId");
    for (const auto *key : {"ports", "properties"}) {
        auto definitions = legacy.value(key).toArray();
        for (qsizetype i = 0; i < definitions.size(); ++i) {
            auto definition = definitions.at(i).toObject();
            definition.remove("originId");
            definitions.replace(i, definition);
        }
        legacy.insert(key, definitions);
    }
    types.replace(types.size() - 1, legacy);
    legacyProject.insert("faceplates", types);
    QVERIFY(Project::save(path.toLocalFile(), legacyProject, error));
    QVERIFY(studio.open(path));
    studio.selectScreen(panels);
    const auto legacyPreview = studio.previewFaceplateTypeChange(instanceId, legacyId);
    QVERIFY(legacyPreview.value("allowed").toBool());
    QVERIFY(!legacyPreview.value("related").toBool());
    QCOMPARE(legacyPreview.value("tags").toInt(), 3);
    QCOMPARE(legacyPreview.value("properties").toInt(), 5);
    QVERIFY(studio.changeFaceplateType(instanceId, legacyId));
    studio.undo();
    const auto sibling = studio.items().at(4);
    QVERIFY(studio.enterComposite(legacyId));
    const auto legacyLayout = studio.faceplates().last().toMap();
    const auto renamedPorts = renameFirst(legacyLayout.value("ports").toList());
    auto renamedProperties = renameFirst(legacyLayout.value("properties").toList());
    renamedProperties.removeAt(2); // Only the valve instance has a custom value color.
    QVERIFY(studio.setFaceplatePorts(renamedPorts));
    QVERIFY(studio.setFaceplateProperties(renamedProperties));
    studio.leaveComposite();
    studio.selectWidget(instanceId);

    const auto manualPreview = studio.previewFaceplateTypeChange(instanceId, legacyId);
    QVERIFY(!manualPreview.value("allowed").toBool());
    QCOMPARE(manualPreview.value("fields").toList().size(), 8);
    const auto oldPort = source.value("ports").toList().first().toMap().value("id").toString();
    const auto oldCaption = source.value("properties").toList().first().toMap().value("id").toString();
    const QVariantMap manual{{oldPort, renamedPorts.first().toMap().value("id")},
                             {oldCaption, renamedProperties.first().toMap().value("id")}};
    auto invalid = manual;
    invalid[oldPort] = renamedPorts.at(1).toMap().value("id");
    QVERIFY(!studio.changeFaceplateType(instanceId, legacyId, invalid));
    QCOMPARE(studio.widget(), original);
    QVERIFY(studio.changeFaceplateType(instanceId, legacyId, manual));
    QCOMPARE(studio.selectedId(), instanceId);
    QCOMPARE(studio.items().at(4), sibling);
    QCOMPARE(studio.widget().value("bindings").toMap().size(), 3);
    const auto resolved = studio.resolveFaceplate(studio.widget());
    QCOMPARE(resolved.size(), beforeSwitch.size());
    for (qsizetype i = 0; i < resolved.size(); ++i) {
        auto child = beforeSwitch.at(i).toMap();
        child["id"] = resolved.at(i).toMap().value("id");
        QCOMPARE(resolved.at(i).toMap(), child);
    }
    QVERIFY(studio.save(path));
    studio.undo();
    QCOMPARE(studio.widget(), original);

    Studio reopened;
    QVERIFY(reopened.open(path));
    reopened.selectScreen(panels);
    reopened.selectWidget(instanceId);
    const auto suggestion = [](const QVariantMap &preview, const QString &id) {
        const auto fields = preview.value("fields").toList();
        for (const auto &entry : fields) {
            const auto field = entry.toMap();
            if (field.value("id").toString() == id)
                return field.value("automatic");
        }
        return QVariant{};
    };
    const auto back = reopened.previewFaceplateTypeChange(instanceId, sourceId);
    QVERIFY(back.value("allowed").toBool());
    QCOMPARE(suggestion(back, manual.value(oldPort).toString()).toString(), oldPort);
    QCOMPARE(suggestion(back, manual.value(oldCaption).toString()).toString(), oldCaption);
    QVERIFY(reopened.changeFaceplateType(instanceId, sourceId));
    QCOMPARE(reopened.widget(), original);
    const auto remembered = reopened.previewFaceplateTypeChange(instanceId, legacyId);
    QVERIFY(remembered.value("allowed").toBool());
    QCOMPARE(suggestion(remembered, oldPort), manual.value(oldPort));
    QCOMPARE(suggestion(remembered, oldCaption), manual.value(oldCaption));
    QVERIFY(reopened.changeFaceplateType(instanceId, legacyId));
    QCOMPARE(reopened.resolveFaceplate(reopened.widget()), resolved);
    reopened.undo();

    QVERIFY(reopened.enterComposite(legacyId));
    auto remainingPorts = renamedPorts;
    remainingPorts.removeFirst();
    QVERIFY(reopened.setFaceplatePorts(remainingPorts));
    reopened.leaveComposite();
    const auto stale = reopened.previewFaceplateTypeChange(instanceId, legacyId);
    QVERIFY(!stale.value("allowed").toBool());
    QVERIFY(suggestion(stale, oldPort).toString().isEmpty());
    QVERIFY(reopened.removeFaceplate(legacyId));
    QVERIFY(reopened.save());
    QJsonObject afterRemoval;
    QVERIFY(Project::load(path.toLocalFile(), afterRemoval, error));
    QVERIFY(!afterRemoval.contains("faceplateMappings"));
}

void CoreTest::faceplateInstanceProperties()
{
    Studio studio;
    studio.newProject(false);
    studio.addWidget("label", 30, 40);
    const auto labelId = studio.selectedId();
    const QVariantList rules{QVariantMap{{"op", "gte"}, {"limit", 20}, {"background", "#df4440"}}};
    QVERIFY(studio.configureWidget({{"title", "Исходная подпись"}, {"appearance", QVariantMap{
        {"background", "#112233"}, {"rules", rules}}}}));
    studio.addWidget("value", 310, 40);
    const auto readingId = studio.selectedId();
    QVERIFY(studio.configureWidget({{"appearance", QVariantMap{{"value", "#303030"}}}}));
    studio.selectWidget(labelId);
    studio.selectWidget(readingId, true);
    QVERIFY(studio.createFaceplate("Насосная панель"));
    const auto firstId = studio.selectedId();
    const auto typeId = studio.widget().value("faceplate").toString();
    QVERIFY(studio.enterComposite(firstId));
    QVERIFY(studio.setFaceplateProperties({
        QVariantMap{{"name", "Заголовок"}, {"item", labelId}, {"kind", "title"}},
        QVariantMap{{"name", "Фон"}, {"item", labelId}, {"kind", "background"}},
        QVariantMap{{"name", "Цвет заголовка"}, {"item", labelId}, {"kind", "captionColor"}},
        QVariantMap{{"name", "Показывать заголовок"}, {"item", labelId}, {"kind", "visible"}},
        QVariantMap{{"name", "Цвет показания"}, {"item", readingId}, {"kind", "value"}},
        QVariantMap{{"name", "Показывать показание"}, {"item", readingId}, {"kind", "visible"}, {"defaultVisible", false}}
    }));
    auto properties = studio.faceplates().first().toMap().value("properties").toList();
    const auto propertyId = [&properties](int index) { return properties.at(index).toMap().value("id").toString(); };
    const auto titleId = propertyId(0);
    const auto backgroundId = propertyId(1);
    const auto captionId = propertyId(2);
    const auto showLabelId = propertyId(3);
    const auto valueId = propertyId(4);
    const auto showReadingId = propertyId(5);
    studio.leaveComposite();
    QCOMPARE(studio.selectedId(), firstId);
    const auto defaults = studio.resolveFaceplate(studio.widget());
    QCOMPARE(defaults.first().toMap().value("title").toString(), QString("Исходная подпись"));
    QCOMPARE(defaults.first().toMap().value("appearance").toMap().value("background").toString(), QString("#112233"));
    QCOMPARE(defaults.first().toMap().value("visible").toBool(), true);
    QCOMPARE(defaults.last().toMap().value("visible").toBool(), false);

    const QVariantMap overrides{{titleId, QStringLiteral("")}, {backgroundId, "#225588"}, {captionId, "#cc9911"},
                                {showLabelId, false}, {valueId, "#667788"}, {showReadingId, true}};
    QVERIFY(studio.configureWidget({{"overrides", overrides}}));
    auto first = studio.resolveFaceplate(studio.widget());
    QCOMPARE(first.first().toMap().value("title").toString(), QString{});
    QCOMPARE(first.first().toMap().value("captionColor").toString(), QString("#cc9911"));
    QCOMPARE(first.first().toMap().value("visible").toBool(), false);
    const auto appearance = first.first().toMap().value("appearance").toMap();
    QCOMPARE(appearance.value("background").toString(), QString("#225588"));
    QCOMPARE(appearance.value("rules").toList(), rules);
    QCOMPARE(studio.widgetColors(appearance, {{"value", 21}, {"quality", "good"}, {"hasValue", true}})
                 .value("background").toString(), QString("#df4440"));
    QCOMPARE(first.last().toMap().value("visible").toBool(), true);
    QCOMPARE(first.last().toMap().value("appearance").toMap().value("value").toString(), QString("#667788"));
    QVERIFY(!studio.configureWidget({{"overrides", QVariantMap{{showLabelId, "false"}}}}));
    QCOMPARE(studio.widget().value("overrides").toMap(), overrides);
    QVERIFY(studio.saveTemplate("Вариант панели"));
    const auto templateItem = [&studio] {
        return studio.templates().first().toMap().value("items").toList().first().toMap();
    };
    QCOMPARE(templateItem().value("overrides").toMap(), overrides);

    studio.insertFaceplate(typeId);
    const auto secondId = studio.selectedId();
    QVERIFY(secondId != firstId);
    auto second = studio.resolveFaceplate(studio.widget());
    QCOMPARE(second.first().toMap().value("title").toString(), QString("Исходная подпись"));
    QCOMPARE(second.first().toMap().value("visible").toBool(), true);
    QCOMPARE(second.last().toMap().value("visible").toBool(), false);
    QVERIFY(studio.configureWidget({{"overrides", QVariantMap{{titleId, "Насос №2"}}}}));
    second = studio.resolveFaceplate(studio.widget());
    QCOMPARE(second.first().toMap().value("title").toString(), QString("Насос №2"));

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto path = QUrl::fromLocalFile(temp.filePath("faceplate-properties.scada.json"));
    QVERIFY(studio.save(path));
    QVERIFY(studio.open(path));
    studio.selectWidget(firstId);
    QCOMPARE(studio.widget().value("overrides").toMap(), overrides);
    QVERIFY(studio.enterComposite(firstId));
    auto changed = properties.at(2).toMap();
    changed.insert("kind", "value");
    properties.replace(2, changed);
    QVERIFY(studio.setFaceplateProperties(properties));
    studio.leaveComposite();
    QVERIFY(!studio.widget().value("overrides").toMap().contains(captionId));
    QCOMPARE(studio.widget().value("overrides").toMap().value(showLabelId).toBool(), false);
    QVERIFY(!templateItem().value("overrides").toMap().contains(captionId));

    QVERIFY(studio.enterComposite(firstId));
    studio.selectWidget(labelId);
    studio.removeWidget();
    QCOMPARE(studio.faceplates().first().toMap().value("properties").toList().size(), 2);
    studio.leaveComposite();
    const QVariantMap retained{{valueId, "#667788"}, {showReadingId, true}};
    QCOMPARE(studio.widget().value("overrides").toMap(), retained);
    QCOMPARE(templateItem().value("overrides").toMap(), retained);
    studio.selectWidget(secondId);
    QVERIFY(!studio.widget().contains("overrides"));
}

void CoreTest::faceplatePropertyBindings()
{
    Studio studio;
    studio.newProject(false);
    auto endpoint = source("Станция", 502);
    endpoint.remove("id");
    QVERIFY(studio.setSource(endpoint.toVariantMap()));
    endpoint = QJsonObject::fromVariantMap(studio.sources().first().toMap());
    auto pressure = tag(endpoint, "Давление", "input", "u16", 0);
    pressure.remove("id");
    pressure.insert("alarm", QJsonObject{{"direction", "high"}, {"limit", 10}, {"hysteresis", 1}});
    QVERIFY(studio.setTag(pressure.toVariantMap()));
    const auto pressureId = studio.tags().last().toMap().value("id").toString();
    auto temperature = tag(endpoint, "Температура", "input", "u16", 1);
    temperature.remove("id");
    QVERIFY(studio.setTag(temperature.toVariantMap()));
    const auto temperatureId = studio.tags().last().toMap().value("id").toString();

    studio.addWidget("signal", 30, 40);
    const auto lampId = studio.selectedId();
    QVERIFY(studio.configureWidget({{"appearance", QVariantMap{{"value", "#556677"}}}}));
    QVERIFY(studio.createFaceplate("Панель сигнализации"));
    const auto instanceId = studio.selectedId();
    QVERIFY(studio.enterComposite(instanceId));
    QVERIFY(studio.setFaceplateProperties({
        QVariantMap{{"name", "Показывать лампу"}, {"item", lampId}, {"kind", "visible"}, {"defaultVisible", false}},
        QVariantMap{{"name", "Цвет лампы"}, {"item", lampId}, {"kind", "value"}}
    }));
    auto properties = studio.faceplates().first().toMap().value("properties").toList();
    const auto visibleId = properties.at(0).toMap().value("id").toString();
    const auto colorId = properties.at(1).toMap().value("id").toString();
    studio.leaveComposite();

    const QVariantMap visibility{{"tag", pressureId}, {"op", "alarm"}, {"value", true}, {"otherwise", false}};
    const QVariantMap color{{"tag", temperatureId}, {"op", "between"}, {"limit", 20}, {"max", 40},
                            {"value", "#bb2222"}, {"otherwise", "#33aa66"}};
    const QVariantMap bindings{{visibleId, visibility}, {colorId, color}};
    QVERIFY(studio.configureWidget({{"propertyBindings", bindings}}));
    const auto child = studio.resolveFaceplate(studio.widget()).first().toMap();
    QCOMPARE(child.value("visible"), QVariant(false));
    QCOMPARE(child.value("appearance").toMap().value("value").toString(), QString("#556677"));
    const auto childBindings = child.value("propertyBindings").toMap();
    QCOMPARE(childBindings, (QVariantMap{{"visible", visibility}, {"value", color}}));

    const auto readings = [&temperatureId](int value, const QString &quality) {
        return QVariantMap{{temperatureId, QVariantMap{{"value", value}, {"hasValue", true}, {"quality", quality}}}};
    };
    const QVariantList alarms{QVariantMap{{"tag", pressureId}, {"ack", true}}};
    const auto active = studio.faceplatePropertyValues(childBindings, readings(20, "good"), alarms);
    QCOMPARE(active.value("visible"), QVariant(true));
    QCOMPARE(active.value("value").toString(), QString("#bb2222"));
    const auto inactive = studio.faceplatePropertyValues(childBindings, readings(41, "good"), {});
    QCOMPARE(inactive.value("visible"), QVariant(false));
    QCOMPARE(inactive.value("value").toString(), QString("#33aa66"));
    const auto stale = studio.faceplatePropertyValues(childBindings, readings(30, "stale"), alarms);
    QCOMPARE(stale, (QVariantMap{{"visible", true}}));

    auto invalidColor = color;
    invalidColor.insert("tag", Project::id());
    QVERIFY(!studio.configureWidget({{"propertyBindings", QVariantMap{{colorId, invalidColor}}}}));
    QCOMPARE(studio.widget().value("propertyBindings").toMap(), bindings);

    studio.duplicateWidget();
    const auto copyId = studio.selectedId();
    QVERIFY(copyId != instanceId);
    QCOMPARE(studio.widget().value("propertyBindings").toMap(), bindings);
    auto changedColor = color;
    changedColor.insert("tag", pressureId);
    auto copyBindings = bindings;
    copyBindings.insert(colorId, changedColor);
    QVERIFY(studio.configureWidget({{"propertyBindings", copyBindings}}));
    studio.selectWidget(instanceId);
    QCOMPARE(studio.widget().value("propertyBindings").toMap(), bindings);

    QVERIFY(studio.saveTemplate("Переносимая панель"));
    const auto templateBindings = [&studio] {
        return studio.templates().first().toMap().value("items").toList().first().toMap().value("propertyBindings").toMap();
    };
    auto detachedVisibility = visibility;
    detachedVisibility.insert("tag", QStringLiteral(""));
    auto detachedColor = color;
    detachedColor.insert("tag", QStringLiteral(""));
    QCOMPARE(templateBindings(), (QVariantMap{{visibleId, detachedVisibility}, {colorId, detachedColor}}));
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto path = QUrl::fromLocalFile(temp.filePath("faceplate-rules.scada.json"));
    QVERIFY(studio.save(path));
    QVERIFY(studio.open(path));
    studio.selectWidget(instanceId);
    QCOMPARE(studio.widget().value("propertyBindings").toMap(), bindings);
    studio.selectWidget(copyId);
    QCOMPARE(studio.widget().value("propertyBindings").toMap(), copyBindings);

    QVERIFY(studio.removeTag(temperatureId));
    QCOMPARE(studio.widget().value("propertyBindings").toMap(), copyBindings);
    studio.selectWidget(instanceId);
    QCOMPARE(studio.widget().value("propertyBindings").toMap(),
             (QVariantMap{{visibleId, visibility}, {colorId, detachedColor}}));
    QVERIFY(studio.enterComposite(instanceId));
    auto changedProperty = properties.at(1).toMap();
    changedProperty.insert("kind", "captionColor");
    properties.replace(1, changedProperty);
    QVERIFY(studio.setFaceplateProperties(properties));
    studio.leaveComposite();
    QCOMPARE(studio.widget().value("propertyBindings").toMap(), (QVariantMap{{visibleId, visibility}}));
    QCOMPARE(templateBindings(), (QVariantMap{{visibleId, detachedVisibility}}));

    QVERIFY(studio.enterComposite(instanceId));
    studio.selectWidget(lampId);
    studio.removeWidget();
    studio.leaveComposite();
    QVERIFY(!studio.widget().contains("propertyBindings"));
    QVERIFY(templateBindings().isEmpty());
}

void CoreTest::operatorWidgetSettings()
{
    Studio studio;
    studio.newProject(true);
    const auto level = studio.tags().at(0).toMap().value("id");
    const auto feedback = studio.tags().at(3).toMap().value("id");
    const auto command = studio.tags().at(4).toMap().value("id");

    studio.addWidget("switch", 10, 10);
    QVERIFY(studio.configureWidget({{"tag", command}, {"feedback", feedback}}));
    QVERIFY(!studio.configureWidget({{"feedback", level}}));
    QCOMPARE(studio.widget().value("feedback"), feedback);
    QVERIFY(studio.saveTemplate("Тумблер с обратной связью"));
    studio.insertTemplate(studio.templates().first().toMap().value("id").toString());
    QVERIFY(studio.widget().value("tag").toString().isEmpty());
    QVERIFY(studio.widget().value("feedback").toString().isEmpty());
    QVERIFY(studio.removeTag(feedback.toString()));

    studio.addWidget("slider", 10, 10);
    QVERIFY(studio.configureWidget({{"min", -10}, {"max", 20}, {"step", 0.5}}));
    QVERIFY(!studio.configureWidget({{"step", 31}}));
    QVERIFY(!studio.configureWidget({{"step", 0}}));
    QCOMPARE(studio.widget().value("step").toDouble(), 0.5);
    QVERIFY(studio.configureWidget({{"step", QVariant::fromValue(nullptr)}}));
    QVERIFY(!studio.widget().contains("step"));

    studio.addWidget("selector", 10, 10);
    const QVariantMap first{{"label", "Ручной"}, {"value", 1}};
    const QVariantMap second{{"label", "Авто"}, {"value", 2}};
    QVERIFY(!studio.configureWidget({{"positions", QVariantList{first, first}}}));
    QVERIFY(studio.configureWidget({{"positions", QVariantList{first, second}}}));
    QCOMPARE(studio.widget().value("positions").toList().size(), 2);
}

void CoreTest::writeConfirmationSettings()
{
    Studio studio;
    studio.newProject(false);
    const QStringList kinds{"command", "switch", "valve", "setpoint", "slider", "knob", "selector"};
    for (const auto &kind : kinds) {
        studio.addWidget(kind, 10, 10);
        QCOMPARE(studio.widget().value("kind").toString(), kind);
        QVERIFY(!studio.widget().contains("confirmWrite"));
        QVERIFY(studio.configureWidget({{"confirmWrite", false}}));
        QCOMPARE(studio.widget().value("confirmWrite"), QVariant(false));
    }
    QVERIFY(!studio.configureWidget({{"confirmWrite", "false"}}));
    QCOMPARE(studio.widget().value("confirmWrite"), QVariant(false));
    QVERIFY(studio.saveTemplate("Переключатель режимов"));
    studio.insertTemplate(studio.templates().first().toMap().value("id").toString());
    QCOMPARE(studio.widget().value("confirmWrite"), QVariant(false));

    studio.addWidget("command", 10, 10);
    QVERIFY(!studio.widget().contains("confirmWrite"));
    studio.addWidget("value", 10, 10);
    QVERIFY(!studio.configureWidget({{"confirmWrite", false}}));

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto path = QUrl::fromLocalFile(temp.filePath("confirm-write.scada.json"));
    QVERIFY(studio.save(path));
    QVERIFY(studio.open(path));
    const auto items = studio.items();
    QCOMPARE(items.size(), kinds.size() + 3);
    for (qsizetype i = 0; i <= kinds.size(); ++i)
        QCOMPARE(items.at(i).toMap().value("confirmWrite"), QVariant(false));
    QVERIFY(!items.at(kinds.size() + 1).toMap().contains("confirmWrite"));
    QVERIFY(!items.last().toMap().contains("confirmWrite"));
}

void CoreTest::widgetActionSettings()
{
    Studio studio;
    studio.newProject(true);
    const auto input = studio.tags().at(3).toMap().value("id");
    const auto output = studio.tags().at(4).toMap().value("id");
    auto endpoint = engineSource(15115);
    endpoint.remove("id");
    QVERIFY(studio.setSource(endpoint.toVariantMap()));
    const auto telemetrySource = QJsonObject::fromVariantMap(studio.sources().last().toMap());
    auto telemetry = engineTag(telemetrySource, "Обороты JSON", "values.rpm", "number");
    telemetry.remove("id");
    QVERIFY(studio.setTag(telemetry.toVariantMap()));
    const auto jsonNumber = studio.tags().last().toMap().value("id");
    auto status = engineTag(telemetrySource, "Статус JSON", "state", "text", 0);
    status.remove("id");
    QVERIFY(studio.setTag(status.toVariantMap()));
    const auto jsonText = studio.tags().last().toMap().value("id");
    const QVariantMap press{{"event", "press"}, {"action", "write"}, {"tag", output}, {"valueMode", "constant"}, {"value", 1}};
    const QVariantMap release{{"event", "release"}, {"action", "write"}, {"tag", output}, {"valueMode", "constant"}, {"value", 0}};
    const QVariantMap read{{"event", "release"}, {"action", "read"}, {"tag", input}};
    const QVariantMap copy{{"event", "click"}, {"action", "write"}, {"tag", output}, {"valueMode", "tag"}, {"valueTag", input}};
    studio.addWidget("command", 20, 20);
    const auto widget = studio.selectedId();
    auto fromJson = copy;
    fromJson.insert("valueTag", jsonNumber);
    QVERIFY(studio.configureWidget({{"actions", QVariantList{fromJson}}}));
    QVERIFY(studio.configureWidget({{"actions", QVariantList{press, release, read, copy}}}));
    auto invalid = press;
    invalid.insert("event", "change");
    QVERIFY(!studio.configureWidget({{"actions", QVariantList{invalid}}}));
    invalid = press;
    invalid.insert("value", "1");
    QVERIFY(!studio.configureWidget({{"actions", QVariantList{invalid}}}));
    invalid = copy;
    invalid.insert("valueTag", jsonText);
    QVERIFY(!studio.configureWidget({{"actions", QVariantList{invalid}}}));
    invalid.insert("valueTag", Project::id());
    QVERIFY(!studio.configureWidget({{"actions", QVariantList{invalid}}}));
    invalid.insert("valueTag", 1);
    QVERIFY(!studio.configureWidget({{"actions", QVariantList{invalid}}}));

    QVERIFY(studio.saveTemplate("Кнопка удержания"));
    studio.insertTemplate(studio.templates().first().toMap().value("id").toString());
    const auto templateRows = studio.widget().value("actions").toList();
    QCOMPARE(templateRows.size(), 4);
    for (const auto &entry : templateRows)
        QCOMPARE(entry.toMap().value("tag").toString(), QString{});
    QCOMPARE(templateRows.last().toMap().value("valueTag"), QVariant(QString{}));

    QVERIFY(studio.removeTag(output.toString()));
    studio.selectWidget(widget);
    const auto rows = studio.widget().value("actions").toList();
    QCOMPARE(rows.size(), 4);
    QCOMPARE(rows.at(0).toMap().value("event").toString(), QString("press"));
    QCOMPARE(rows.at(0).toMap().value("tag").toString(), QString{});
    QCOMPARE(rows.at(1).toMap().value("event").toString(), QString("release"));
    QCOMPARE(rows.at(1).toMap().value("tag").toString(), QString{});
    QCOMPARE(rows.at(2).toMap().value("tag"), input);
    QCOMPARE(rows.last().toMap().value("tag").toString(), QString{});
    QCOMPARE(rows.last().toMap().value("valueTag"), input);
    QVERIFY(studio.removeTag(input.toString()));
    studio.selectWidget(widget);
    const auto cleared = studio.widget().value("actions").toList().last().toMap();
    QCOMPARE(cleared.value("valueMode").toString(), QString("tag"));
    QCOMPARE(cleared.value("valueTag"), QVariant(QString{}));
}

void CoreTest::faceplateSchema()
{
    auto project = Project::empty();
    const auto driver = source("Источник", 15020, true);
    const auto number = tag(driver, "Уставка", "holding", "u16", 0, true);
    const auto flag = tag(driver, "Режим", "coils", "bool", 0, true);
    project.insert("sources", QJsonArray{driver});
    project.insert("tags", QJsonArray{number, flag});
    const QJsonObject numberPort{{"id", Project::id()}, {"name", "Уставка"}, {"type", "number"}};
    const QJsonObject flagPort{{"id", Project::id()}, {"name", "Режим"}, {"type", "bool"}};
    const QJsonObject command{
        {"id", Project::id()}, {"kind", "command"}, {"title", "Записать"},
        {"x", 80}, {"y", 0}, {"w", 80}, {"h", 52}, {"command", 0},
        {"actions", QJsonArray{
            QJsonObject{{"event", "press"}, {"action", "write"}, {"tag", id(numberPort)},
                        {"valueMode", "tag"}, {"valueTag", id(flagPort)}},
            QJsonObject{{"event", "release"}, {"action", "read"}, {"tag", id(numberPort)}}}}
    };
    const QJsonObject value{{"id", Project::id()}, {"kind", "value"}, {"title", "Уставка"},
                            {"x", 0}, {"y", 0}, {"w", 80}, {"h", 52}, {"tag", id(numberPort)}};
    const QJsonObject type{{"id", Project::id()}, {"name", "Насос"}, {"width", 160}, {"height", 52},
                           {"ports", QJsonArray{numberPort, flagPort}}, {"items", QJsonArray{value, command}}};
    const QJsonObject instance{{"id", Project::id()}, {"kind", "faceplate"}, {"title", "Насос № 1"},
                               {"x", 0}, {"y", 0}, {"w", 240}, {"h", 90}, {"faceplate", id(type)},
                               {"bindings", QJsonObject{{id(numberPort), id(number)}, {id(flagPort), id(flag)}}}};
    const auto screen = project.value("screens").toArray().first().toObject();
    const auto withInstance = [&project, &screen](const QJsonObject &definition, const QJsonObject &item) {
        auto snapshot = project;
        auto layout = screen;
        layout.insert("items", QJsonArray{item});
        snapshot.insert("screens", QJsonArray{layout});
        snapshot.insert("faceplates", QJsonArray{definition});
        return snapshot;
    };

    const auto error = Project::validate(withInstance(type, instance));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto invalid = instance;
    invalid.insert("bindings", QJsonObject{{id(numberPort), id(flag)}});
    QVERIFY(!Project::validate(withInstance(type, invalid)).isEmpty());
    invalid.insert("bindings", QJsonObject{{Project::id(), id(number)}});
    QVERIFY(!Project::validate(withInstance(type, invalid)).isEmpty());
    invalid.insert("bindings", QJsonObject{{id(numberPort), ""}});
    QVERIFY(Project::validate(withInstance(type, invalid)).isEmpty());
    invalid.insert("faceplate", Project::id());
    QVERIFY(!Project::validate(withInstance(type, invalid)).isEmpty());

    auto invalidType = type;
    auto invalidChild = value;
    invalidChild.insert("tag", id(number));
    invalidType.insert("items", QJsonArray{invalidChild, command});
    QVERIFY(!Project::validate(withInstance(invalidType, instance)).isEmpty());
    auto duplicatePort = flagPort;
    duplicatePort.insert("name", "уставка");
    invalidType = type;
    invalidType.insert("ports", QJsonArray{numberPort, duplicatePort});
    QVERIFY(!Project::validate(withInstance(invalidType, instance)).isEmpty());
    invalidType = type;
    invalidType.insert("width", 159);
    QVERIFY(!Project::validate(withInstance(invalidType, instance)).isEmpty());

    auto templateProject = withInstance(type, instance);
    auto templateInstance = instance;
    templateInstance.insert("id", Project::id());
    QJsonObject panelTemplate{{"id", Project::id()}, {"name", "Панель"}, {"items", QJsonArray{templateInstance}}};
    templateProject.insert("templates", QJsonArray{panelTemplate});
    QVERIFY(!Project::validate(templateProject).isEmpty());
    templateInstance.remove("bindings");
    panelTemplate.insert("items", QJsonArray{templateInstance});
    templateProject.insert("templates", QJsonArray{panelTemplate});
    QVERIFY(Project::validate(templateProject).isEmpty());
}

void CoreTest::customWidgetConstructor()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    Studio studio;
    const auto sample = studio.faceplates().last().toMap();
    QCOMPARE(sample.value("name").toString(), QString("Насос · конструктор"));
    QCOMPARE(studio.screens().last().toMap().value("name").toString(), QString("Свой насос"));
    const auto samplePackage = QUrl::fromLocalFile(temp.filePath("pump.faceplate.json"));
    QVERIFY(studio.exportFaceplate(sample.value("id").toString(), samplePackage));
    studio.newProject(false);
    QVERIFY(!studio.createFaceplate("Неверный размер", 79, 330));
    QVERIFY(studio.createFaceplate("Мой виджет", 640, 330));
    QCOMPARE(studio.canvasSettings().value("width").toInt(), 640);
    QCOMPARE(studio.canvasSettings().value("height").toInt(), 330);
    QVERIFY(studio.items().isEmpty());
    const auto draftId = studio.canvasSettings().value("id").toString();
    studio.addWidget("shape", 25, 30);
    const auto shapeId = studio.selectedId();
    QVERIFY(studio.configureWidget({{"shape", "ellipse"}}));
    studio.moveWidget(shapeId, 28, 31, 42, 36);
    QCOMPARE(studio.widget().value("w").toInt(), 42);
    QVERIFY(studio.configureWidget({{"locked", true}}));
    studio.moveWidget(shapeId, 50, 50, 52, 52);
    QCOMPARE(studio.widget().value("x").toInt(), 28);
    QCOMPARE(studio.widget().value("w").toInt(), 42);
    studio.addWidget("hitArea", 25, 30);
    const auto hitId = studio.selectedId();
    studio.moveLayer(-1);
    QCOMPARE(studio.items().first().toMap().value("id").toString(), hitId);
    studio.moveLayer(1);
    QCOMPARE(studio.items().last().toMap().value("id").toString(), hitId);

    QVERIFY(studio.setFaceplatePorts({QVariantMap{{"name", "Сигнал"}, {"type", "bool"}}}));
    const auto port = studio.editorTags().first().toMap().value("id").toString();
    QCOMPARE(studio.selectedId(), hitId);
    const QVariantMap appearance{{"tag", port}, {"rules", QVariantList{
        QVariantMap{{"op", "eq"}, {"limit", 1}, {"visible", true}, {"text", "Активный"}}}}};
    QVERIFY(studio.configureWidget({{"visible", false}, {"appearance", appearance}}));
    QVERIFY(studio.widgetColors(appearance, {{"value", 0}, {"quality", "good"}, {"hasValue", true}}).isEmpty());
    QVERIFY(studio.widgetColors(appearance, {{"value", 1}, {"quality", "stale"}, {"hasValue", true}}).isEmpty());
    const auto matched = studio.widgetColors(appearance, {{"value", true}, {"quality", "good"}, {"hasValue", true}});
    QCOMPARE(matched.value("visible").toBool(), true);
    QCOMPARE(matched.value("text").toString(), QString("Активный"));
    const QVariantMap write{{"event", "press"}, {"action", "write"}, {"tag", port},
                            {"valueMode", "constant"}, {"value", 1}};
    QVERIFY(studio.configureWidget({{"actions", QVariantList{write}}}));

    QFile source(Project::demoPumpIdle);
    QVERIFY(source.open(QIODevice::ReadOnly));
    const auto picturePath = temp.filePath("local.svg");
    QFile local(picturePath);
    QVERIFY(local.open(QIODevice::WriteOnly));
    const auto bytes = source.readAll();
    QVERIFY(!bytes.isEmpty());
    QCOMPARE(local.write(bytes), bytes.size());
    local.close();
    studio.addWidget("image", 90, 45);
    studio.setImage(QUrl::fromLocalFile(picturePath));
    const QVariantMap animation{{"kind", "rotate"}, {"tag", port}, {"op", "eq"}, {"limit", 1},
                                {"periodMs", 850}, {"reverse", true}};
    QVERIFY(studio.configureWidget({{"animation", animation}}));
    auto invalidAnimation = animation;
    invalidAnimation.insert("periodMs", 5);
    QVERIFY(!studio.configureWidget({{"animation", invalidAnimation}}));
    invalidAnimation = animation;
    invalidAnimation.insert("tag", Project::id());
    QVERIFY(!studio.configureWidget({{"animation", invalidAnimation}}));
    QCOMPARE(studio.widget().value("animation").toMap(), animation);
    QVERIFY(studio.animationActive(animation, {{"value", true}, {"quality", "good"}, {"hasValue", true}}));
    QVERIFY(!studio.animationActive(animation, {{"value", false}, {"quality", "good"}, {"hasValue", true}}));
    QVERIFY(!studio.animationActive(animation, {{"value", true}, {"quality", "stale"}, {"hasValue", true}}));
    QVERIFY(!studio.animationActive(animation, {{"quality", "good"}, {"hasValue", false}}));
    const auto image = studio.widget().value("image").toString();
    QVERIFY(image.endsWith(".svg"));
    QVERIFY(QFile::exists(studio.imageUrl(image).toLocalFile()));
    QVERIFY(local.remove());
    const auto project = QUrl::fromLocalFile(temp.filePath("constructor.scada.json"));
    QVERIFY(studio.save(project));
    QVERIFY(studio.open(project));
    QVERIFY(studio.enterComposite(draftId));
    QFile savedImage(studio.imageUrl(image).toLocalFile());
    QVERIFY(savedImage.open(QIODevice::ReadOnly));
    QCOMPARE(savedImage.readAll(), bytes);
    QCOMPARE(studio.items().last().toMap().value("image").toString(), image);
    QCOMPARE(studio.items().last().toMap().value("animation").toMap(), animation);
    const auto copyId = studio.duplicateFaceplate(draftId);
    QVERIFY(!copyId.isEmpty());
    const auto copy = studio.faceplates().last().toMap();
    const auto copyTag = copy.value("ports").toList().first().toMap().value("id").toString();
    QVERIFY(copyTag != port);
    QCOMPARE(copy.value("items").toList().last().toMap().value("animation").toMap().value("tag").toString(), copyTag);
    QVERIFY(studio.setFaceplatePorts({}));
    QCOMPARE(studio.items().last().toMap().value("animation").toMap().value("tag").toString(), QString{});
    studio.selectWidget(studio.items().last().toMap().value("id").toString());
    QVERIFY(studio.configureWidget({{"animation", QVariant{}}}));
    QVERIFY(!studio.widget().contains("animation"));
    studio.leaveComposite();

    const auto imported = studio.importFaceplate(samplePackage);
    QVERIFY(!imported.isEmpty());
    const auto importedType = studio.faceplates().last().toMap();
    const auto importedPorts = importedType.value("ports").toList();
    const auto storedLayers = importedType.value("items").toList();
    QCOMPARE(storedLayers.last().toMap().value("animation").toMap().value("tag"),
             importedPorts.first().toMap().value("id"));
    QCOMPARE(storedLayers.at(5).toMap().value("animation").toMap().value("tag"),
             importedPorts.at(1).toMap().value("id"));
    studio.leaveComposite();
    studio.insertFaceplate(imported);
    const auto resolved = studio.resolveFaceplate(studio.widget());
    QCOMPARE(resolved.size(), 10);
    QCOMPARE(resolved.last().toMap().value("animation").toMap().value("tag").toString(), QString{});
    for (const auto &layer : resolved) {
        const auto reference = layer.toMap().value("image").toString();
        if (reference.isEmpty())
            continue;
        QVERIFY(reference.startsWith("images/"));
        QVERIFY(QFile::exists(studio.imageUrl(reference).toLocalFile()));
    }
}

QTEST_GUILESS_MAIN(CoreTest)
#include "core_test.moc"
