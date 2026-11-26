#include "project.h"

#include "drivers.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace {
bool finite(const QJsonValue &value, double min, double max)
{
    return value.isDouble() && std::isfinite(value.toDouble())
        && value.toDouble() >= min && value.toDouble() <= max;
}

bool nameValid(const QJsonValue &value)
{
    return value.isString() && !value.toString().trimmed().isEmpty();
}

bool validColor(const QJsonValue &value)
{
    static const QRegularExpression pattern("^#[0-9a-fA-F]{6}$");
    return value.isString() && pattern.match(value.toString()).hasMatch();
}

bool validImagePath(const QString &path)
{
    static const QRegularExpression pattern("^images/[a-f0-9-]{36}\\.(png|jpg|jpeg|bmp|svg)$");
    return path.isEmpty() || Project::isDemoImage(path) || pattern.match(path).hasMatch();
}

bool colorsValid(const QJsonObject &object)
{
    for (const auto *key : {"background", "value"}) {
        if (object.contains(key) && !validColor(object.value(key)))
            return false;
    }
    return true;
}

bool numericOperator(const QString &op)
{
    static const QSet<QString> operators{"lt", "lte", "eq", "ne", "gte", "gt", "between", "outside"};
    return operators.contains(op);
}

bool rangeOperator(const QString &op)
{
    return op == "between" || op == "outside";
}

QString validateAppearance(const QJsonValue &value, const QSet<QString> &tags,
                           const QSet<QString> &textTags, bool isTemplate)
{
    if (value.isUndefined())
        return {};
    const auto appearance = value.toObject();
    if (!value.isObject() || !colorsValid(appearance))
        return "Задайте цвета виджета в формате #RRGGBB.";
    const auto tag = appearance.value("tag");
    if (!tag.isUndefined() && (!tag.isString() || (!tag.toString().isEmpty() &&
        (isTemplate || !tags.contains(tag.toString()) || textTags.contains(tag.toString())))))
        return "Для правил цвета нужен существующий числовой или логический тег.";
    const auto rules = appearance.value("rules");
    if (rules.isUndefined())
        return {};
    if (!rules.isArray() || rules.toArray().size() > 32)
        return "Можно задать до 32 правил цвета на виджет.";
    for (const auto &entry : rules.toArray()) {
        const auto rule = entry.toObject();
        const auto op = rule.value("op").toString();
        if (!numericOperator(op) || !finite(rule.value("limit"), -1e100, 1e100))
            return "Укажите условие и числовой порог для каждого правила.";
        const auto visibility = rule.value("visible");
        const auto text = rule.value("text");
        if (!colorsValid(rule) || (!visibility.isUndefined() && !visibility.isBool()) ||
            (!text.isUndefined() && (!text.isString() || text.toString().size() > 240)))
            return "Проверьте цвета, видимость и подпись в правиле оформления.";
        if (!rule.contains("background") && !rule.contains("value") && visibility.isUndefined() && text.isUndefined())
            return "Для каждого правила задайте цвет, видимость или подпись.";
        if (rangeOperator(op) && (!finite(rule.value("max"), -1e100, 1e100)
             || rule.value("max").toDouble() < rule.value("limit").toDouble()))
            return "Верхняя граница диапазона должна быть не меньше нижней.";
    }
    return {};
}

QString validateAnimation(const QJsonValue &value, const QSet<QString> &tags,
                          const QSet<QString> &textTags, bool isTemplate)
{
    if (value.isUndefined())
        return {};
    const auto animation = value.toObject();
    const auto kind = animation.value("kind").toString();
    if (!value.isObject() || (kind != "rotate" && kind != "blink"))
        return "Выберите вращение или мигание слоя.";
    const auto tag = animation.value("tag");
    if (!tag.isString() || (!tag.toString().isEmpty() &&
        (isTemplate || !tags.contains(tag.toString()) || textTags.contains(tag.toString()))))
        return "Для анимации выберите числовой или логический тег.";
    const auto op = animation.value("op").toString();
    const auto lower = animation.value("limit");
    if (!numericOperator(op) || !finite(lower, -1e100, 1e100)
        || (rangeOperator(op) && !finite(animation.value("max"), lower.toDouble(), 1e100)))
        return "Проверьте условие и пороги анимации.";
    const auto period = animation.value("periodMs");
    if (!finite(period, kind == "blink" ? 400 : 100, 60000) || period.toDouble() != period.toInt())
        return "Укажите длительность цикла в миллисекундах.";
    if (animation.contains("reverse") && (kind != "rotate" || !animation.value("reverse").isBool()))
        return "Проверьте направление вращения.";
    if (animation.contains("dimPercent") && (kind != "blink" || !finite(animation.value("dimPercent"), 0, 95)))
        return "Укажите видимость в тусклой фазе от 0 до 95 %.";
    return {};
}

