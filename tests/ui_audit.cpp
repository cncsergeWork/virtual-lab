#include "project.h"
#include "studio.h"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSValue>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <functional>

namespace {
QQuickItem *visibleItem(QQuickItem *parent, const QString &name)
{
    if (!parent || !parent->isVisible())
        return nullptr;
    if (parent->objectName() == name)
        return parent;
    for (auto *child : parent->childItems()) {
        if (auto *found = visibleItem(child, name))
            return found;
    }
    return nullptr;
}

class Audit {
public:
    Audit(QQuickWindow *window, Studio &studio) : window_(window), studio_(studio) {}
    int run();

private:
    QQuickItem *item(const QString &name) const { return visibleItem(window_->contentItem(), name); }
    QVariant value(const QString &name, const char *property) const;
    bool set(const QString &name, const char *property, const QVariant &value);
    bool click(const QString &name);
    bool select(const QString &name, int index);
    bool pick(const QString &name, const QString &path, const std::function<bool()> &open);
    bool menu(const QString &action) { return click("projectMenuButton") && click(action); }
    bool rename(const QString &name) { return menu("projectRename") && set("screenName", "text", name) && click("saveName"); }
    QString file(const QString &name) const { return temp_.filePath(name); }
    bool projects();
    bool editor();
    bool forms();
    bool sources();
    bool archive();

    QQuickWindow *window_;
    Studio &studio_;
    QTemporaryDir temp_;
    QSet<QString> buttons_;
    int clicks_ = 0;
};

QVariant Audit::value(const QString &name, const char *property) const
{
    const auto *control = item(name);
    return control ? control->property(property) : QVariant{};
}

bool Audit::set(const QString &name, const char *property, const QVariant &value)
{
    auto *control = item(name);
    if (control && control->setProperty(property, value))
        return true;
    qWarning().noquote() << "AUDIT поле" << name << property;
    return false;
}

bool Audit::click(const QString &name)
{
    QQuickItem *control = nullptr;
    if (!QTest::qWaitFor([&] { control = item(name); return control && control->isEnabled(); }, 1100)) {
        qWarning().noquote() << "AUDIT кнопка недоступна" << name;
        return false;
    }
    for (auto *parent = control->parentItem(); parent; parent = parent->parentItem()) {
        const auto y = parent->property("contentY");
        if (!y.isValid() || parent->height() <= 0)
            continue;
        const auto center = control->mapToItem(parent, {control->width() / 2, control->height() / 2});
        const auto margin = std::min(20.0, parent->height() / 4);
        if (center.y() < margin || center.y() > parent->height() - margin) {
            const auto maxScroll = std::max(0.0, parent->property("contentHeight").toDouble() - parent->height());
            parent->setProperty("contentY", std::clamp(y.toDouble() + center.y() - parent->height() / 2, 0.0, maxScroll));
            QTest::qWait(25);
        }
    }
    const auto point = control->mapToScene({control->width() / 2, control->height() / 2}).toPoint();
    if (control->width() <= 0 || control->height() <= 0 || !QRect(QPoint{}, window_->size()).contains(point)) {
        qWarning().noquote() << "AUDIT кнопка вне окна" << name << point;
        return false;
    }
    QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, point);
    buttons_.insert(name);
    ++clicks_;
    QTest::qWait(35);
    return true;
}

bool Audit::select(const QString &name, int index)
{
    auto *box = item(name);
    return box && box->isEnabled() && index >= 0 && index < box->property("count").toInt()
        && box->setProperty("currentIndex", index) && QMetaObject::invokeMethod(box, "activated", Q_ARG(int, index));
}

bool Audit::pick(const QString &name, const QString &path, const std::function<bool()> &open)
{
    auto *dialog = window_->findChild<QObject *>(name);
    const auto url = QUrl::fromLocalFile(path);
    if (!dialog || !dialog->setProperty("currentFolder", QUrl::fromLocalFile(QFileInfo(path).absolutePath())) ||
        !dialog->setProperty("selectedFile", url) || !open() ||
        !QTest::qWaitFor([&] { return dialog->property("visible").toBool(); }, 1500) ||
        dialog->property("selectedFile").toUrl() != url || !QMetaObject::invokeMethod(dialog, "accept") ||
        !QTest::qWaitFor([&] { return !dialog->property("visible").toBool(); }, 1500)) {
        qWarning().noquote() << "AUDIT выбор файла" << name << path;
        return false;
    }
    return true;
}