QString validateActions(const QJsonValue &value, const QSet<QString> &tags, const QSet<QString> &textTags,
                        const QSet<QString> &modbusTags, bool isTemplate, bool analogControl)
{
    const auto rows = value.toArray();
    if (!value.isArray() || rows.size() > 32)
        return "Можно задать до 32 действий для элемента управления.";
    for (const auto &entry : rows) {
        const auto row = entry.toObject();
        const auto event = row.value("event").toString();
        const auto action = row.value("action").toString();
        const auto tag = row.value("tag");
        const auto id = tag.toString();
        if ((event != "press" && event != "release" && event != "click" && (event != "change" || !analogControl))
            || (action != "write" && action != "read"))
            return "Выберите подходящее событие и действие чтения или записи.";
        if (!tag.isString() || (!id.isEmpty() && (isTemplate || !tags.contains(id))))
            return "В действии указан недоступный тег.";
        if (action == "read" && !id.isEmpty() && !modbusTags.contains(id))
            return "Чтение по событию доступно для тегов Modbus TCP.";
        if (action == "write") {
            const auto mode = row.value("valueMode").toString();
            if (textTags.contains(id) || (mode != "widget" && mode != "constant" && mode != "tag"))
                return "Для записи выберите числовой или логический тег и укажите источник значения.";
            if (mode == "constant" && !finite(row.value("value"), -1e100, 1e100))
                return "Введите конечное число для записи.";
            if (mode == "tag") {
                const auto valueTag = row.value("valueTag");
                const auto sourceId = valueTag.toString();
                if (!valueTag.isString() || (!sourceId.isEmpty() &&
                    (isTemplate || !tags.contains(sourceId) || textTags.contains(sourceId))))
                    return "Источником значения должен быть существующий числовой или логический тег.";
            }
        }
    }
    return {};
}

int compare(double a, double b)
{
    const double epsilon = std::max({1.0, std::abs(a), std::abs(b)}) * (8 * std::numeric_limits<double>::epsilon());
    if (std::abs(a - b) <= epsilon)
        return 0;
    return a < b ? -1 : 1;
}

std::optional<double> goodNumber(const QVariantMap &reading)
{
    if (reading.value("quality").toString() != "good" || !reading.value("hasValue").toBool())
        return std::nullopt;
    const auto raw = reading.value("value");
    const int type = raw.typeId();
    if (type != QMetaType::Double && type != QMetaType::Float && type != QMetaType::Int
        && type != QMetaType::UInt && type != QMetaType::LongLong && type != QMetaType::ULongLong
        && type != QMetaType::Bool)
        return std::nullopt;
    const auto value = raw.toDouble();
    if (!std::isfinite(value))
        return std::nullopt;
    return value;
}

std::optional<bool> matchesRule(const QVariantMap &rule, double value)
{
    const auto op = rule.value("op").toString();
    bool parsed = false;
    const auto lower = rule.value("limit").toDouble(&parsed);
    if (!parsed || !std::isfinite(lower) || !numericOperator(op))
        return std::nullopt;
    const int order = compare(value, lower);
    if (rangeOperator(op)) {
        const auto upper = rule.value("max").toDouble(&parsed);
        if (!parsed || !std::isfinite(upper) || upper < lower)
            return std::nullopt;
        const bool inside = order >= 0 && compare(value, upper) <= 0;
        return op == "between" ? inside : !inside;
    }
    return (op == "lt" && order < 0) || (op == "lte" && order <= 0) ||
        (op == "eq" && order == 0) || (op == "ne" && order != 0) ||
        (op == "gte" && order >= 0) || (op == "gt" && order > 0);
}

bool uniqueId(const QJsonObject &object, QSet<QString> &ids)
{
    const auto id = object.value("id").toString();
    if (QUuid::fromString(id).isNull() || ids.contains(id))
        return false;
    ids.insert(id);
    return true;
}

QUuid faceplateOriginId(const QJsonObject &object)
{
    const auto origin = object.value("originId");
    return QUuid::fromString(origin.isUndefined() ? object.value("id").toString() : origin.toString());
}

bool uniqueFaceplateOrigin(const QJsonObject &object, QSet<QUuid> &origins)
{
    const auto origin = faceplateOriginId(object);
    if (origin.isNull() || origins.contains(origin))
        return false;
    origins.insert(origin);
    return true;
}

bool validFaceplateMappings(const QJsonValue &value, bool allowSameType = false)
{
    if (value.isUndefined())
        return true;
    if (!value.isObject())
        return false;
    const auto mappings = value.toObject();
    for (auto source = mappings.constBegin(); source != mappings.constEnd(); ++source) {
        if (QUuid::fromString(source.key()).isNull() || !source.value().isObject())
            return false;
        const auto targets = source.value().toObject();
        for (auto target = targets.constBegin(); target != targets.constEnd(); ++target) {
            if (QUuid::fromString(target.key()).isNull() || (!allowSameType && source.key() == target.key()) ||
                !target.value().isObject())
                return false;
            const auto fields = target.value().toObject();
            for (auto field = fields.constBegin(); field != fields.constEnd(); ++field) {
                if (QUuid::fromString(field.key()).isNull() || QUuid::fromString(field.value().toString()).isNull())
                    return false;
            }
        }
    }
    return true;
}

QString validateInterfaceProperties(const QJsonObject &faceplate, QSet<QString> &ids,
                                    QHash<QString, QString> &propertyKinds)
{
    const auto properties = faceplate.value("properties");
    if (properties.isUndefined())
        return {};
    const auto definitions = properties.toArray();
    if (!properties.isArray() || definitions.size() > 128)
        return "Для типа можно задать до 128 свойств интерфейса.";

    QSet<QString> itemIds;
    for (const auto &entry : faceplate.value("items").toArray())
        itemIds.insert(entry.toObject().value("id").toString());

    static const QSet<QString> kinds{"title", "background", "value", "captionColor", "visible"};
    QSet<QString> names;
    QSet<QUuid> origins;
    QHash<QString, QSet<QString>> itemProperties;
    for (const auto &entry : definitions) {
        const auto property = entry.toObject();
        const auto name = property.value("name").toString();
        const auto foldedName = name.trimmed().toCaseFolded();
        if (!uniqueId(property, ids) || foldedName.isEmpty() || name.size() > 80 || names.contains(foldedName))
            return "У свойств интерфейса должны быть уникальные идентификаторы и имена длиной до 80 символов.";
        if (!uniqueFaceplateOrigin(property, origins))
            return "Неверное или повторяющееся происхождение свойства интерфейса.";
        const auto kind = property.value("kind").toString();
        if (!kinds.contains(kind))
            return "Выберите подпись, цвет или видимость элемента.";
        const auto item = property.value("item").toString();
        if (item.isEmpty() || !itemIds.contains(item))
            return "Выберите существующий элемент этого типа лицевой панели.";
        auto &assigned = itemProperties[item];
        if (assigned.contains(kind))
            return "Свойство элемента может быть включено в интерфейс только один раз.";
        const auto defaultVisible = property.value("defaultVisible");
        if (!defaultVisible.isUndefined() && (kind != "visible" || !defaultVisible.isBool()))
            return "Видимость по умолчанию задаётся логическим значением у свойства видимости.";
        names.insert(foldedName);
        assigned.insert(kind);
        propertyKinds.insert(property.value("id").toString(), kind);
    }
    return {};
}

QString validateInterfaceOverrides(const QJsonValue &value, const QHash<QString, QString> &propertyKinds)
{
    if (value.isUndefined())
        return {};
    if (!value.isObject())
        return "Свойства экземпляра лицевой панели должны быть объектом.";
    const auto overrides = value.toObject();
    for (auto entry = overrides.constBegin(); entry != overrides.constEnd(); ++entry) {
        const auto kind = propertyKinds.value(entry.key());
        if (kind.isEmpty())
            return "Экземпляр лицевой панели ссылается на отсутствующее свойство интерфейса.";
        const auto setting = entry.value();
        const bool valid = kind == "title" ? setting.isString() :
            kind == "visible" ? setting.isBool() : validColor(setting);
        if (!valid)
            return "Значение экземпляра должно соответствовать типу свойства интерфейса.";
    }
    return {};
}

QString validatePropertyBindings(const QJsonValue &value, const QHash<QString, QString> &propertyKinds,
                                 const QSet<QString> &tags, const QSet<QString> &textTags, bool isTemplate)
{
    if (value.isUndefined())
        return {};
    if (!value.isObject())
        return "Привязки свойств экземпляра должны быть объектом.";
    const auto bindings = value.toObject();
    for (auto entry = bindings.constBegin(); entry != bindings.constEnd(); ++entry) {
        const auto kind = propertyKinds.value(entry.key());
        if (kind.isEmpty() || kind == "title")
            return "Тег можно привязать к опубликованному цвету или видимости элемента.";
        if (!entry.value().isObject())
            return "Для привязки свойства задайте тег, условие и значение.";
        const auto rule = entry.value().toObject();
        const auto tag = rule.value("tag");
        const auto tagId = tag.toString();
        if (!tag.isString() || (!tagId.isEmpty() && (isTemplate || !tags.contains(tagId) || textTags.contains(tagId))))
            return "Для привязки свойства нужен доступный числовой или логический тег.";
        const auto op = rule.value("op").toString();
        constexpr double maxNumber = std::numeric_limits<double>::max();
        const auto limit = rule.value("limit");
        const auto max = rule.value("max");
        if (op != "alarm" && (!numericOperator(op) || !finite(limit, -maxNumber, maxNumber)))
            return "Выберите условие тревоги или числовое условие с порогом.";
        if (rangeOperator(op) && !finite(max, limit.toDouble(), maxNumber))
            return "Верхняя граница диапазона должна быть не меньше нижней.";
        const auto validValue = [&kind](const QJsonValue &value) {
            return kind == "visible" ? value.isBool() : validColor(value);
        };
        const auto otherwise = rule.value("otherwise");
        if (!validValue(rule.value("value")) || (!otherwise.isUndefined() && !validValue(otherwise)))
            return "Для видимости выберите логическое значение, для цвета задайте #RRGGBB.";
    }
    return {};
}
}