bool Audit::projects()
{
    if (!menu("projectNew") || studio_.simulator() || !studio_.path().isEmpty() ||
        !rename("Проверка проекта") || !studio_.modified() ||
        !menu("projectOpen") || !click("unsavedCancel") || studio_.name() != "Проверка проекта" ||
        !menu("projectRename") || !set("screenName", "text", "Отменённое имя") || !click("cancelName") ||
        studio_.name() != "Проверка проекта")
        return false;
    const auto original = file("original.scada.json");
    const auto copy = file("copy.scada.json");
    if (!pick("projectSavePicker", original, [&] { return click("saveButton"); }) ||
        !QFile::exists(original) || studio_.modified() ||
        !rename("Исходный проект") || !menu("projectSave") || studio_.modified() ||
        !pick("projectSavePicker", copy, [&] { return menu("projectSaveAs"); }) ||
        !QFile::exists(copy) || studio_.path() != copy ||
        !rename("Несохранённое имя") || !menu("projectNew") || !click("unsavedDiscard") ||
        !studio_.path().isEmpty() || !pick("projectOpenPicker", original, [&] { return menu("projectOpen"); }) ||
        studio_.name() != "Исходный проект" || studio_.path() != original)
        return false;
    if (!rename("Сохранено при выходе") || !menu("projectNew") || !click("unsavedSave") ||
        !studio_.path().isEmpty() || !pick("projectOpenPicker", original, [&] { return menu("projectOpen"); }) ||
        studio_.name() != "Сохранено при выходе")
        return false;
    return menu("projectDemo") && studio_.simulator();
}

bool Audit::editor()
{
    if (!click("tab0") || !click("addScreen") || !set("screenName", "text", "Проверочный экран") || !click("saveName"))
        return false;
    const auto testScreen = studio_.screenId();
    const auto count = studio_.screens().size();
    if (!click("screenMenu") || !click("deleteScreen") || !click("cancelAction") || studio_.screens().size() != count ||
        !click("editorSnap") || value("editorSnap", "checked").toBool() ||
        !click("editorSnap") || !value("editorSnap", "checked").toBool())
        return false;
    const auto factor = value("screenCanvas", "factor").toDouble();
    if (!click("editorZoomIn") || value("screenCanvas", "factor").toDouble() <= factor ||
        !click("editorZoomOut") || !click("editorFit") || value("dashboard", "fit").toBool() != true)
        return false;

    const auto tools = value("dashboard", "tools").value<QJSValue>().toVariant().toList();
    if (tools.size() != 26)
        return false;
    for (const auto &tool : tools) {
        const auto kind = tool.toMap().value("kind").toString();
        if (!click("palette_" + kind) || studio_.widget().value("kind").toString() != kind) {
            qWarning().noquote() << "AUDIT палитра: добавление" << kind << studio_.widget().value("kind");
            return false;
        }
        if (!click("widgetRemove") || !studio_.items().isEmpty()) {
            qWarning().noquote() << "AUDIT палитра: удаление" << kind << studio_.items().size() << studio_.selectedId();
            return false;
        }
    }
    if (!click("palette_label") || !click("widgetDuplicate") || studio_.items().size() != 2)
        return false;
    const auto first = studio_.items().first().toMap().value("id").toString();
    const auto second = studio_.selectedId();
    if (first == second || !set("widgetX", "text", "90") || !set("widgetY", "text", "120") ||
        !click("widgetApplyPosition") || studio_.widget().value("x").toInt() != 90 ||
        !click("widgetSendBack") || studio_.items().first().toMap().value("id") != second ||
        !click("widgetBringFront") || studio_.items().last().toMap().value("id") != second)
        return false;
    studio_.selectWidget(first);
    studio_.selectWidget(second, true);
    QTest::qWait(35);
    if (!click("widgetAlignLeft") || !click("widgetAlignTop") ||
        studio_.items().first().toMap().value("x") != studio_.items().last().toMap().value("x") ||
        studio_.items().first().toMap().value("y") != studio_.items().last().toMap().value("y") ||
        !click("groupWidgets") || studio_.widget().value("group").toString().isEmpty() ||
        !click("groupWidgets") || studio_.widget().contains("group"))
        return false;
    if (!click("createTemplate") || !set("templateName", "text", "Проверка шаблона") ||
        !click("templateSave") || studio_.templates().size() != 1 ||
        !click("insertTemplate") || studio_.items().size() != 4 ||
        !click("editorUndo") || studio_.items().size() != 2 ||
        !click("editorRedo") || studio_.items().size() != 4 ||
        !click("removeTemplate") || !studio_.templates().isEmpty())
        return false;
    if (studio_.screenId() != testScreen || !click("screenMenu") || !click("deleteScreen") || !click("confirmAction") ||
        studio_.screens().size() != count - 1 || studio_.screenId() == testScreen)
        return false;
    if (!click("faceplateLibraryNew") || !set("faceplateLibraryName", "text", "Проверка слоёв") ||
        !click("faceplateLibraryNameApply") || !click("faceplateSize") || !set("typeWidth", "text", "620") ||
        !set("typeHeight", "text", "340") || !click("typeSizeApply") ||
        studio_.canvasSettings().value("width").toInt() != 620 ||
        studio_.canvasSettings().value("height").toInt() != 340 ||
        !click("palette_shape") || !click("palette_hitArea") || studio_.widget().value("kind") != "hitArea")
        return false;
    const auto commandLayer = studio_.selectedId();
    if (!click("widgetLayerDown") || studio_.items().first().toMap().value("id") != commandLayer ||
        !click("widgetLayerUp") || studio_.items().last().toMap().value("id") != commandLayer ||
        !click("faceplatePorts") || !click("faceplatePortAdd") || !click("faceplatePortRemove_0") ||
        !click("faceplatePortsApply") || !studio_.editorTags().isEmpty() ||
        !click("faceplatePropertyInterface") || !click("propertyInterfaceAdd") || !click("propertyInterfaceRemove_0") ||
        !click("propertyInterfaceApply"))
        return false;
    return click("faceplateBack");
}