bool Project::isDemoImage(const QString &path)
{
    return path == demoImage || path == demoPumpIdle || path == demoPumpRunning || path == demoPumpImpeller;
}

QSize Project::minimumWidgetSize(const QString &kind)
{
    return kind == "shape" || kind == "hitArea" ? QSize(8, 8) : QSize(80, 52);
}

QVariantMap Project::widgetColors(const QVariantMap &appearance, const QVariantMap &reading)
{
    QVariantMap colors;
    for (const auto *key : {"background", "value"}) {
        const auto color = appearance.value(key);
        if (color.isValid()) colors.insert(key, color);
    }
    const auto value = goodNumber(reading);
    if (!value)
        return colors;
    for (const auto &entry : appearance.value("rules").toList()) {
        const auto rule = entry.toMap();
        if (!matchesRule(rule, *value).value_or(false))
            continue;
        for (const auto *key : {"background", "value", "visible", "text"}) {
            const auto color = rule.value(key);
            if (color.isValid()) colors.insert(key, color);
        }
        break;
    }
    return colors;
}

bool Project::animationActive(const QVariantMap &animation, const QVariantMap &reading)
{
    const auto value = goodNumber(reading);
    return value && matchesRule(animation, *value).value_or(false);
}

QVariantMap Project::faceplatePropertyValues(const QVariantMap &bindings, const QVariantMap &readings,
                                            const QVariantList &alarms)
{
    QVariantMap values;
    for (auto binding = bindings.constBegin(); binding != bindings.constEnd(); ++binding) {
        const auto rule = binding.value().toMap();
        const auto tag = rule.value("tag").toString();
        if (tag.isEmpty())
            continue;
        bool matched = false;
        if (rule.value("op").toString() == "alarm") {
            matched = std::any_of(alarms.cbegin(), alarms.cend(), [&tag](const QVariant &alarm) {
                return alarm.toMap().value("tag").toString() == tag;
            });
        } else {
            const auto reading = goodNumber(readings.value(tag).toMap());
            if (!reading)
                continue;
            const auto result = matchesRule(rule, *reading);
            if (!result)
                continue;
            matched = *result;
        }
        const auto value = rule.value(matched ? "value" : "otherwise");
        if (value.isValid())
            values.insert(binding.key(), value);
    }
    return values;
}

QString Project::id()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QJsonObject Project::empty()
{
    return {{"version", 1}, {"id", id()}, {"name", "Новый проект"},
            {"sources", QJsonArray{}}, {"tags", QJsonArray{}},
            {"screens", QJsonArray{QJsonObject{{"id", id()}, {"name", "Главный экран"},
                                                 {"items", QJsonArray{}}}}}};
}

QJsonObject Project::find(const QJsonObject &project, const QString &array, const QString &id)
{
    for (const auto &value : project.value(array).toArray()) {
        const auto object = value.toObject();
        if (object.value("id").toString() == id)
            return object;
    }
    return {};
}

QSize Project::screenSize(const QJsonObject &screen)
{
    return {screen.value("width").toInt(1200), screen.value("height").toInt(692)};
}

QString Project::validateScreenSettings(const QJsonObject &screen)
{
    const auto integral = [&](const QString &name, int min, int max) {
        const auto number = screen.value(name);
        return number.isUndefined() || (finite(number, min, max) && number.toDouble() == number.toInt());
    };
    if (!integral("width", 240, 8192) || !integral("height", 160, 8192))
        return "Ширина экрана должна быть от 240 до 8192 px, высота — от 160 до 8192 px.";
    if (!integral("gridStep", 2, 200))
        return "Шаг сетки должен быть целым числом от 2 до 200 px.";
    if (screen.contains("background") && !validColor(screen.value("background")))
        return "Задайте цвет фона экрана в формате #RRGGBB.";
    const auto image = screen.value("backgroundImage");
    if (!image.isUndefined() && (!image.isString() || !validImagePath(image.toString())))
        return "Фоновое изображение должно находиться в папке images проекта.";
    const auto fit = screen.value("backgroundFit");
    if (!fit.isUndefined() && fit != "contain" && fit != "cover" && fit != "stretch")
        return "Выберите способ размещения фонового изображения.";
    const auto operatorFit = screen.value("operatorFit");
    if (!operatorFit.isUndefined() && operatorFit != "fill" && operatorFit != "fit")
        return "Выберите способ отображения мнемосхемы в операторском режиме.";
    return {};
}