bool Audit::forms()
{
    studio_.newProject(true);
    const auto selectWidget = [&](const QString &kind, const QString &title = {}) {
        for (const auto &entry : studio_.items()) {
            const auto widget = entry.toMap();
            if (widget.value("kind") == kind && (title.isEmpty() || widget.value("title") == title)) {
                studio_.selectWidget(widget.value("id").toString());
                QTest::qWait(25);
                return true;
            }
        }
        return false;
    };
    if (!click("tab0") || !selectWidget("value", "Давление") || !click("editAppearance") ||
        !click("appearanceAdd") || !click("appearanceRuleRemove_2") ||
        !click("appearanceRuleDown_0") || !click("appearanceRuleUp_1") ||
        !click("appearanceRuleRemove_1") || !click("appearanceBackground_pick"))
        return false;
    auto *colors = window_->findChild<QObject *>("appearanceColorPicker");
    if (!colors || !QTest::qWaitFor([&] { return colors->property("visible").toBool(); }, 1500) ||
        !colors->setProperty("selectedColor", QColor("#296f84")) || !QMetaObject::invokeMethod(colors, "accept") ||
        !click("appearanceSave"))
        return false;
    const auto appearance = studio_.widget().value("appearance").toMap();
    if (appearance.value("background") != "#296f84" || appearance.value("rules").toList().size() != 1 ||
        !selectWidget("trend") || !click("editTrend") || !click("trendAddSeries") ||
        !click("trendRemoveSeries_3") || !click("trendRemoveSeries_2") ||
        !select("trendAxisMode", 1) || !click("trendLegendValues") || !click("trendSave") ||
        studio_.widget().value("trend").toMap().value("series").toList().size() != 2)
        return false;

    const auto controls = studio_.screens().at(1).toMap().value("id").toString();
    if (!click("screenTab_" + controls) || !selectWidget("selector") || !click("editSelectorPositions") ||
        !click("selectorAdd") || !click("selectorRemove_3") || !click("selectorRemove_2") ||
        !click("selectorApply") || studio_.widget().value("positions").toList().size() != 2 ||
        !selectWidget("command", "Пуск насоса") || !click("widgetActions") ||
        !click("widgetActionAdd") || !click("widgetActionAdd") ||
        !select("widgetActionEvent_0", 0) || !click("widgetActionDown_0") ||
        value("widgetActionEvent_1", "currentValue") != "press" ||
        !click("widgetActionUp_1") || !click("widgetActionRemove_1") || !click("widgetActionApply"))
        return false;
    const auto actions = studio_.widget().value("actions").toList();
    if (actions.size() != 1 || actions.first().toMap().value("event") != "press")
        return false;

    const auto input = file("picture.png");
    if (!QFile::copy(Project::demoImage, input) || !click("palette_image") ||
        !pick("widgetImagePicker", input, [&] { return click("chooseWidgetImage"); }))
        return false;
    const auto image = studio_.widget().value("image").toString();
    if (!image.startsWith("images/") || !QFile::exists(studio_.imageUrl(image).toLocalFile()) ||
        !studio_.save(QUrl::fromLocalFile(file("screen-background.scada.json"))))
        return false;
    const auto screenProperties = [&] { return click("screenMenu") && click("openScreenProperties"); };
    const auto background = [&] { return studio_.canvasSettings().value("backgroundImage").toString(); };
    return screenProperties() &&
        pick("screenImagePicker", input, [&] { return click("screenChooseImage"); }) &&
        click("screenPropertiesSave") && !background().isEmpty() &&
        QFile::exists(studio_.imageUrl(background()).toLocalFile()) &&
        screenProperties() && click("screenRemoveImage") && click("screenPropertiesSave") && background().isEmpty();
}

bool Audit::sources()
{
    studio_.newProject(false);
    if (!click("tab2") || !click("addSource") || !set("sourceName", "text", "Отмена нового источника") ||
        !click("sourceCancel") || !studio_.sources().isEmpty() ||
        !click("addSource") || !set("sourceName", "text", "Проверочный Modbus") ||
        !set("sourceHost", "text", "127.0.0.1") || !click("sourceAllowWrite") || !click("sourceSave") ||
        studio_.sources().size() != 1)
        return false;
    const auto source = studio_.sources().first().toMap().value("id").toString();
    if (!click("sourceEdit_" + source) || !set("sourceName", "text", "Отредактированный Modbus") ||
        !click("sourceSave") || studio_.sources().first().toMap().value("name") != "Отредактированный Modbus" ||
        !click("tab3") || !click("addTag") || !set("tagName", "text", "Первый проверочный тег") ||
        !select("tagModbusTable", 0) || !click("tagAllowWrite") || !click("tagSave") || studio_.tags().size() != 1)
        return false;
    const auto tag = studio_.tags().first().toMap().value("id").toString();
    if (!studio_.tags().first().toMap().value("writable").toBool() ||
        !click("tagEdit_" + tag) || !set("tagName", "text", "Отмена правки") || !click("tagCancel") ||
        studio_.tags().first().toMap().value("name") != "Первый проверочный тег" ||
        !click("tagEdit_" + tag) || !set("tagName", "text", "Отредактированный тег") || !click("tagSave") ||
        !set("tagSearch", "text", "несуществующий") || value("tagList", "count").toInt() != 0 ||
        !set("tagSearch", "text", "редакт") || value("tagList", "count").toInt() != 1 ||
        !click("tagRemove_" + tag) || !click("cancelAction") || studio_.tags().size() != 1 ||
        !click("tagRemove_" + tag) || !click("confirmAction") || !studio_.tags().isEmpty())
        return false;
    if (!click("tab2") || !click("sourceRemove_" + source) || !click("cancelAction") || studio_.sources().size() != 1 ||
        !click("sourceRemove_" + source) || !click("confirmAction") || !studio_.sources().isEmpty() ||
        !click("addSource") || !select("sourceDriver", 1) || !select("jsonTransport", 1) ||
        !set("sourceName", "text", "Проверочный USB COM") || !set("sourceSerialPort", "text", "COM7") ||
        !click("jsonSampleTags") || !click("sourceSave") || studio_.sources().size() != 1 || studio_.tags().size() < 25)
        return false;
    const auto catalogSize = studio_.tags().size();
    if (!click("tab3") || !click("addTag") || !set("tagName", "text", "Проверочный JSON тег") ||
        !set("tagJsonPath", "text", "values.audit_only") || !click("tagSave") ||
        studio_.tags().size() != catalogSize + 1)
        return false;
    const auto customTag = studio_.tags().last().toMap().value("id").toString();
    if (!set("tagSearch", "text", "Проверочный JSON тег") || !click("tagRemove_" + customTag) ||
        !click("confirmAction") || studio_.tags().size() != catalogSize || !click("tab2"))
        return false;
    const auto catalog = file("tags.json");
    if (!pick("jsonExportPicker", catalog, [&] { return click("engineCatalog") && click("jsonExportTags"); }) ||
        !QFile::exists(catalog) || !studio_.removeTag(studio_.tags().first().toMap().value("id").toString()) ||
        !pick("jsonImportPicker", catalog, [&] { return click("engineCatalog") && click("jsonImportTags"); }) ||
        studio_.tags().size() != catalogSize)
        return false;
    const auto jsonSource = studio_.sources().first().toMap().value("id").toString();
    return click("sourceEdit_" + jsonSource) && select("jsonTransport", 0) &&
        set("engineUrl", "text", "ws://127.0.0.1:15115/ws") && click("sourceSave") &&
        value("sourceExchange", "text") == "JSON · WebSocket";
}

bool Audit::archive()
{
    studio_.newProject(true);
    auto level = studio_.tags().first().toMap();
    const auto levelId = level.value("id").toString();
    level.insert("alarm", QVariantMap{{"direction", "high"}, {"limit", 0}, {"hysteresis", 0}});
    if (!studio_.setTag(level) || !click("runButton") ||
        !QTest::qWaitFor([&] { return !studio_.alarms().isEmpty(); }, 4000) ||
        !click("tab5"))
        return false;
    const auto acknowledge = "eventAcknowledge_" + levelId;
    const QPointer<QQuickItem> button(item(acknowledge));
    if (!button || !button->isEnabled())
        return false;
    const auto coordinate = button->mapToScene({button->width() / 2, button->height() / 2}).toPoint();
    const auto readingTime = [&] {
        return studio_.readings().value(levelId).toMap().value("time").toLongLong();
    };
    const auto beforePress = readingTime();
    QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, coordinate);
    const bool polledWhileHeld = QTest::qWaitFor([&] { return readingTime() > beforePress; }, 1800);
    const bool sameButton = button && button == item(acknowledge);
    QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, coordinate);
    if (!polledWhileHeld || !sameButton || !QTest::qWaitFor([&] {
        const auto alarms = studio_.alarms();
        return !alarms.isEmpty() && alarms.first().toMap().value("ack").toBool();
    }, 1500)) {
        qWarning() << "AUDIT квитирование" << polledWhileHeld << sameButton << studio_.alarms();
        return false;
    }
    buttons_.insert(acknowledge);
    ++clicks_;
    const auto now = QDateTime::currentDateTime();
    const QString format = "yyyy-MM-dd HH:mm:ss";
    const auto trendPeriod = [&] { return click("dashboardTrendPeriod") && click("dashboardTrendInterval"); };
    if (!click("tab1") || !trendPeriod() || !set("trendLastSeconds", "text", "45") ||
        !click("trendApplyLivePeriod") || value("dashboardTrend", "span").toInt() != 45000 ||
        !trendPeriod() || !set("trendDateFrom", "text", now.addSecs(-60).toString(format)) ||
        !set("trendDateTo", "text", now.addSecs(30).toString(format)) || !click("trendApplyInterval") ||
        value("dashboardTrend", "following").toBool() || value("dashboardTrend", "span").toInt() != 90000 ||
        !click("dashboardTrendPeriod") || !click("dashboardTrendLive") ||
        !value("dashboardTrend", "following").toBool() || !click("tab4"))
        return false;
    if (!set("archiveSearch", "text", "Уровень бака") || !click("archiveClearChannels") ||
        value("archiveExport", "enabled").toBool() || !click("archiveAllChannels") ||
        !select("archivePeriod", 1) || !value("archiveExport", "enabled").toBool())
        return false;
    const auto period = value("archivePage", "span").toDouble();
    const auto start = value("archivePage", "startTime").toDouble();
    if (!click("archiveEarlier") || value("archivePage", "startTime").toDouble() >= start ||
        !click("archiveLater") || value("archivePage", "startTime").toDouble() < start - 10 ||
        !click("archiveZoomIn") || value("archivePage", "span").toDouble() >= period ||
        !click("archiveZoomOut") || value("archivePage", "span").toDouble() <= period * 0.9)
        return false;
    if (!click("archiveInterval") || !set("archiveDateFrom", "text", "не дата") || !click("archiveDateApply") ||
        !item("archiveInvalidDates") || !set("archiveDateFrom", "text", now.addSecs(-60).toString(format)) ||
        !set("archiveDateTo", "text", now.addSecs(30).toString(format)) || !click("archiveDateApply") ||
        item("archiveDateApply") || value("archivePage", "span").toInt() != 90000)
        return false;
    auto *plot = item("archivePlot_" + levelId);
    if (!plot)
        return false;
    QTest::mouseMove(window_, plot->mapToScene({plot->width() * 0.5, plot->height() * 0.5}).toPoint());
    if (!click("archiveMarkA") || value("archivePage", "markA").toDouble() <= 0 ||
        !click("archiveMarkB") || value("archivePage", "markB").toDouble() <= 0 ||
        !click("archiveClearMarks") || value("archivePage", "markA").toDouble() != 0 ||
        value("archivePage", "markB").toDouble() != 0)
        return false;
    const auto csv = file("archive.csv");
    if (!pick("archiveCsvPicker", csv, [&] { return click("archiveExport") && click("archiveExportApply"); }))
        return false;
    QFile table(csv);
    if (!table.open(QIODevice::ReadOnly) || table.readAll().count('\n') < 2 ||
        !click("archiveLive") || !value("archivePage", "follow").toBool() ||
        !click("runButton") || studio_.running())
        return false;
    return true;
}