QString Project::validate(const QJsonObject &project)
{
    if (project.value("version").toDouble() != 1)
        return "Эта версия программы поддерживает формат проекта 1.";
    QSet<QString> ids;
    if (!uniqueId(project, ids) || !nameValid(project.value("name")))
        return "У проекта должно быть имя и уникальный идентификатор.";
    if (!project.value("sources").isArray() || !project.value("tags").isArray()
        || !project.value("screens").isArray() || project.value("screens").toArray().isEmpty())
        return "В проекте должны быть описаны источники, теги и хотя бы один экран.";

    QHash<QString, const Driver *> sourceDrivers;
    QSet<QString> sourceNames;
    for (const auto &entry : project.value("sources").toArray()) {
        const auto source = entry.toObject();
        const auto name = source.value("name").toString().trimmed();
        if (!uniqueId(source, ids) || name.isEmpty() || sourceNames.contains(name.toCaseFolded()))
            return "У источников должны быть разные имена и идентификаторы.";
        const auto *driver = findDriver(source.value("driver").toString());
        if (!driver)
            return QString("Источник «%1» использует неподдерживаемый протокол.").arg(name);
        if (const auto error = driver->validateSource(source); !error.isEmpty())
            return QString("Источник «%1». %2").arg(name, error);
        sourceNames.insert(name.toCaseFolded());
        sourceDrivers.insert(source.value("id").toString(), driver);
    }

    QSet<QString> tagIds;
    QSet<QString> tagNames;
    QSet<QString> textTags;
    QSet<QString> binaryTags;
    QSet<QString> modbusTags;
    const auto *modbus = findDriver("modbus-tcp");
    for (const auto &entry : project.value("tags").toArray()) {
        const auto tag = entry.toObject();
        const auto name = tag.value("name").toString().trimmed();
        if (!uniqueId(tag, ids) || name.isEmpty() || tagNames.contains(name.toCaseFolded()))
            return "У тегов должны быть разные имена и идентификаторы.";
        const auto *driver = sourceDrivers.value(tag.value("source").toString());
        if (!driver)
            return QString("Выберите источник для тега «%1».").arg(name);
        if (const auto error = driver->validateTag(tag); !error.isEmpty())
            return QString("Тег «%1». %2").arg(name, error);
        const auto interval = tag.value("archiveMs");
        if (!finite(interval, 0, 86400000) || interval.toDouble() != interval.toInt()
            || (interval.toInt() > 0 && interval.toInt() < 100))
            return QString("Тег «%1». Период архива задаётся целым числом от 100 до 86400000 мс или 0.").arg(name);
        const auto alarm = tag.value("alarm");
        if (!alarm.isUndefined()) {
            const auto settings = alarm.toObject();
            const auto direction = settings.value("direction").toString();
            if ((direction != "high" && direction != "low") ||
                !finite(settings.value("limit"), -1e100, 1e100) ||
                !finite(settings.value("hysteresis"), 0, 1e100))
                return QString("Тег «%1». Проверьте порог и гистерезис тревоги.").arg(name);
        }
        tagNames.insert(name.toCaseFolded());
        tagIds.insert(tag.value("id").toString());
        const auto type = tag.value("address").toObject().value("type").toString();
        if (type == "text") textTags.insert(tag.value("id").toString());
        if (type == "bool") binaryTags.insert(tag.value("id").toString());
        if (driver == modbus) modbusTags.insert(tag.value("id").toString());
    }

    const QSet<QString> kinds{"value", "indicator", "trend", "label", "gauge", "command", "link", "image", "tank", "pump",
                              "dial", "column", "ring", "segments", "signal", "compass", "bargraph",
                              "switch", "setpoint", "slider", "knob", "selector", "valve", "motor", "pipe", "alarm",
                              "faceplate", "shape", "hitArea"};
    const QSet<QString> analogControls{"setpoint", "slider", "knob"};
    const QSet<QString> ranged{"gauge", "tank", "dial", "column", "ring", "bargraph", "setpoint", "slider", "knob"};
    QSet<QString> screenIds;
    QSet<QString> screenNames;
    QSet<QString> templateNames;
    const auto screens = project.value("screens").toArray();
    for (const auto &entry : screens)
        screenIds.insert(entry.toObject().value("id").toString());
    auto layouts = screens;
    const auto templates = project.value("templates");
    if (!templates.isUndefined() && !templates.isArray())
        return "Ожидается список шаблонов.";
    for (const auto &entry : templates.toArray())
        layouts.append(entry);

    const auto regularLayoutCount = layouts.size();
    const auto faceplates = project.value("faceplates");
    if (!faceplates.isUndefined() && !faceplates.isArray())
        return "Ожидается список типов лицевых панелей.";
    if (!validFaceplateMappings(project.value("faceplateMappings")))
        return "Неверный формат запомненных сопоставлений лицевых панелей.";
    if (!validFaceplateMappings(project.value("faceplateImportMappings"), true))
        return "Неверный формат запомненных сопоставлений при импорте лицевых панелей.";
    struct FaceplateInterface {
        QHash<QString, QString> ports;
        QHash<QString, QString> properties;
    };
    QHash<QString, FaceplateInterface> interfaceTypes;
    QSet<QString> faceplateNames;
    for (const auto &entry : faceplates.toArray()) {
        const auto faceplate = entry.toObject();
        const auto name = faceplate.value("name").toString().trimmed().toCaseFolded();
        if (!uniqueId(faceplate, ids) || name.isEmpty() || faceplateNames.contains(name))
            return "У типов лицевых панелей должны быть разные имена и идентификаторы.";
        if (faceplateOriginId(faceplate).isNull())
            return "Неверное происхождение типа лицевой панели.";
        const auto width = faceplate.value("width");
        const auto height = faceplate.value("height");
        if (!finite(width, 80, 8192) || width.toDouble() != width.toInt() ||
            !finite(height, 52, 8192) || height.toDouble() != height.toInt())
            return "Укажите целые размеры типа лицевой панели от 80 × 52 до 8192 × 8192 px.";
        if (!faceplate.value("ports").isArray() || !faceplate.value("items").isArray())
            return "Для типа лицевой панели задайте списки интерфейсных тегов и элементов.";

        QHash<QString, QString> portTypes;
        QSet<QString> portNames;
        QSet<QUuid> portOrigins;
        for (const auto &portEntry : faceplate.value("ports").toArray()) {
            const auto port = portEntry.toObject();
            const auto portName = port.value("name").toString().trimmed().toCaseFolded();
            const auto type = port.value("type").toString();
            if (!uniqueId(port, ids) || portName.isEmpty() || portNames.contains(portName))
                return "Интерфейсным тегам типа нужны уникальные имена и идентификаторы.";
            if (!uniqueFaceplateOrigin(port, portOrigins))
                return "Неверное или повторяющееся происхождение интерфейсного тега.";
            if (type != "number" && type != "bool" && type != "text")
                return "Выберите числовой, логический или текстовый тип интерфейсного тега.";
            portNames.insert(portName);
            portTypes.insert(port.value("id").toString(), type);
        }
        QHash<QString, QString> propertyKinds;
        if (const auto error = validateInterfaceProperties(faceplate, ids, propertyKinds); !error.isEmpty())
            return QString("Тип «%1». %2").arg(faceplate.value("name").toString(), error);
        faceplateNames.insert(name);
        interfaceTypes.insert(faceplate.value("id").toString(), FaceplateInterface{portTypes, propertyKinds});
        layouts.append(entry);
    }

    for (qsizetype index = 0; index < layouts.size(); ++index) {
        const auto screen = layouts[index].toObject();
        const bool isFaceplate = index >= regularLayoutCount;
        const bool isTemplate = !isFaceplate && index >= screens.size();
        auto &names = isTemplate ? templateNames : screenNames;
        const auto name = screen.value("name").toString().trimmed();
        const auto items = screen.value("items").toArray();
        QSet<QString> portIds;
        QSet<QString> textPorts;
        QSet<QString> binaryPorts;
        QSet<QString> readablePorts;
        if (isFaceplate) {
            const auto ports = interfaceTypes.value(screen.value("id").toString()).ports;
            for (auto port = ports.cbegin(); port != ports.cend(); ++port) {
                portIds.insert(port.key());
                if (port.value() == "text")
                    textPorts.insert(port.key());
                else
                    readablePorts.insert(port.key());
                if (port.value() == "bool")
                    binaryPorts.insert(port.key());
            }
        } else {
            if (!uniqueId(screen, ids) || name.isEmpty() || names.contains(name.toCaseFolded())
                || !screen.value("items").isArray() || (isTemplate && items.isEmpty()))
                return isTemplate ? "У шаблона должны быть уникальное имя и список элементов."
                                  : "У каждого экрана должны быть своё имя, идентификатор и список элементов.";
            if (const auto error = validateScreenSettings(screen); !error.isEmpty())
                return QString("Экран «%1». %2").arg(name, error);
            names.insert(name.toCaseFolded());
        }

        const auto &layoutTags = isFaceplate ? portIds : tagIds;
        const auto &layoutTextTags = isFaceplate ? textPorts : textTags;
        const auto &layoutBinaryTags = isFaceplate ? binaryPorts : binaryTags;
        const auto &layoutReadableTags = isFaceplate ? readablePorts : modbusTags;
        const auto size = screenSize(screen);
        for (const auto &entry : items) {
            const auto item = entry.toObject();
            const auto kind = item.value("kind").toString();
            const auto minimum = minimumWidgetSize(kind);
            if (!uniqueId(item, ids) || !kinds.contains(kind) || !item.value("title").isString()
                || !finite(item.value("x"), 0, size.width() - minimum.width())
                || !finite(item.value("y"), 0, size.height() - minimum.height())
                || !finite(item.value("w"), minimum.width(), size.width())
                || !finite(item.value("h"), minimum.height(), size.height())
                || item.value("x").toDouble() > size.width() - item.value("w").toDouble()
                || item.value("y").toDouble() > size.height() - item.value("h").toDouble())
                return QString("В макете «%1» есть элемент с недопустимыми параметрами.").arg(name);
            const auto typeId = item.value("faceplate");
            const auto bindings = item.value("bindings");
            const auto overrides = item.value("overrides");
            const auto propertyBindings = item.value("propertyBindings");
            if (kind == "faceplate") {
                if (isFaceplate)
                    return "Внутри типа лицевой панели размещаются отдельные виджеты.";
                const auto type = interfaceTypes.constFind(typeId.toString());
                if (!typeId.isString() || type == interfaceTypes.cend())
                    return "Экземпляр лицевой панели ссылается на отсутствующий тип.";
                if (!bindings.isUndefined() && !bindings.isObject())
                    return "Привязки интерфейса лицевой панели должны быть объектом.";
                const auto connections = bindings.toObject();
                const auto &ports = type.value().ports;
                for (auto binding = connections.constBegin(); binding != connections.constEnd(); ++binding) {
                    const auto port = ports.constFind(binding.key());
                    const auto tag = binding.value().toString();
                    if (port == ports.cend() || !binding.value().isString())
                        return "Привязка лицевой панели указывает на отсутствующий интерфейсный тег.";
                    if (tag.isEmpty())
                        continue;
                    if (isTemplate || !tagIds.contains(tag))
                        return "Привязка лицевой панели указывает на недоступный тег проекта.";
                    const auto actualType = textTags.contains(tag) ? "text" : binaryTags.contains(tag) ? "bool" : "number";
                    if (port.value() != actualType)
                        return "Тип тега проекта должен совпадать с типом интерфейсного тега лицевой панели.";
                }
                if (const auto error = validateInterfaceOverrides(overrides, type.value().properties); !error.isEmpty())
                    return QString("Элемент «%1». %2").arg(item.value("title").toString(), error);
                if (const auto error = validatePropertyBindings(propertyBindings, type.value().properties,
                                                               tagIds, textTags, isTemplate); !error.isEmpty())
                    return QString("Элемент «%1». %2").arg(item.value("title").toString(), error);
            } else if (!typeId.isUndefined() || !bindings.isUndefined() || !overrides.isUndefined() ||
                       !propertyBindings.isUndefined()) {
                return "Тип, привязки и свойства экземпляра задаются у лицевых панелей.";
            }

            const auto tagValue = item.value("tag");
            const auto tag = tagValue.toString();
            if (isFaceplate && ((!tagValue.isUndefined() && !tagValue.isString()) ||
                (!tag.isEmpty() && !layoutTags.contains(tag))))
                return QString("В типе «%1» используйте теги его интерфейса.").arg(name);
            if (!tag.isEmpty() && !layoutTags.contains(tag))
                return QString("В макете «%1» есть ссылка на отсутствующий тег.").arg(name);
            const bool binaryControl = kind == "switch" || kind == "valve";
            if (!tag.isEmpty() && binaryControl && !layoutBinaryTags.contains(tag))
                return "Выберите логический тег для тумблера или клапана.";
            const bool analogControl = analogControls.contains(kind);
            const bool writeControl = binaryControl || analogControl || kind == "selector" || kind == "command" || kind == "hitArea";
            if (!tag.isEmpty() && (analogControl || kind == "selector") &&
                (layoutTextTags.contains(tag) || layoutBinaryTags.contains(tag)))
                return "Выберите числовой тег для регулятора или переключателя режимов.";
            const auto confirmWrite = item.value("confirmWrite");
            if (!confirmWrite.isUndefined() && (!confirmWrite.isBool() || !writeControl))
                return "Неверный формат прежней настройки подтверждения записи.";
            const auto actions = item.value("actions");
            if (!actions.isUndefined()) {
                if (!writeControl)
                    return "Действия по событиям задаются для управляющих виджетов.";
                if (const auto error = validateActions(actions, layoutTags, layoutTextTags, layoutReadableTags, isTemplate, analogControl);
                    !error.isEmpty())
                    return QString("Элемент «%1». %2").arg(item.value("title").toString(), error);
            }
            if (const auto error = validateAppearance(item.value("appearance"), layoutTags, layoutTextTags, isTemplate); !error.isEmpty())
                return QString("Элемент «%1». %2").arg(item.value("title").toString(), error);
            if (const auto error = validateAnimation(item.value("animation"), layoutTags, layoutTextTags, isTemplate); !error.isEmpty())
                return QString("Элемент «%1». %2").arg(item.value("title").toString(), error);
            const auto trend = item.value("trend");
            if (!trend.isUndefined()) {
                const auto options = trend.toObject();
                const auto window = options.value("seconds");
                const auto axis = options.value("axis").toString();
                const auto traces = options.value("series");
                const auto timeLabels = options.value("timeLabels");
                const auto legendValues = options.value("legendValues");
                if (kind != "trend" || !finite(window, 1, 31536000) || window.toDouble() != window.toInt()
                    || (axis != "shared" && axis != "dual" && axis != "individual") || !traces.isArray()
                    || traces.toArray().size() > 32 || !options.value("legend").isBool() || !options.value("toolbar").isBool()
                    || (!legendValues.isUndefined() && !legendValues.isBool())
                    || (!timeLabels.isUndefined() && timeLabels != "auto" && timeLabels != "relative" && timeLabels != "clock"))
                    return "Проверьте параметры графика, период и число рядов.";
                QSet<QString> plotted;
                for (const auto &entry : traces.toArray()) {
                    const auto row = entry.toObject();
                    const auto id = row.value("tag").toString();
                    const auto side = row.value("side").toString();
                    const auto style = row.value("style").toString();
                    if (!row.value("tag").isString() || (!id.isEmpty() && (isTemplate || !layoutTags.contains(id)
                          || layoutTextTags.contains(id) || plotted.contains(id))) ||
                        (side != "left" && side != "right") ||
                        (style != "auto" && style != "line" && style != "step" && style != "points") ||
                        !validColor(row.value("color")))
                        return "Ряд графика должен иметь цвет, способ отображения и уникальный числовой тег.";
                    if (!row.value("min").isUndefined() || !row.value("max").isUndefined()) {
                        if (!finite(row.value("min"), -1e100, 1e100) || !finite(row.value("max"), -1e100, 1e100)
                            || row.value("min").toDouble() >= row.value("max").toDouble())
                            return "Укажите обе границы шкалы ряда. Максимум должен быть больше минимума.";
                    }
                    const auto divisions = row.value("divisions");
                    if (!divisions.isUndefined() && (!finite(divisions, 2, 12) || divisions.toDouble() != divisions.toInt()))
                        return "Укажите от 2 до 12 делений шкалы.";
                    plotted.insert(id);
                }
            }
            if (ranged.contains(kind) &&
                (!finite(item.value("min"), -1e100, 1e100) || !finite(item.value("max"), -1e100, 1e100)
                 || item.value("min").toDouble() >= item.value("max").toDouble()))
                return "Минимум шкалы должен быть меньше максимума.";
            const auto step = item.value("step");
            if (!step.isUndefined() && (!analogControls.contains(kind)
                || !finite(step, 0, item.value("max").toDouble() - item.value("min").toDouble()) || step.toDouble() <= 0))
                return "Шаг регулятора должен быть больше нуля и не превышать диапазон.";
            const auto feedback = item.value("feedback");
            if (!feedback.isUndefined() && (!binaryControl || !feedback.isString() ||
                (!feedback.toString().isEmpty() && (isTemplate || !layoutBinaryTags.contains(feedback.toString())))))
                return "Проверьте тег обратной связи элемента.";
            const auto vertical = item.value("vertical");
            if (!vertical.isUndefined() && (kind != "pipe" || !vertical.isBool()))
                return "Проверьте направление трубопровода.";
            const auto positions = item.value("positions");
            if (!positions.isUndefined()) {
                const auto rows = positions.toArray();
                if (kind != "selector" || rows.size() < 2 || rows.size() > 6)
                    return "Переключателю режимов нужно от 2 до 6 положений.";
                QSet<double> values;
                for (const auto &entry : rows) {
                    const auto position = entry.toObject();
                    const auto label = position.value("label").toString().trimmed();
                    const auto value = position.value("value");
                    if (label.isEmpty() || label.size() > 40 || !finite(value, -1e100, 1e100)
                        || values.contains(value.toDouble()))
                        return "Укажите подписи до 40 символов и разные значения положений.";
                    values.insert(value.toDouble());
                }
            }
            const auto decimals = item.value("decimals");
            if (!decimals.isUndefined() && (!finite(decimals, 0, 6) || decimals.toDouble() != decimals.toInt()))
                return "Точность прибора задаётся числом знаков от 0 до 6.";
            const auto accent = item.value("accent");
            if (!accent.isUndefined() && !validColor(accent))
                return "Цвет прибора задаётся в формате #RRGGBB.";
            if ((kind == "command" || (kind == "hitArea" && item.contains("command"))) &&
                !finite(item.value("command"), -1e100, 1e100))
                return "У кнопки записи должно быть задано числовое значение.";
            if (kind == "link" && !item.value("screen").toString().isEmpty()
                && !screenIds.contains(item.value("screen").toString()))
                return "Переход указывает на отсутствующий экран.";
            if (isTemplate && (!tag.isEmpty() || !item.value("screen").toString().isEmpty()))
                return "Привязки к тегам и переходам задаются после вставки шаблона.";
            const auto group = item.value("group");
            if (!group.isUndefined() && QUuid::fromString(group.toString()).isNull())
                return "Неверный идентификатор группы элементов.";
            if (kind == "image" && !validImagePath(item.value("image").toString()))
                return "Путь к рисунку должен вести в папку images проекта.";
            for (const auto *key : {"locked", "visible"}) {
                if (item.contains(key) && !item.value(key).isBool())
                    return "Видимость и блокировка слоя должны быть логическими значениями.";
            }
            const auto shape = item.value("shape");
            if (!shape.isUndefined() && (kind != "shape" ||
                (shape != "rectangle" && shape != "ellipse" && shape != "line")))
                return "Выберите прямоугольник, эллипс или линию.";
        }
    }
    return {};
}

bool Project::load(const QString &path, QJsonObject &project, QString &error)
{
    error.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        error = parseError.error == QJsonParseError::NoError
            ? QString("Ожидается объект JSON.") : parseError.errorString();
        return false;
    }
    error = validate(document.object());
    if (!error.isEmpty())
        return false;
    project = document.object();
    return true;
}

bool Project::save(const QString &path, const QJsonObject &project, QString &error)
{
    error = validate(project);
    if (!error.isEmpty())
        return false;
    QSaveFile file(path);
    const auto data = QJsonDocument(project).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}