int Audit::run()
{
    if (!temp_.isValid() || !QTest::qWaitForWindowExposed(window_, 3000))
        return 2;
    struct Cleanup {
        Studio &studio;
        ~Cleanup() { studio.newProject(false); }
    } cleanup{studio_};
    window_->resize(1460, 1040);
    QTest::qWait(50);
    const std::pair<const char *, bool (Audit::*)()> checks[] = {
        {"project files, menus and unsaved changes", &Audit::projects},
        {"editor buttons, palette, template and layer order", &Audit::editor},
        {"appearance, color picker, trend, selector, action tables and image selection", &Audit::forms},
        {"source, tag, USB COM settings and JSON file import/export", &Audit::sources},
        {"events, archive controls, valid and invalid intervals, CSV", &Audit::archive}
    };
    for (const auto &[description, check] : checks) {
        qInfo().noquote() << "AUDIT start" << description;
        if (!(this->*check)()) {
            qCritical().noquote() << "AUDIT failed" << description;
            const auto directory = qEnvironmentVariable("SCADA_UI_CAPTURE");
            if (!directory.isEmpty())
                window_->grabWindow().save(QDir(directory).filePath("ui-audit-failure.png"));
            return 2;
        }
        qInfo().noquote() << "AUDIT passed" << description;
    }
    qInfo() << "AUDIT mouse clicks" << clicks_ << "distinct controls" << buttons_.size();
    return 0;
}
}

int runUiAudit(QObject *root, Studio &studio)
{
    auto *window = qobject_cast<QQuickWindow *>(root);
    return window ? Audit(window, studio).run() : 2;
}
