#include "studio.h"

#include <QColor>
#include <QJSValue>
#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QtTest>
#include <algorithm>
#include <functional>
#include <memory>

namespace {
QQuickItem *item(QQuickItem *parent, const QString &name, const QString &label = {}, const char *property = "text")
{
    if (!parent || !parent->isVisible())
        return nullptr;
    if (parent->objectName() == name && (label.isEmpty() || parent->property(property).toString() == label))
        return parent;
    for (auto *child : parent->childItems()) {
        if (auto *found = item(child, name, label, property))
            return found;
    }
    return nullptr;
}

QPoint point(QQuickItem *item)
{
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}

bool waitFor(const std::function<bool()> &condition, int ms = 2200)
{
    return QTest::qWaitFor(condition, ms);
}
}

int runUiFlow(QObject *root, Studio &studio)
{
    auto *window = qobject_cast<QQuickWindow *>(root);
    const auto fail = [](const QString &step) {
        qCritical().noquote() << "Проверка GUI не прошла." << step;
        return 2;
    };
    if (!window)
        return fail("Открытие окна");
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return fail("Создание временного проекта");
    struct CloseProject {
        Studio &studio;
        ~CloseProject() { studio.newProject(false); }
    } closeProject{studio};
    auto *scene = window->contentItem();
    const auto click = [&](const QString &name, const QString &label = {}) {
        auto *control = item(scene, name, label);
        if (!control || !control->isEnabled() || control->width() <= 0 || control->height() <= 0)
            return false;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point(control));
        QTest::qWait(50);
        return true;
    };
    const auto drag = [&](const QPoint &from, const QPoint &to) {
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        for (int i = 1; i <= 6; ++i)
            QTest::mouseMove(window, from + (to - from) * i / 6, 15);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
        QTest::qWait(70);
    };
    const auto capture = [&](const QString &name, QQuickItem *region = nullptr) {
        const auto path = qEnvironmentVariable("SCADA_UI_CAPTURE");
        if (!path.isEmpty()) {
            if (auto *toast = window->findChild<QObject *>("studioToast"))
                QMetaObject::invokeMethod(toast, "close");
            QTest::qWait(30);
            auto snapshot = window->grabWindow();
            if (region && !snapshot.isNull()) {
                const auto rect = region->mapRectToScene(QRectF(QPointF{}, region->size()));
                const auto ratio = snapshot.devicePixelRatio();
                snapshot = snapshot.copy(QRectF(rect.topLeft() * ratio, rect.size() * ratio).toAlignedRect());
            }
            snapshot.save(QDir(path).filePath(name));
        }
    };
    const auto map = [&](int tag) { return studio.tags().at(tag).toMap(); };
    const auto reading = [&](int tag) { return studio.readings().value(map(tag).value("id").toString()).toMap(); };
    if (!QTest::qWaitForWindowExposed(window, 3000))
        return fail("Показ окна");
    QTest::qWait(180);
    capture("editor.png");

    auto *palettePanel = item(scene, "palettePanel");
    auto *paletteResize = item(scene, "paletteResize");
    auto *inspectorPanel = item(scene, "inspectorPanel");
    auto *inspectorResize = item(scene, "inspectorResize");
    auto *boardView = item(scene, "dashboardView");
    if (!palettePanel || !paletteResize || !boardView || palettePanel->width() != 166)
        return fail("Исходная ширина и ручка левой панели");
    if (!inspectorPanel || !inspectorResize || inspectorPanel->width() != 230)
        return fail("Исходная ширина и ручка панели свойств");
    const auto initialViewWidth = boardView->width();
    const auto edge = point(paletteResize);
    const auto wider = edge + QPoint(100, 0);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, edge);
    QTest::mouseMove(window, wider, 50);
    const bool resizedWhileDragging = waitFor([&] {
        return palettePanel->width() == 266 && boardView->width() == initialViewWidth - 100;
    });
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, wider);
    if (!resizedWhileDragging || inspectorPanel->width() != 230 || studio.modified())
        return fail("Изменение ширины левой панели и холста при перетаскивании");
    const auto inspectorEdge = point(inspectorResize);
    const auto widerInspector = inspectorEdge - QPoint(120, 0);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, inspectorEdge);
    QTest::mouseMove(window, widerInspector, 50);
    const bool inspectorResized = waitFor([&] {
        return inspectorPanel->width() == 350 && boardView->width() == initialViewWidth - 220;
    });
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, widerInspector);
    if (!inspectorResized || palettePanel->width() != 266 || studio.modified())
        return fail("Изменение ширины панели свойств и холста при перетаскивании");

    const auto initialWindowSize = window->size();
    window->resize(window->minimumWidth(), initialWindowSize.height());
    if (!waitFor([&] {
        return window->width() == window->minimumWidth() && boardView->width() >= 358 &&
            palettePanel->width() < 266;
    }))
        return fail("Узкое окно оставляет место для холста при широких панелях");
    QQmlComponent reopenedEditor(qmlEngine(root), QUrl("qrc:/qml/Dashboard.qml"));
    const auto editorRestoresPanels = [&](bool paletteCollapsed, bool inspectorCollapsed) {
        QSettings settings;
        settings.sync();
        const std::unique_ptr<QObject> newDashboard(reopenedEditor.createWithInitialProperties(
            {{"api", QVariant::fromValue(&studio)}, {"editing", true}}));
        return newDashboard && settings.value("editor/paletteWidth").toInt() == 266 &&
            settings.value("editor/inspectorWidth").toInt() == 350 &&
            settings.value("editor/paletteCollapsed", false).toBool() == paletteCollapsed &&
            settings.value("editor/inspectorCollapsed", false).toBool() == inspectorCollapsed &&
            newDashboard->property("paletteWidth").toInt() == 266 &&
            newDashboard->property("inspectorWidth").toInt() == 350 &&
            newDashboard->property("paletteCollapsed").toBool() == paletteCollapsed &&
            newDashboard->property("inspectorCollapsed").toBool() == inspectorCollapsed;
    };
    if (!editorRestoresPanels(false, false))
        return fail("Редактор загружает ширину панелей и открывает их по умолчанию");
    window->resize(initialWindowSize);
    if (!waitFor([&] { return palettePanel->width() == 266 && inspectorPanel->width() == 350; }))
        return fail("Расширение окна восстанавливает выбранные размеры панелей");

    const auto expandedViewWidth = boardView->width();
    if (!click("togglePalette") || !waitFor([&] {
        return !palettePanel->isVisible() && inspectorPanel->isVisible() && inspectorPanel->width() == 350 &&
            boardView->width() > expandedViewWidth + 266;
    }))
        return fail("Сворачивание палитры освобождает место для холста");
    if (!editorRestoresPanels(true, false))
        return fail("Редактор восстанавливает свёрнутую палитру и открытую панель свойств");
    const auto viewWithoutPalette = boardView->width();
    if (!click("toggleInspector") || !waitFor([&] {
        return !palettePanel->isVisible() && !inspectorPanel->isVisible() && boardView->width() > viewWithoutPalette + 350;
    }))
        return fail("Обе боковые панели можно свернуть одновременно");
    if (!editorRestoresPanels(true, true))
        return fail("Редактор запоминает обе свёрнутые панели одновременно");
    if (!click("togglePalette") || !waitFor([&] {
        return palettePanel->isVisible() && palettePanel->width() == 266 && !inspectorPanel->isVisible();
    }))
        return fail("Палитра восстанавливает прежнюю ширину независимо от панели свойств");
    if (!editorRestoresPanels(false, true))
        return fail("Редактор восстанавливает открытую палитру и свёрнутую панель свойств");
    if (!click("toggleInspector") || !waitFor([&] {
        return inspectorPanel->isVisible() && inspectorPanel->width() == 350 && palettePanel->width() == 266 &&
            boardView->width() == expandedViewWidth;
    }) || studio.modified())
        return fail("Панель свойств восстанавливает прежнюю ширину без изменения проекта");
    if (!editorRestoresPanels(false, false))
        return fail("Редактор запоминает повторное открытие обеих панелей");

    const auto wideInspectorEdge = point(inspectorResize);
    drag(wideInspectorEdge, wideInspectorEdge + QPoint(140, 0));
    if (inspectorPanel->width() != 210 || palettePanel->width() != 266)
        return fail("Панель свойств можно сузить без изменения левой панели");
    const auto narrowInspectorEdge = point(inspectorResize);
    drag(narrowInspectorEdge, narrowInspectorEdge - QPoint(20, 0));
    if (inspectorPanel->width() != 230)
        return fail("Точное изменение ширины панели свойств");
    const auto wideEdge = point(paletteResize);
    drag(wideEdge, wideEdge - QPoint(100, 0));
    if (palettePanel->width() != 166 || boardView->width() != initialViewWidth)
        return fail("Сужение левой панели возвращает место холсту");

    const auto originalTypes = studio.faceplates().size();
    const auto screenBeforeNewType = studio.screenId();
    const auto itemsBeforeNewType = studio.items();
    if (!click("faceplateLibraryNew") || !waitFor([&] { return item(scene, "faceplateLibraryName"); }))
        return fail("Кнопка «Новый тип…» открывает диалог с названием");
    auto *typeName = item(scene, "faceplateLibraryName");
    if (!waitFor([&] { return typeName->hasActiveFocus(); }) || !typeName->property("text").toString().isEmpty())
        return fail("Поле названия нового типа пустое и получает фокус");
    if (!typeName->setProperty("text", "Тестовая лицевая панель"))
        return fail("Ввод названия нового типа");
    auto *newWidth = item(scene, "newWidgetWidth");
    if (!newWidth || !newWidth->setProperty("text", "620"))
        return fail("Размер холста задаётся при создании виджета");
    QTest::keyClick(window, Qt::Key_Return);
    if (!waitFor([&] { return studio.compositeDepth() == 1 && !item(scene, "faceplateLibraryName"); }) ||
        studio.faceplates().size() != originalTypes + 1 ||
        studio.canvasSettings().value("name") != "Тестовая лицевая панель" ||
        studio.canvasSettings().value("width").toInt() != 620 || !studio.items().isEmpty())
        return fail("Создание по Enter открывает пустой холст именованного типа");
    const auto newTypeId = studio.canvasSettings().value("id").toString();
    if (!click("palette_shape") || studio.widget().value("kind") != "shape")
        return fail("Добавление фигуры из палитры конструктора");
    const auto shapeId = studio.selectedId();
    if (!click("widgetLayerLock") || !studio.widget().value("locked").toBool() ||
        !item(scene, "widgetLayer_" + shapeId))
        return fail("Список слоёв и блокировка положения фигуры");
    if (!click("faceplateBack") || studio.compositeDepth() != 0 || studio.screenId() != screenBeforeNewType ||
        studio.items() != itemsBeforeNewType)
        return fail("Возврат на исходный экран из редактора нового типа");
    const auto clickTypeAction = [&](const QString &action) {
        return click("faceplateLibraryType") && waitFor([&] { return item(scene, action); }) && click(action);
    };
    if (!clickTypeAction("faceplateRename_" + newTypeId) ||
        !waitFor([&] { return item(scene, "faceplateLibraryName"); }))
        return fail("Переименование открывается действием в строке списка типов");
    typeName = item(scene, "faceplateLibraryName");
    if (!waitFor([&] { return typeName->hasActiveFocus(); }) ||
        typeName->property("text") != "Тестовая лицевая панель" ||
        !typeName->setProperty("text", "Переименованная панель"))
        return fail("Диалог показывает текущее название выбранного типа");
    QTest::keyClick(window, Qt::Key_Return);
    if (!waitFor([&] { return !item(scene, "faceplateLibraryName"); }) ||
        studio.faceplates().last().toMap().value("name") != "Переименованная панель" ||
        studio.faceplates().last().toMap().value("id") != newTypeId)
        return fail("Переименование сохраняет тип с прежним идентификатором");
    if (!clickTypeAction("faceplateDelete_" + newTypeId) || !click("faceplateDeleteCancel") ||
        studio.faceplates().size() != originalTypes + 1)
        return fail("Отмена удаления оставляет тип в библиотеке");
    if (!clickTypeAction("faceplateDelete_" + newTypeId) || !click("faceplateDeleteConfirm") ||
        !waitFor([&] { return studio.faceplates().size() == originalTypes; }) ||
        studio.items() != itemsBeforeNewType)
        return fail("Подтверждение удаляет свободный тип из списка");

    const auto pumpType = studio.faceplates().last().toMap();
    const auto pumpId = pumpType.value("id").toString();
    const auto pumpScreen = studio.screens().last().toMap().value("id").toString();
    const auto pumpLayers = pumpType.value("items").toList();
    const auto pumpPorts = pumpType.value("ports").toList();
    const auto layer = [&](int index) {
        return "previewLayer_" + pumpLayers.at(index).toMap().value("id").toString();
    };
    const auto port = [&](int index) { return pumpPorts.at(index).toMap().value("id").toString(); };
    if (!studio.enterComposite(pumpId))
        return fail("Редактор типа демонстрационного насоса");
    studio.selectWidget(pumpLayers.at(5).toMap().value("id").toString());
    if (!click("editLayerAnimation"))
        return fail("Открытие параметров анимации слоя");
    auto *animationKind = item(scene, "layerAnimationKind");
    auto *animationTag = item(scene, "layerAnimationTag");
    auto *animationPeriod = item(scene, "layerAnimationPeriod");
    if (!animationKind || animationKind->property("currentValue") != "blink" ||
        !animationTag || animationTag->property("currentValue") != port(1) ||
        !animationPeriod || !animationPeriod->setProperty("value", 800) ||
        !click("layerAnimationApply") || studio.widget().value("animation").toMap().value("periodMs").toInt() != 800)
        return fail("Чтение и сохранение тега, эффекта и периода анимации");
    studio.undo();
    if (!click("faceplatePreview") ||
        !waitFor([&] {
            auto *base = item(item(scene, layer(2)), "widgetImage");
            return base && base->property("status").toInt() == 1;
        }) || item(scene, layer(3)) || item(scene, layer(5))) {
        auto *baseLayer = item(scene, layer(2));
        auto *baseImage = item(baseLayer, "widgetImage");
        qWarning() << "Widget preview" << item(scene, "previewJournal") << baseLayer << baseImage
                   << (baseImage ? baseImage->property("status") : QVariant{}) << item(scene, layer(3)) << item(scene, layer(5));
        capture("custom-widget-preview-diagnostic.png");
        return fail("Предпросмотр показывает SVG насоса, пока рабочий и аварийный слои скрыты");
    }
    if (!click("previewBool_" + port(0)) || !waitFor([&] {
        auto *running = item(item(scene, layer(3)), "widgetImage");
        return running && running->property("status").toInt() == 1;
    }) || item(scene, layer(4))->property("displayTitle") != "В работе")
        return fail("Пробный сигнал работы меняет изображение и подпись");
    auto *impeller = item(scene, layer(9));
    if (!impeller || !waitFor([&] { return impeller->property("motionActive").toBool() && impeller->rotation() > 10; }))
        return fail("Крыльчатка вращается по пробному сигналу работы");
    if (!click("previewValue_" + port(1) + "_input"))
        return fail("Ввод пробного уровня в предпросмотре");
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_6);
    QTest::keyClick(window, Qt::Key_5);
    auto *warningLayer = item(scene, layer(5));
    if (!warningLayer || !waitFor([&] { return warningLayer->opacity() < 0.4; }) ||
        !waitFor([&] { return warningLayer->opacity() > 0.9; }))
        return fail("Аварийный слой мигает при пробном уровне 65");
    if (!click("previewHitAreaAction") || !waitFor([&] {
        auto *journal = item(scene, "previewJournal");
        auto *command = item(scene, "previewBool_" + port(2));
        return journal && journal->property("count").toInt() == 1 && command && command->property("checked").toBool();
    }) || studio.running())
        return fail("Прозрачная кнопка пишет пробную команду в журнал без запуска опроса");
    capture("custom-widget-preview.png");
    if (!click("previewBool_" + port(0)) ||
        !waitFor([&] { return !impeller->property("motionActive").toBool() && qFuzzyIsNull(impeller->rotation()); }) ||
        !click("previewValue_" + port(1) + "_input"))
        return fail("Крыльчатка останавливается при снятии пробного сигнала");
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_0);
    if (!waitFor([&] { return !item(scene, layer(5)); }))
        return fail("Мигающий слой скрывается при нормальном пробном уровне");
    if (!click("widgetPreviewClose") || !click("faceplateBack"))
        return fail("Выход из предпросмотра на экран");

    const auto originalType = studio.faceplates().first().toMap();
    const auto originalTypeId = originalType.value("id").toString();
    if (!clickTypeAction("faceplateDuplicate_" + originalTypeId) ||
        !waitFor([&] { return item(scene, "faceplateLibraryName"); }))
        return fail("Дублирование из списка открывает диалог с названием");
    typeName = item(scene, "faceplateLibraryName");
    if (typeName->property("text").toString() != originalType.value("name").toString() + " (копия)" ||
        !click("faceplateLibraryNameCancel") || !waitFor([&] { return !item(scene, "faceplateLibraryName"); }) ||
        studio.faceplates().size() != originalTypes || studio.compositeDepth() != 0)
        return fail("Диалог предлагает свободное имя, отмена оставляет список типов без изменений");
    if (!clickTypeAction("faceplateDuplicate_" + originalTypeId) ||
        !waitFor([&] { return item(scene, "faceplateLibraryName"); }))
        return fail("Повторное открытие диалога дублирования из списка");
    typeName = item(scene, "faceplateLibraryName");
    if (!waitFor([&] { return typeName->hasActiveFocus(); }) ||
        !typeName->setProperty("text", "Отдельная панель оборудования") ||
        !click("faceplateLibraryNameApply") ||
        !waitFor([&] { return studio.compositeDepth() == 1; }) ||
        studio.faceplates().size() != originalTypes + 1 ||
        studio.canvasSettings().value("name") != "Отдельная панель оборудования" ||
        studio.items().size() != originalType.value("items").toList().size())
        return fail("Подтверждение имени открывает отдельный холст копии с элементами");
    const auto copyTypeId = studio.canvasSettings().value("id").toString();
    if (copyTypeId.isEmpty() || copyTypeId == originalTypeId || !click("screenItem_label"))
        return fail("Выбор элемента в копии типа");
    auto *copyCaption = item(scene, "widgetTitle");
    if (!copyCaption || !copyCaption->setProperty("text", "Изменено только в копии") || !click("applyWidget") ||
        studio.widget().value("title") != "Изменено только в копии" || studio.faceplates().first().toMap() != originalType)
        return fail("Изменение виджета копии оставляет исходный тип прежним");
    if (!click("faceplateBack") || studio.compositeDepth() != 0 || studio.items() != itemsBeforeNewType ||
        !clickTypeAction("faceplateDelete_" + copyTypeId) || !click("faceplateDeleteConfirm") ||
        studio.faceplates().size() != originalTypes)
        return fail("Копия остаётся отдельным типом в библиотеке без добавления экземпляра");

    auto *palette = item(scene, "palette_value");
    auto *canvas = item(scene, "screenCanvas");
    if (!palette || !canvas)
        return fail("Палитра и холст");
    const auto mainScreen = studio.screenId();
    const auto originalScreens = studio.screens().size();
    const auto originalTags = studio.tags().size();
    const auto controlScreen = studio.screens().at(1).toMap().value("id").toString();
    const auto instrumentScreen = studio.screens().at(2).toMap().value("id").toString();
    const auto galleryScreen = studio.screens().at(3).toMap().value("id").toString();
    const auto panelsScreen = studio.screens().at(4).toMap().value("id").toString();
    const auto quantity = studio.items().size();
    const auto start = canvas->mapToScene(QPointF(894, 18)).toPoint();
    drag(point(palette), start);
    if (studio.items().size() != quantity + 1 || studio.selectedId().isEmpty()
        || studio.widget().value("x").toDouble() < 840)
        return fail(QString("Перетаскивание из палитры. Элементов %1, X %2").arg(studio.items().size())
                    .arg(studio.widget().value("x").toDouble()));
    const auto beforePosition = studio.widget().value("x").toDouble();
    const auto center = canvas->mapToScene(QPointF(beforePosition + 85, 62)).toPoint();
    drag(center, center - QPoint(28, 0));
    if (studio.widget().value("x").toDouble() >= beforePosition)
        return fail("Перемещение элемента");
    auto *grip = item(scene, "resizeGrip");
    const auto oldWidth = studio.widget().value("w").toDouble();
    if (!grip)
        return fail("Ручка изменения размера");
    drag(point(grip), point(grip) + QPoint(24, 12));
    if (studio.widget().value("w").toDouble() <= oldWidth)
        return fail("Изменение размера");
    auto *tagPicker = item(scene, "widgetTag");
    if (!tagPicker || !tagPicker->setProperty("currentIndex", 2) || !click("applyWidget")
        || studio.widget().value("tag") != map(1).value("id"))
        return fail("Привязка тега в свойствах");
    const auto editedWidget = studio.selectedId();
    auto *label = item(scene, "screenItem_label");
    if (!label)
        return fail("Выбор элемента для группы");
    QTest::mouseClick(window, Qt::LeftButton, Qt::ShiftModifier, point(label));
    QTest::qWait(60);
    if (studio.selectedIds().size() != 2 || !click("groupWidgets") || studio.widget().value("group").toString().isEmpty())
        return fail("Группировка из редактора");
    studio.undo();
    const auto faceplateCount = studio.faceplates().size();
    if (!click("faceplateCreate"))
        return fail("Создание составного виджета из двух элементов");
    auto *faceplateName = item(scene, "faceplateName");
    if (!faceplateName || !faceplateName->setProperty("text", "Карточка показания") ||
        !click("faceplateCreateApply") || studio.faceplates().size() != faceplateCount + 1 ||
        studio.widget().value("kind") != "faceplate" || studio.items().size() != quantity ||
        studio.resolveFaceplate(studio.widget()).size() != 2)
        return fail("На экране остаётся один экземпляр с двумя элементами внутри");
    studio.undo();
    studio.selectWidget({});
    studio.selectWidget(editedWidget);
    studio.removeWidget();

    if (!click("screenItem_trend") || !click("editTrend") || !click("trendAddSeries"))
        return fail("Настройка многорядного графика из редактора");
    auto *firstTrace = item(scene, "trendTag_0");
    auto *secondTrace = item(scene, "trendTag_1");
    auto *firstStyle = item(scene, "trendStyle_0");
    if (!firstTrace || !secondTrace || !firstStyle ||
        firstTrace->property("currentIndex").toInt() != 1 || secondTrace->property("currentIndex").toInt() != 2 ||
        firstStyle->property("currentIndex").toInt() != 1)
        return fail("Показ сохранённых тегов и линий в настройке графика");
    auto *fourthTrace = item(scene, "trendTag_3");
    auto *timeLabels = item(scene, "trendTimeLabels");
    auto *divisions = item(scene, "trendDivisions_0");
    auto *axisSide = item(scene, "trendSide_1");
    if (!fourthTrace || !fourthTrace->setProperty("currentIndex", 8)
        || !QMetaObject::invokeMethod(fourthTrace, "activated", Q_ARG(int, 8))
        || !timeLabels || !timeLabels->setProperty("currentIndex", 1)
        || !divisions || !divisions->setProperty("value", 4) || !QMetaObject::invokeMethod(divisions, "valueModified")
        || !axisSide || axisSide->property("currentIndex").toInt() != 1)
        return fail("Настройка четвёртого тега, подписей времени и делений шкалы");
    capture("trend-editor.png");
    if (!click("trendSave"))
        return fail("Сохранение настроек графика");
    const auto trendConfig = studio.widget().value("trend").toMap();
    const auto rows = trendConfig.value("series").toList();
    if (rows.size() != 4 || rows.last().toMap().value("tag") != map(7).value("id")
        || trendConfig.value("axis").toString() != "individual" || trendConfig.value("timeLabels").toString() != "relative"
        || rows.first().toMap().value("divisions").toInt() != 4)
        return fail("Сохранение четырёх независимых шкал и настроек времени");
    studio.undo();

    if (!click("screenItem_value") || studio.widget().value("title").toString() != "Давление" || !click("editAppearance"))
        return fail("Форма цветов и порогов");
    auto *baseBackground = item(scene, "appearanceBackground");
    auto *baseValue = item(scene, "appearanceValue");
    auto *sample = item(scene, "appearanceSample");
    auto *preview = item(scene, "appearancePreview");
    if (!baseBackground || !baseValue || !sample || !preview)
        return fail("Основные цвета и предпросмотр");
    baseBackground->setProperty("text", "#f3f7ff");
    baseValue->setProperty("text", "#163867");
    sample->setProperty("text", "2,8");
    if (preview->property("color").value<QColor>() != QColor("#e7f6ed"))
        return fail("Предпросмотр правила больше или равно");
    sample->setProperty("text", "0,5");
    if (preview->property("color").value<QColor>() != QColor("#fff1db") || !click("appearanceAdd"))
        return fail("Предпросмотр правила меньше");
    auto *limit = item(scene, "appearanceLimit_2");
    auto *value = item(scene, "appearanceRuleValue_2");
    if (!limit || !value || !limit->setProperty("text", "1")
        || !QMetaObject::invokeMethod(limit, "textEdited")
        || !QMetaObject::invokeMethod(value, "setColor", Q_ARG(QVariant, QVariant("#2674a9"))))
        return fail("Ввод нового правила");
    sample->setProperty("text", "2,8");
    capture("appearance.png");
    if (!click("appearanceSave") || studio.widget().value("appearance").toMap().value("rules").toList().size() != 3)
        return fail("Сохранение настроенных цветов");

    if (!click("addScreen"))
        return fail("Добавление экрана");
    auto *name = item(scene, "screenName");
    if (!name)
        return fail("Диалог имени экрана");
    name->setProperty("text", "Диагностика");
    name->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Return);
    if (!waitFor([&] { return studio.screens().size() == originalScreens + 1; }))
        return fail("Сохранение имени экрана");
    const auto diagnosticsScreen = studio.screenId();
    if (!click("screenMenu") || !click("openScreenProperties"))
        return fail("Открытие свойств экрана из меню");
    auto *screenWidth = item(scene, "screenWidth");
    auto *screenHeight = item(scene, "screenHeight");
    auto *screenColor = item(scene, "screenBackground");
    auto *fitMode = item(scene, "screenOperatorFit");
    if (!screenWidth || !screenHeight || !screenColor || !fitMode)
        return fail("Поля размера и цвета фона экрана");
    screenWidth->setProperty("text", "1280");
    screenHeight->setProperty("text", "720");
    screenColor->setProperty("text", "#cee2ee");
    fitMode->setProperty("currentIndex", 1);
    capture("screen-properties.png");
    if (!click("screenPropertiesSave") ||
        studio.screens().last().toMap().value("width").toInt() != 1280 ||
        studio.screens().last().toMap().value("height").toInt() != 720 ||
        studio.screens().last().toMap().value("operatorFit").toString() != "fit" ||
        canvas->property("color").value<QColor>() != QColor("#cee2ee") || canvas->width() != 1280)
        return fail("Новый размер и фон на холсте");
    studio.selectScreen(mainScreen);

    if (!click("tab2") || !click("addSource"))
        return fail("Открытие формы источника");
    auto *sourceName = item(scene, "sourceName");
    auto *sourceHost = item(scene, "sourceHost");
    if (!sourceName || !sourceHost)
        return fail("Поля подключения");
    sourceName->setProperty("text", "Ещё один источник");
    sourceHost->setProperty("text", "127.0.0.1");
    if (!click("sourceSave") || studio.sources().size() != 2)
        return fail("Сохранение источника из интерфейса");
    if (!studio.removeSource(studio.sources().last().toMap().value("id").toString()))
        return fail("Удаление временного источника");
    if (!click("tab3") || !click("addTag"))
        return fail("Открытие формы тега");
    auto *newTag = item(scene, "tagName");
    if (!newTag)
        return fail("Поля тега");
    newTag->setProperty("text", "Тестовый тег");
    if (!click("tagSave") || studio.tags().size() != originalTags + 1)
        return fail("Сохранение тега из интерфейса");
    if (!studio.removeTag(studio.tags().last().toMap().value("id").toString()))
        return fail("Удаление временного тега");
    auto alarmTag = map(0);
    auto alarmRule = alarmTag.value("alarm").toMap();
    alarmRule.insert("limit", 50);
    alarmTag.insert("alarm", alarmRule);
    if (!studio.setTag(alarmTag))
        return fail("Настройка испытательного порога тревоги");
    if (!click("tab0") || !click("screenTab_" + controlScreen))
        return fail("Пульт управления в редакторе");
    const auto selectChoice = [&](const QString &name, const QVariant &value) {
        auto *box = item(scene, name);
        if (!box) return false;
        int index = -1;
        return QMetaObject::invokeMethod(box, "indexOfValue", Q_RETURN_ARG(int, index), Q_ARG(QVariant, value))
            && index >= 0 && box->setProperty("currentIndex", index)
            && QMetaObject::invokeMethod(box, "activated", Q_ARG(int, index));
    };
    if (!click("screenItem_slider") || !selectChoice("widgetTag", map(2).value("id")) || !click("applyWidget")
        || studio.widget().value("tag") != map(2).value("id"))
        return fail("Отдельный тег показания ползунка");
    if (!click("widgetActions") || !click("widgetActionAdd") ||
        !selectChoice("widgetActionEvent_0", "change") ||
        !selectChoice("widgetActionTag_0", map(5).value("id")) ||
        !selectChoice("widgetActionValueMode_0", "widget"))
        return fail("Редактор действия ползунка по событию изменения");
    capture("widget-actions.png");
    if (!click("widgetActionApply") || studio.widget().value("actions").toList().size() != 1
        || studio.widget().value("tag") != map(2).value("id"))
        return fail("Сохранение отдельной настройки чтения и записи ползунка");
    const auto controlWidgets = studio.items();
    const auto copyButton = std::find_if(controlWidgets.cbegin(), controlWidgets.cend(), [](const QVariant &entry) {
        return entry.toMap().value("title") == "Обороты в уставку";
    });
    if (copyButton == controlWidgets.cend())
        return fail("Демо кнопки записи из другого тега");
    studio.selectWidget(copyButton->toMap().value("id").toString());
    QTest::qWait(50);
    if (!click("widgetActions") || !selectChoice("widgetActionValueMode_0", "constant") ||
        !selectChoice("widgetActionValueMode_0", "tag") ||
        !selectChoice("widgetActionSourceTag_0", map(2).value("id")))
        return fail("Выбор другого тега как источника значения");
    capture("widget-copy-tag.png");
    if (!click("widgetActionApply"))
        return fail("Сохранение действия записи из другого тега");
    const auto copyAction = studio.widget().value("actions").toList().value(0).toMap();
    if (copyAction.value("event") != "press" || copyAction.value("tag") != map(5).value("id") ||
        copyAction.value("valueMode") != "tag" || copyAction.value("valueTag") != map(2).value("id"))
        return fail("Источник значения и тег назначения хранятся отдельно");

    if (!click("screenTab_" + panelsScreen))
        return fail("Экран с экземплярами фейсплейса");
    const auto panelId = [&](const QString &title) {
        const auto items = studio.items();
        for (const auto &entry : items) {
            const auto data = entry.toMap();
            if (data.value("title") == title)
                return data.value("id").toString();
        }
        return QString{};
    };
    const auto pumpPanel = panelId("Панель насоса");
    const auto valvePanel = panelId("Панель клапана");
    if (pumpPanel.isEmpty() || valvePanel.isEmpty() || !click("screenItem_faceplate_" + pumpPanel) ||
        !click("faceplateOpenType") || !waitFor([&] { return studio.compositeDepth() == 1; }) ||
        !click("faceplatePorts"))
        return fail("Редактор общего типа и интерфейс тегов");
    capture("faceplate-interface.png");
    if (!click("faceplatePortsCancel") || !waitFor([&] { return !item(scene, "faceplatePortsApply"); }) ||
        !click("screenItem_label"))
        return fail("Редактирование элемента внутри типа");
    auto *childTitle = item(scene, "widgetTitle");
    if (!childTitle || !childTitle->setProperty("text", "Параметры оборудования") || !click("applyWidget"))
        return fail("Изменение подписи в общем типе");
    capture("faceplate-type.png");
    const auto measurementId = studio.items().value(1).toMap().value("id");
    if (measurementId.toString().isEmpty() || !click("faceplatePropertyInterface") ||
        !click("propertyInterfaceAdd"))
        return fail("Добавление свойства в интерфейс типа");
    auto *propertyName = item(scene, "propertyInterfaceName_7");
    if (!propertyName || !propertyName->setProperty("text", "Подпись измерения") ||
        !selectChoice("propertyInterfaceItem_7", measurementId) ||
        !selectChoice("propertyInterfaceKind_7", "title") || !click("propertyInterfaceApply"))
        return fail("Публикация подписи дочернего показания");
    const auto currentType = studio.faceplates().value(0).toMap();
    if (currentType.value("properties").toList().size() != 8)
        return fail("Новое свойство добавлено в общий тип");
    if (!click("faceplateBack") || studio.compositeDepth() != 0 ||
        !click("screenItem_faceplate_" + valvePanel) || !click("faceplateBindings") ||
        !selectChoice("faceplateBindingTag_0", map(7).value("id")) || !click("faceplateBindingsApply"))
        return fail("Отдельная привязка показания экземпляра");
    const auto valveParts = studio.resolveFaceplate(studio.widget());
    if (valveParts.value(0).toMap().value("title") != "Параметры оборудования" ||
        valveParts.value(1).toMap().value("tag") != map(7).value("id"))
        return fail("Экземпляр получает новый вид типа и сохраняет свои теги");
    if (!click("faceplateInstanceProperties") || !click("faceplatePropertyOwn_0") ||
        !click("faceplatePropertyOwn_4") || !click("faceplatePropertyOwn_7"))
        return fail("Переопределение свойств экземпляра");
    const auto editProperty = [&](const QString &name, const QString &value) {
        auto *field = item(scene, name);
        return field && field->setProperty("text", value);
    };
    if (!editProperty("faceplatePropertyText_0", "Параметры клапана") ||
        !editProperty("faceplatePropertyColor_1", "#fbe6d9") ||
        !editProperty("faceplatePropertyColor_4", "#733a29") ||
        !editProperty("faceplatePropertyText_7", "Температура") ||
        !click("faceplatePropertyOwn_3") || !click("faceplatePropertiesApply"))
        return fail("Запись подписей и цветов с возвратом видимости к настройке типа");
    const auto inheritedFeedback = studio.resolveFaceplate(studio.widget()).value(3).toMap();
    if (!inheritedFeedback.value("visible").toBool())
        return fail("Сброс переопределения возвращает видимость из типа");
    if (!click("faceplateInstanceProperties") || !click("faceplatePropertyOwn_3"))
        return fail("Повторное включение индивидуальной видимости");
    auto *showIndicator = item(scene, "faceplatePropertyVisible_3");
    if (!showIndicator || (showIndicator->property("checked").toBool() && !click("faceplatePropertyVisible_3")) ||
        showIndicator->property("checked").toBool())
        return fail("Скрытие индикатора только в экземпляре клапана");
    const auto valveFeedbackTag = map(9).value("id");
    const auto editBinding = [&](const QString &property) {
        return click(property) && selectChoice("faceplateBindingConditionTag", valveFeedbackTag) &&
            selectChoice("faceplateBindingConditionOp", "eq") && editProperty("faceplateBindingConditionLimit", "0");
    };
    if (!editBinding("faceplatePropertyBinding_5") || !click("faceplateBindingConditionApply") ||
        !editBinding("faceplatePropertyBinding_6") || !editProperty("faceplateBindingMatchColor", "#b2212d"))
        return fail("Выбор тега и условия для видимости и цвета аварийной плашки");
    capture("faceplate-property-binding.png");
    if (!click("faceplateBindingConditionApply"))
        return fail("Применение привязки цвета в черновике свойств");
    capture("faceplate-properties.png");
    if (!click("faceplatePropertiesApply"))
        return fail("Сохранение видимости экземпляра");
    const auto visibleProperty = currentType.value("properties").toList().value(3).toMap().value("id").toString();
    const auto overrides = studio.widget().value("overrides").toMap();
    if (!overrides.contains(visibleProperty) || overrides.value(visibleProperty).toBool())
        return fail("Явное выключение видимости записано отдельно от наследования");
    const auto properties = currentType.value("properties").toList();
    const auto ruleFor = [&](int index) {
        const auto id = properties.at(index).toMap().value("id").toString();
        return studio.widget().value("propertyBindings").toMap().value(id).toMap();
    };
    const auto visibilityRule = ruleFor(5);
    const auto colorRule = ruleFor(6);
    if (visibilityRule.value("tag") != valveFeedbackTag || !visibilityRule.value("value").toBool() ||
        !visibilityRule.contains("otherwise") || visibilityRule.value("otherwise").toBool() ||
        colorRule.value("tag") != valveFeedbackTag || colorRule.value("value") != "#b2212d")
        return fail("Две независимые динамические привязки сохранены у экземпляра клапана");
    const auto copiedValveType = studio.duplicateFaceplate(currentType.value("id").toString());
    if (copiedValveType.isEmpty() || !studio.enterComposite(copiedValveType))
        return fail("Открытие копии для изменения интерфейса");
    auto alternateProperties = studio.faceplates().last().toMap().value("properties").toList();
    auto alternateCaption = alternateProperties.first().toMap();
    alternateCaption.remove("id");
    alternateCaption.remove("originId");
    alternateCaption["name"] = "Подпись узла";
    alternateProperties[0] = alternateCaption;
    if (!studio.setFaceplateProperties(alternateProperties))
        return fail("Подготовка свойства копии с другим именем и идентификатором");
    const auto alternateCaptionId = studio.faceplates().last().toMap().value("properties").toList()
        .first().toMap().value("id");
    studio.leaveComposite();
    studio.selectWidget(valvePanel);
    if (!click("faceplateChangeType") ||
        !selectChoice("faceplateChangeTarget", copiedValveType))
        return fail("Выбор типа-копии для размещённого фейсплейса");
    auto *changeApply = item(scene, "faceplateChangeApply");
    if (!changeApply || changeApply->isEnabled() ||
        !waitFor([&] { return item(scene, "faceplateChangeMap_3"); }) ||
        !selectChoice("faceplateChangeMap_3", alternateCaptionId) ||
        !waitFor([&] { return changeApply->isEnabled(); }))
        return fail("Ручное сопоставление свойства при смене типа выбранного экземпляра");
    capture("faceplate-change-type.png");
    if (!click("faceplateChangeApply") ||
        !waitFor([&] { return studio.widget().value("faceplate").toString() == copiedValveType; }) ||
        studio.selectedId() != valvePanel ||
        studio.widget().value("bindings").toMap().size() != 3 ||
        studio.widget().value("overrides").toMap().size() != overrides.size() ||
        studio.widget().value("propertyBindings").toMap().size() != 2)
        return fail("Смена типа сохраняет размещённый экземпляр, теги и его настройки");
    if (!studio.changeFaceplateType(valvePanel, currentType.value("id").toString()) ||
        !click("faceplateChangeType") || !selectChoice("faceplateChangeTarget", copiedValveType) ||
        !waitFor([&] {
            auto *choice = item(scene, "faceplateChangeMap_3");
            auto *apply = item(scene, "faceplateChangeApply");
            return choice && choice->property("currentValue") == alternateCaptionId && apply && apply->isEnabled();
        }) || !click("faceplateChangeApply") ||
        !waitFor([&] { return studio.widget().value("faceplate").toString() == copiedValveType; }))
        return fail("Запомненное сопоставление применяется при повторной смене типа");
    const auto typesBeforeInstanceCopy = studio.faceplates().size();
    if (!click("faceplateCopyType") || !waitFor([&] { return item(scene, "faceplateCopyName"); }))
        return fail("Копирование из инспектора открывает диалог с названием");
    auto *copyName = item(scene, "faceplateCopyName");
    if (!copyName->property("text").toString().endsWith(" (копия)") || !click("faceplateNameCancel") ||
        !waitFor([&] { return !item(scene, "faceplateCopyName"); }) ||
        studio.faceplates().size() != typesBeforeInstanceCopy || studio.widget().value("faceplate") != copiedValveType)
        return fail("Диалог предлагает имя, отмена оставляет экземпляр на прежнем типе");
    if (!click("faceplateCopyType") || !waitFor([&] { return item(scene, "faceplateCopyName"); }))
        return fail("Повторное открытие диалога копирования");
    copyName = item(scene, "faceplateCopyName");
    if (!waitFor([&] { return copyName->hasActiveFocus(); }) ||
        !copyName->setProperty("text", "Панель клапана для стенда") || !click("faceplateCopyApply") ||
        !waitFor([&] { return studio.compositeDepth() == 1 && !item(scene, "faceplateCopyName"); }) ||
        studio.faceplates().size() != typesBeforeInstanceCopy + 1 ||
        studio.canvasSettings().value("id") != studio.faceplates().last().toMap().value("id") ||
        studio.canvasSettings().value("name") != "Панель клапана для стенда")
        return fail("Выбранное имя сохраняется, открывается редактор копии");
    if (!click("faceplateBack") ||
        studio.compositeDepth() != 0 || studio.screenId() != panelsScreen || studio.selectedId() != valvePanel ||
        studio.widget().value("faceplate").toString() == copiedValveType ||
        studio.widget().value("faceplate") != studio.faceplates().last().toMap().value("id") ||
        studio.widget().value("bindings").toMap().size() != 3 ||
        studio.widget().value("overrides").toMap().size() != overrides.size() ||
        studio.widget().value("propertyBindings").toMap().size() != 2)
        return fail("Копирование из инспектора сразу переводит экземпляр на отдельный тип с его настройками");
    if (!click("faceplateLibraryInsert") || studio.widget().value("kind") != "faceplate" ||
        !studio.widget().value("bindings").toMap().isEmpty() || !studio.widget().value("overrides").toMap().isEmpty() ||
        !studio.widget().value("propertyBindings").toMap().isEmpty())
        return fail("Вставка нового экземпляра из библиотеки");
    studio.removeWidget();
    QTest::qWait(100);
    auto *overviewTab = item(scene, "screenTab_" + mainScreen);
    const auto overviewPosition = overviewTab ? point(overviewTab) : QPoint{};
    if (!click("screenTab_" + mainScreen) || !waitFor([&] { return studio.screenId() == mainScreen; })) {
        auto *screenTabs = item(scene, "screenTabs");
        qWarning() << "Overview tab" << overviewTab << overviewPosition << "current screen" << studio.screenId()
                   << "editor depth" << studio.compositeDepth()
                   << "tab strip" << (screenTabs ? screenTabs->width() : 0)
                   << "offset" << (screenTabs ? screenTabs->property("contentX") : QVariant{});
        capture("faceplate-diagnostic.png");
        return fail("Возврат на обзор после настройки событий");
    }
    const auto path = QUrl::fromLocalFile(temporary.filePath("ui.scada.json"));
    if (!studio.save(path) || !click("runButton") || !studio.running())
        return fail("Запуск опроса из интерфейса");
    if (!waitFor([&] { return reading(3).value("value").toInt() == 1 && reading(3).value("quality") == "good"; }))
        return fail("Получение первых показаний");
    QTest::qWait(650);
    auto *dashboardTrend = item(scene, "dashboardTrendPlot");
    if (!dashboardTrend || !dashboardTrend->property("individualAxes").toBool()
        || !dashboardTrend->property("relativeTime").toBool()
        || !waitFor([&] { return dashboardTrend->property("visibleSeriesCount").toInt() == 3; })) {
        qWarning() << "Trend" << dashboardTrend << "active screen" << studio.screenId() << "overview" << mainScreen
                   << "editor depth" << studio.compositeDepth() << "items" << studio.items().size();
        capture("faceplate-diagnostic.png");
        return fail("Отрисовка трёх независимых рядов на операторском экране");
    }
    const auto scales = dashboardTrend->property("drawnScales").value<QJSValue>().toVariant().toList();
    if (scales.size() != 3 || scales.at(0).toMap().value("units").toString() != "%"
        || scales.at(0).toMap().value("side").toString() != "left"
        || scales.at(1).toMap().value("range").toList().value(1).toInt() != 6
        || scales.at(2).toMap().value("range").toList().value(1).toInt() != 3000
        || scales.at(2).toMap().value("side").toString() != "right")
        return fail("Цветные шкалы с инженерными единицами и заданными диапазонами");
    auto *trendGesture = item(scene, "dashboardTrendGesture");
    auto *trendWidget = item(scene, "dashboardTrend");
    auto *periodMenu = trendWidget ? trendWidget->property("periodPopup").value<QObject *>() : nullptr;
    if (!trendGesture || !periodMenu)
        return fail("Меню периода на операторском графике");
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, point(trendGesture));
    if (!waitFor([&] { return periodMenu->property("opened").toBool(); }))
        return fail("Открытие меню периода правой кнопкой");
    QTest::keyClick(window, Qt::Key_Escape);
    if (!waitFor([&] { return !periodMenu->property("visible").toBool(); }))
        return fail("Закрытие меню периода");
    const auto pressureLegend = "dashboardTrendLegend_" + map(1).value("id").toString();
    if (!click(pressureLegend) || !waitFor([&] { return dashboardTrend->property("visibleSeriesCount").toInt() == 2; }) ||
        !click(pressureLegend) || !waitFor([&] { return dashboardTrend->property("visibleSeriesCount").toInt() == 3; }))
        return fail("Отключение и возврат ряда через компактную легенду");
    const auto pressureColors = [&](const QColor &background, const QColor &ink) {
        auto *display = item(scene, "screenItem_value");
        auto *label = item(display, "widgetValue");
        return display && label && display->property("surfaceColor").value<QColor>() == background &&
            label->property("color").value<QColor>() == ink;
    };
    if (!pressureColors(QColor("#e7f6ed"), QColor("#176044")))
        return fail("Зелёная карточка давления в рабочем режиме");
    const auto canvasFillsViewport = [&](double padding) {
        auto *view = item(scene, "dashboardView");
        auto *board = item(scene, "screenCanvas");
        if (!view || !board) return false;
        const auto topLeft = board->mapToScene(QPointF{}) - view->mapToScene(QPointF{});
        const auto bottomRight = board->mapToScene(QPointF(board->width(), board->height())) -
            view->mapToScene(QPointF(view->width(), view->height()));
        return qAbs(topLeft.x() - padding) < 1 && qAbs(topLeft.y() - padding) < 1 &&
            qAbs(bottomRight.x() + padding) < 1 && qAbs(bottomRight.y() + padding) < 1;
    };
    if (!canvasFillsViewport(3))
        return fail("Мнемосхема заполняет широкое окно");
    QTest::mouseMove(window, QPoint(1, 1));
    capture("operator.png");
    capture("trend-widget.png", item(scene, "screenItem_trend"));
    const auto initialSize = window->size();
    window->resize(window->minimumWidth(), window->minimumHeight());
    QTest::qWait(100);
    const auto onScreen = [&](const QString &name) {
        auto *control = item(scene, name);
        if (!control) return false;
        const auto origin = control->mapToScene(QPointF{});
        return control->width() > 0 && origin.x() >= 0 && origin.x() + control->width() <= window->width();
    };
    auto *toolbar = item(scene, "mainToolbar");
    auto *operatorView = item(scene, "dashboardView");
    if (!toolbar || toolbar->height() > 45 || !operatorView || !canvasFillsViewport(3) ||
        operatorView->mapToScene(QPointF{}).y() > 100 ||
        !onScreen("tab0") || !onScreen("tab5") || !onScreen("saveButton") ||
        !onScreen("runButton") || !onScreen("pauseDemo") || !onScreen("screenTab_" + mainScreen))
        return fail("Компактная навигация в операторском окне минимального размера");
    capture("operator-compact.png");
    window->resize(initialSize);
    QTest::qWait(100);
    QTest::keyClick(window, Qt::Key_F11);
    if (!waitFor([&] {
            return window->visibility() == QWindow::FullScreen && !item(scene, "mainToolbar") &&
                !item(scene, "mainFooter") && !item(scene, "screenToolbar");
        }))
        return fail("Клавиша F11 скрывает меню, вкладки экранов и строку состояния");
    operatorView = item(scene, "dashboardView");
    if (!operatorView || operatorView->mapToScene(QPointF{}) != QPointF{} || !canvasFillsViewport(0) ||
        operatorView->size().toSize() != window->size())
        return fail("Полноэкранная мнемосхема занимает всю рабочую область");
    capture("operator-fullscreen.png");
    QTest::keyClick(window, Qt::Key_Escape);
    if (!waitFor([&] {
            return window->visibility() != QWindow::FullScreen && item(scene, "mainToolbar") &&
                item(scene, "mainFooter") && item(scene, "screenToolbar");
        }))
        return fail("Esc возвращает оконный вид и навигацию");
    if (!click("screenTab_" + diagnosticsScreen) || canvas->property("fillWindow").toBool())
        return fail("Режим с сохранением пропорций для отдельной мнемосхемы");
    if (!click("screenTab_" + galleryScreen) || studio.screenId() != galleryScreen)
        return fail("Открытие экрана со всеми элементами");
    auto *dashboard = item(scene, "dashboard");
    const auto tools = dashboard ? dashboard->property("tools").value<QJSValue>().toVariant().toList() : QVariantList{};
    if (tools.size() != 26 || studio.items().size() != tools.size())
        return fail("Все типы из палитры представлены в галерее");
    for (const auto &entry : tools) {
        const auto kind = entry.toMap().value("kind").toString();
        if (!item(scene, "screenItem_" + kind))
            return fail(QString("В галерее отсутствует виджет %1").arg(kind));
    }
    auto *picture = item(scene, "widgetImage");
    if (!picture || !waitFor([&] { return picture->property("status").toInt() == 1; }))
        return fail("Загружено встроенное изображение контура");
    capture("all-widgets.png");
    const auto element = [&](const QString &kind, const QString &name) {
        return item(item(scene, "screenItem_" + kind), name);
    };
    const auto press = [&](QQuickItem *control, QPoint at = {}) {
        if (!control || !control->isEnabled() || control->width() <= 0 || control->height() <= 0)
            return false;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at.isNull() ? point(control) : at);
        QTest::qWait(55);
        return true;
    };
    const auto measured = [&](int tag, int expected) {
        return reading(tag).value("quality") == "good" && reading(tag).value("value").toInt() == expected;
    };
    const auto displayed = [&](const QString &kind, int expected) {
        auto *face = element(kind, "controlFace_" + kind);
        return face && face->property("known").toBool() && face->property("actual").toInt() == expected;
    };
    const auto noCommandDialog = [&] { return !item(scene, "confirmAction"); };
    if (!press(element("switch", "controlOff")) || !noCommandDialog())
        return fail("Выключение тумблера одним действием");
    if (!waitFor([&] { return measured(3, 0) && measured(4, 0) && displayed("switch", 0); }))
        return fail("Обратная связь выключения тумблера");
    if (!press(element("switch", "controlOn")) || !noCommandDialog())
        return fail("Включение тумблера одним действием");
    if (!waitFor([&] { return measured(3, 1) && measured(4, 1) && displayed("switch", 1); }))
        return fail("Обратная связь включения тумблера");
    if (!press(element("valve", "controlOn")) || !noCommandDialog() ||
        !waitFor([&] { return measured(8, 1) && measured(9, 1); }))
        return fail("Открытие клапана по Modbus и чтение отдельного сигнала положения");
    auto *setpointInput = element("setpoint", "controlValue");
    auto *setpointApply = element("setpoint", "controlApply");
    const auto typeValue = [&](const QString &digits) {
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        for (const auto digit : digits)
            QTest::keyClick(window, static_cast<Qt::Key>(digit.unicode()));
    };
    if (!press(setpointInput) || !setpointApply)
        return fail("Поле ввода уставки");
    typeValue("9999");
    if (setpointApply->isEnabled() || !measured(5, 1200))
        return fail("Уставка за пределами диапазона блокируется");
    typeValue("1650");
    if (!press(setpointApply) || !noCommandDialog() || !waitFor([&] { return measured(5, 1650); }))
        return fail("Ввод уставки с клавиатуры и запись по Modbus");
    const auto checkRegulator = [&](const QString &kind, const QString &name, double x, double y, int previous) {
        if (!waitFor([&] { return displayed(kind, previous); }))
            return false;
        auto *operatorInput = element(kind, name);
        auto *face = element(kind, "controlFace_" + kind);
        if (!operatorInput || !face || !operatorInput->isEnabled()) {
            qWarning() << kind << "input" << operatorInput << "face" << face << "reading" << reading(5);
            return false;
        }
        const auto fraction = face->property("fraction").toDouble();
        const auto start = operatorInput->mapToScene(QPointF(operatorInput->width() * fraction, operatorInput->height() * 0.5)).toPoint();
        const auto end = operatorInput->mapToScene(QPointF(operatorInput->width() * x, operatorInput->height() * y)).toPoint();
        if (kind == "slider")
            drag(start, end);
        else if (!press(operatorInput, end))
            return false;
        const auto target = face->property("target").toInt();
        if (target == previous || target <= 0 || target > 3000 || target % 50 || !measured(5, previous)) {
            qWarning() << kind << "previous" << previous << "target" << target << "reading" << reading(5).value("value")
                       << "draft" << face->property("draft");
            return false;
        }
        auto *apply = element(kind, "controlApply");
        if (!press(apply) || !noCommandDialog() || !waitFor([&] { return measured(5, target); })) {
            qWarning() << kind << "Apply" << apply << (apply && apply->isEnabled()) << "target" << target
                       << "reading" << reading(5).value("value") << "events" << studio.events().mid(0, 3);
            capture("controls-diagnostic.png");
            return false;
        }
        return true;
    };
    if (!checkRegulator("slider", "controlSlider", 0.31, 0.5, 1650))
        return fail("Запись уставки с ползунка с заданным шагом");
    if (!checkRegulator("knob", "controlDial", 0.80, 0.24, reading(5).value("value").toInt()))
        return fail("Запись уставки с поворотного регулятора с заданным шагом");
    if (!press(element("selector", "controlPosition_2")) || !noCommandDialog())
        return fail("Отправка команды автоматического режима");
    if (!waitFor([&] { return measured(10, 2) && displayed("selector", 2); }))
        return fail("Запись автоматического режима");
    if (!press(element("selector", "controlPosition_1")) || !noCommandDialog())
        return fail("Отправка команды ручного режима");
    if (!waitFor([&] { return measured(10, 1) && displayed("selector", 1); }))
        return fail("Запись ручного режима");
    if (!press(element("alarm", "ackWidgetAlarm")) ||
        !waitFor([&] { const auto alarms = studio.alarms(); return !alarms.isEmpty() && alarms.first().toMap().value("ack").toBool(); }))
        return fail("Квитирование тревоги на мнемосхеме");
    capture("all-widgets.png");
    if (!click("screenTab_" + panelsScreen))
        return fail("Фейсплейсы в режиме оператора");
    auto *pumpInstance = item(scene, "screenItem_faceplate_" + pumpPanel);
    auto *valveInstance = item(scene, "screenItem_faceplate_" + valvePanel);
    auto *pumpControl = item(pumpInstance, "controlFace_switch");
    auto *valveControl = item(valveInstance, "controlFace_switch");
    const auto units = [](QQuickItem *instance) {
        auto *label = item(item(instance, "faceplateChild_value"), "widgetUnits");
        return label ? label->property("text").toString() : QString{};
    };
    if (!pumpControl || !valveControl || units(pumpInstance) != "об/мин" || units(valveInstance) != "°C" ||
        !waitFor([&] { return pumpControl->property("active").toBool() && valveControl->property("active").toBool(); }))
        return fail("Два экземпляра одного типа показывают разные данные");
    auto *pumpTitle = item(item(pumpInstance, "faceplateChild_label"), "widgetCaption");
    auto *valveTitle = item(item(valveInstance, "faceplateChild_label"), "widgetCaption");
    auto *pumpValue = item(pumpInstance, "faceplateChild_value");
    auto *valveValue = item(valveInstance, "faceplateChild_value");
    auto *temperatureCaption = item(valveValue, "widgetCaption");
    if (!pumpTitle || !valveTitle || !pumpValue || !valveValue || !temperatureCaption ||
        pumpTitle->property("text") != "Параметры насоса" || valveTitle->property("text") != "Параметры клапана" ||
        valveTitle->property("color").value<QColor>() != QColor("#733a29") ||
        pumpValue->property("surfaceColor").value<QColor>() != QColor("#e7f6ee") ||
        valveValue->property("surfaceColor").value<QColor>() != QColor("#fbe6d9") ||
        valveValue->property("valueOverride") != "#994922" || temperatureCaption->property("text") != "Температура" ||
        !item(pumpInstance, "faceplateChild_indicator") || item(valveInstance, "faceplateChild_indicator"))
        return fail("Независимые подписи, цвета и видимость на операторском экране");
    const auto badge = [](QQuickItem *instance) { return item(instance, "widgetCaption", "АВАРИЯ"); };
    const auto badgeColor = [&](QQuickItem *instance) {
        auto *label = badge(instance);
        return label ? label->parentItem()->parentItem()->property("surfaceColor").value<QColor>() : QColor{};
    };
    if (!waitFor([&] { return badgeColor(pumpInstance) == QColor("#b32636") && !badge(valveInstance); }))
        return fail("Квитированная активная тревога остаётся видимой только у насоса");
    if (!press(item(valveInstance, "controlOff")) ||
        !waitFor([&] { return measured(8, 0) && measured(9, 0) && measured(3, 1) && measured(4, 1) &&
            !valveControl->property("active").toBool() && pumpControl->property("active").toBool(); }))
        return fail("Команда внутри экземпляра меняет свой тег и не затрагивает другой экземпляр");
    if (!waitFor([&] {
        return badgeColor(valveInstance) == QColor("#b2212d") && badgeColor(pumpInstance) == QColor("#b32636");
    }))
        return fail("Плашка клапана появляется в своём цвете при изменении тега");
    capture("faceplate-alarm-active.png");
    auto *openValve = item(valveInstance, "controlOn");
    if (!press(openValve))
        return fail("Кнопка открытия клапана доступна у экземпляра");
    if (!waitFor([&] { return measured(9, 1) && !badge(valveInstance) && badgeColor(pumpInstance) == QColor("#b32636"); })) {
        qWarning() << "Valve command" << reading(8) << "position" << reading(9) << "level" << reading(0)
                   << "badges" << badgeColor(valveInstance) << badgeColor(pumpInstance)
                   << "events" << studio.events().mid(0, 8);
        capture("faceplate-valve-diagnostic.png");
        return fail("Плашка клапана скрывается после возврата тега без влияния на насос");
    }
    capture("faceplate-instances.png");
    if (!click("screenTab_" + instrumentScreen))
        return fail("Открытие экрана приборов");
    for (const auto *kind : {"dial", "column", "ring", "segments", "signal", "compass"}) {
        auto *instrument = item(scene, QString("instrumentFace_") + kind);
        if (!instrument || !instrument->property("valid").toBool())
            return fail(QString("Показание прибора %1").arg(kind));
    }
    capture("instruments.png");
    if (!click("screenTab_" + mainScreen))
        return fail("Возврат на обзор станции");
    if (!click("linkAction") || studio.screenId() != controlScreen)
        return fail("Переход на экран управления");
    capture("controls.png");
    auto *holdValve = item(scene, "commandAction", "Удерживать клапан открытым");
    if (!holdValve || !holdValve->isEnabled() || !press(element("valve", "controlOff")) ||
        !waitFor([&] { return measured(8, 0) && measured(9, 0); }))
        return fail("Начальное положение клапана перед проверкой событий");
    const auto holdPosition = point(holdValve);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, holdPosition);
    if (!noCommandDialog() || !waitFor([&] { return measured(8, 1) && measured(9, 1); }))
        return fail("Клапан открывается по событию «Нажал»");
    const auto outside = holdPosition - QPoint(0, static_cast<int>(holdValve->height()) + 55);
    QTest::mouseMove(window, outside, 25);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, outside);
    if (!waitFor([&] { return measured(8, 0) && measured(9, 0); }))
        return fail("Клапан закрывается при отпускании за границей кнопки");

    auto *slider = element("slider", "controlSlider");
    auto *face = element("slider", "controlFace_slider");
    const auto previousSetpoint = reading(5).value("value").toInt();
    if (!slider || !face || !slider->isEnabled() || !waitFor([&] { return displayed("slider", previousSetpoint); }))
        return fail("Ползунок показывает фактические обороты с отдельного тега");
    const auto sliderStart = slider->mapToScene(QPointF(slider->width() * face->property("fraction").toDouble(),
        slider->height() * 0.5)).toPoint();
    const auto sliderEnd = slider->mapToScene(QPointF(slider->width() * 0.44, slider->height() * 0.5)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, sliderStart);
    QTest::mouseMove(window, sliderEnd, 80);
    QTest::qWait(550);
    const auto selection = face->property("target").toInt();
    const bool staged = selection > 0 && selection != previousSetpoint && measured(5, previousSetpoint);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, sliderEnd);
    if (!staged || !noCommandDialog() || !waitFor([&] { return measured(5, selection) && displayed("slider", selection); }))
        return fail("Ползунок отправляет выбранное значение при завершении изменения");

    if (!click("commandAction", "Прочитать уставку") || !waitFor([&] {
        const auto events = studio.events();
        return std::any_of(events.cbegin(), events.cend(), [&](const QVariant &event) {
            const auto details = event.toMap();
            return details.value("tag") == map(5).value("id") && details.value("type") == "read_ok";
        });
    }))
        return fail("Щелчок запускает внеочередное чтение заданного тега");
    if (!press(element("selector", "controlPosition_0")) || !waitFor([&] {
        return measured(10, 0) && measured(2, 0) && measured(5, selection) && displayed("selector", 0);
    }))
        return fail("Фактические обороты отличаются от уставки перед копированием");
    auto *copyActionButton = item(scene, "commandAction", "Обороты в уставку");
    if (!copyActionButton || !copyActionButton->isEnabled())
        return fail("Кнопка записи оборотов в уставку");
    const auto copyPoint = point(copyActionButton);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, copyPoint);
    const bool copiedOnPress = waitFor([&] { return measured(5, 0); });
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, copyPoint);
    if (!copiedOnPress || !noCommandDialog())
        return fail("Кнопка записывает текущие обороты в уставку до отпускания");
    if (!click("commandAction", "Уставка 1800 об/мин") || !waitFor([&] { return measured(5, 1800); }) ||
        !press(element("selector", "controlPosition_1")) ||
        !waitFor([&] { return measured(10, 1) && measured(2, 1800); }))
        return fail("Возврат к ручной уставке после копирования");
    if (!click("commandAction", "Останов насоса") || !noCommandDialog())
        return fail("Обычная команда отправляется одним щелчком");
    if (!waitFor([&] { return reading(3).value("hasValue").toBool() && reading(3).value("value").toInt() == 0; }))
        return fail("Обратная связь после записи");
    if (!click("screenTab_" + mainScreen) ||
        !waitFor([&] { return pressureColors(QColor("#fff1db"), QColor("#985018")); }))
        return fail("Смена обоих цветов при падении давления");
    capture("appearance-live.png");
    trendWidget = item(scene, "dashboardTrend");
    if (!trendWidget || !click("dashboardTrendPeriod") || !click("dashboardTrendFit") ||
        !waitFor([&] { return !trendWidget->property("following").toBool(); }))
        return fail("Показ истории через меню в шапке графика");
    QTest::mouseMove(window, QPoint(1, 1));
    QTest::qWait(80);
    capture("trend-widget-history.png", item(scene, "screenItem_trend"));
    if (!click("pauseDemo") || !waitFor([&] { return reading(3).value("quality") == "stale"; }))
        return fail("Отображение потери связи");
    if (!waitFor([&] { return pressureColors(QColor("#f3f7ff"), QColor("#163867")); }))
        return fail("Возврат основных цветов при устаревших данных");
    if (!click("screenTab_" + controlScreen) || !element("switch", "controlOn")
        || element("switch", "controlOn")->isEnabled() || !click("screenTab_" + mainScreen))
        return fail("Управление при потере связи блокируется и без диалога подтверждения");
    if (!click("pauseDemo") || !waitFor([&] { return reading(3).value("quality") == "good"; }))
        return fail("Возобновление опроса");
    if (!click("tab4"))
        return fail("Открытие архива");
    auto *archive = item(scene, "archivePage");
    auto *tracks = item(scene, "archiveTracks");
    if (!archive || !tracks || tracks->property("count").toInt() != 3)
        return fail("Первые три ряда архива");
    const auto first = map(0).value("id").toString();
    const auto secondId = map(1).value("id").toString();
    if (!click("archiveChannel_" + secondId) || tracks->property("count").toInt() != 2
        || !click("archiveChannel_" + secondId) || tracks->property("count").toInt() != 3)
        return fail("Включение и отключение отдельного ряда");
    if (!click("archiveClearChannels") || tracks->property("count").toInt() != 0
        || !click("archiveAllChannels") || tracks->property("count").toInt() != originalTags)
        return fail("Выбор всех и очистка рядов");
    if (!click("archiveClearChannels") || !click("archiveChannel_" + first) || !click("archiveChannel_" + secondId)
        || !click("archiveFit") || archive->property("follow").toBool())
        return fail("Выбор двух рядов и показ всего архива");
    auto *plot = item(scene, "archivePlot_" + first);
    if (!plot)
        return fail("Интерактивная область графика");
    const auto at = plot->mapToScene(QPointF(plot->width() * 0.44, plot->height() * 0.55)).toPoint();
    QTest::mouseMove(window, at, 60);
    const auto cursorValue = [&](const QString &id) {
        auto *value = item(scene, "archiveCursorValue_" + id);
        return value ? value->property("text").toString() : QString{};
    };
    if (!waitFor([&] { return archive->property("cursor").toLongLong() > 0 &&
                               cursorValue(first).startsWith("Курсор") && !cursorValue(first).contains("—"); }))
        return fail("Мгновенное значение под курсором");
    if (!click("archiveMarkA") || archive->property("markA").toLongLong() == 0)
        return fail("Установка метки А");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at + QPoint(75, 0));
    if (!archive->property("pinned").toBool() || !click("archiveMarkB")
        || archive->property("markB") == archive->property("markA"))
        return fail("Закрепление курсора и установка метки Б");
    capture("archive.png");
    const auto spanBefore = archive->property("span").toDouble();
    QWheelEvent wheel(QPointF(at), QPointF(window->mapToGlobal(at)), QPoint{}, QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(window, &wheel);
    if (!waitFor([&] { return archive->property("span").toDouble() < spanBefore; }))
        return fail("Масштабирование колёсиком");
    const auto startBefore = archive->property("startTime").toDouble();
    drag(at, at + QPoint(85, 0));
    if (archive->property("startTime").toDouble() >= startBefore)
        return fail("Сдвиг времени перетаскиванием");
    const auto pinnedTime = archive->property("cursor");
    if (!click("archiveFit") || !click("archiveCombined"))
        return fail("Открытие сводного графика");
    auto *combined = item(scene, "archiveCombinedChart");
    if (!combined || !item(scene, "archiveCombinedPlot")
        || !waitFor([&] { return combined->property("visibleSeriesCount").toInt() == 2; })
        || archive->property("cursor") != pinnedTime)
        return fail("Две цветные серии и тот же курсор в сводном графике");
    if (!click("archiveChannel_" + secondId)
        || !waitFor([&] { return combined->property("visibleSeriesCount").toInt() == 1; })
        || !click("archiveChannel_" + secondId)
        || !waitFor([&] { return combined->property("visibleSeriesCount").toInt() == 2
                         && !cursorValue(secondId).contains("—") && !cursorValue(secondId).isEmpty(); }))
        return fail("Переключение ряда в сводном графике");
    auto *scale = item(scene, "archiveScaleMode");
    if (!scale || !scale->setProperty("currentIndex", 1) || !combined->property("normalized").toBool())
        return fail("Шкала 0–100% в сводном графике");
    QTest::qWait(120);
    capture("archive-combined.png");
    if (!click("archiveSeparate") || !item(scene, "archivePlot_" + first))
        return fail("Возврат к раздельным графикам");
    if (!click("archiveLive") || !archive->property("follow").toBool() || !click("tab5"))
        return fail("Возврат к текущим данным и просмотр журнала");
    if (studio.events().isEmpty())
        return fail("Событие записи сохранено в журнал");
    studio.selectScreen(pumpScreen);
    if (!click("tab1") || !waitFor([&] { return window->property("page").toInt() == 1; }))
        return fail("Открытие операторского экрана для демонасоса");
    if (!click("hitAreaAction") || !waitFor([&] {
        return measured(3, 1) && item(scene, "widgetCaption", "В работе");
    }))
        return fail("Пуск демонасоса через прозрачную зону команды");
    auto *liveImpeller = item(scene, "faceplateChild_image", "Крыльчатка", "displayTitle");
    if (!liveImpeller || !waitFor([&] {
        return liveImpeller->property("motionActive").toBool() && liveImpeller->rotation() > 10;
    }))
        return fail("Опрос Modbus запускает вращение крыльчатки");
    if (!waitFor([&] {
        auto *warning = item(scene, "faceplateChild_label", "Высокий уровень", "displayTitle");
        return warning && warning->property("motionActive").toBool() && warning->opacity() < 0.4;
    }, 4500) || !waitFor([&] {
        auto *warning = item(scene, "faceplateChild_label", "Высокий уровень", "displayTitle");
        return warning && warning->opacity() > 0.9;
    }))
        return fail("Уровень в симуляторе Modbus запускает мигание аварийной плашки");
    capture("custom-widget-pump.png");
    if (!click("commandAction", "Стоп") || !waitFor([&] {
        return measured(3, 0) && item(scene, "widgetCaption", "Остановлен")
            && !liveImpeller->property("motionActive").toBool() && qFuzzyIsNull(liveImpeller->rotation());
    }))
        return fail("Останов демонасоса кнопкой оператора");
    studio.stop();
    if (!studio.open(path) || studio.screens().size() != originalScreens + 1)
        return fail("Повторное открытие сохранённого проекта");
    studio.newProject(false);
    if (!click("tab2") || !click("addSource"))
        return fail("Открытие формы источника JSON-телеметрии");
    auto *driver = item(scene, "sourceDriver");
    if (!driver || !driver->setProperty("currentIndex", 1) ||
        driver->property("currentText").toString() != "JSON-телеметрия")
        return fail("Выбор источника JSON-телеметрии");
    auto *transport = item(scene, "jsonTransport");
    auto *sampleTags = item(scene, "jsonSampleTags");
    auto *engineName = item(scene, "sourceName");
    auto *engineUrl = item(scene, "engineUrl");
    if (!transport || transport->property("currentText").toString() != "JSON по WebSocket" ||
        !engineName || !engineUrl || !engineUrl->property("text").toString().isEmpty() ||
        !sampleTags || sampleTags->property("checked").toBool())
        return fail("Поля WebSocket и необязательная таблица тегов");
    engineName->setProperty("text", "ESP");
    engineUrl->setProperty("text", "ws://127.0.0.1:15115/ws");
    if (!click("sourceSave") || studio.sources().size() != 1
        || studio.sources().first().toMap().value("driver").toString() != "engine-json"
        || !studio.tags().isEmpty() || !item(scene, "sourceExchange", "JSON · WebSocket"))
        return fail("Добавление JSON-источника без загрузки примера тегов");
    auto *sourceExchange = item(scene, "sourceExchange", "JSON · WebSocket");
    auto *exchangeHeading = item(scene, "sourceExchangeHeading");
    if (!exchangeHeading || qAbs(sourceExchange->mapToScene({}).x() - exchangeHeading->mapToScene({}).x()) > 1)
        return fail("Тип обмена расположен под заголовком своей колонки");
    if (!click("engineCatalog") || !click("jsonLoadSampleTags") || studio.tags().size() < 25)
        return fail("Загрузка примера тегов из меню источника");
    capture("json-source.png");
    if (!click("tab1") || item(scene, "screenToolbar"))
        return fail("Полоса экранов скрыта, когда экран один");
    const auto firstScreen = studio.screenId();
    for (int index = 1; index <= 12; ++index)
        studio.setScreen({}, QString("Длинное название экрана %1").arg(index));
    const auto lastScreen = studio.screenId();
    if (!waitFor([&] { return onScreen("screenTab_" + lastScreen); })) {
        auto *viewport = item(scene, "screenTabs");
        auto *last = item(scene, "screenTab_" + lastScreen);
        qWarning() << "Screen tabs geometry" << (viewport ? viewport->property("contentX") : QVariant{})
                   << (viewport ? viewport->property("contentWidth") : QVariant{}) << (viewport ? viewport->width() : 0)
                   << (last ? last->mapToScene(QPointF{}).x() : -1);
        return fail("Прокрутка к последней вкладке экрана");
    }
    studio.selectScreen(firstScreen);
    if (!waitFor([&] { return onScreen("screenTab_" + firstScreen); }))
        return fail("Прокрутка к первой вкладке экрана");
    if (!click("tab0"))
        return fail("Возврат в редактор");
    QTest::keyClick(window, Qt::Key_F11);
    if (!waitFor([&] { return window->visibility() == QWindow::FullScreen && window->property("page").toInt() == 1; }))
        return fail("F11 из редактора открывает операторский режим");
    QTest::keyClick(window, Qt::Key_Escape);
    if (!waitFor([&] { return window->visibility() != QWindow::FullScreen && window->property("page").toInt() == 0; }))
        return fail("Esc возвращает вкладку редактора");
    studio.newProject(true);
    const auto exportedType = studio.faceplates().first().toMap();
    const auto typeFile = QUrl::fromLocalFile(temporary.filePath("portable.faceplate.json"));
    auto *exportPicker = window->findChild<QObject *>("faceplateExportPicker");
    auto *importPicker = window->findChild<QObject *>("faceplateImportPicker");
    const auto prepareTypePicker = [&](QObject *picker) {
        return picker && picker->setProperty("currentFolder", QUrl::fromLocalFile(temporary.path())) &&
            picker->setProperty("selectedFile", typeFile);
    };
    const auto acceptTypePicker = [&](QObject *picker) {
        const bool visible = waitFor([&] { return picker->property("visible").toBool(); });
        const auto selected = picker->property("selectedFile").toUrl();
        if (!visible || selected != typeFile) {
            qWarning() << picker->objectName() << "visible" << visible << "file" << selected;
            return false;
        }
        return QMetaObject::invokeMethod(picker, "accept") &&
            waitFor([&] { return !picker->property("visible").toBool(); });
    };
    if (!prepareTypePicker(exportPicker) ||
        !clickTypeAction("faceplateExport_" + exportedType.value("id").toString()) ||
        !acceptTypePicker(exportPicker) || !QFile::exists(typeFile.toLocalFile()))
        return fail("Экспорт из строки типа сохраняет выбранный файл");
    studio.newProject(false);
    auto *importOptions = window->findChild<QObject *>("faceplateImportOptionsDialog");
    if (!importOptions || !prepareTypePicker(importPicker) || !click("faceplateLibraryImport") ||
        !acceptTypePicker(importPicker) ||
        !waitFor([&] { return item(scene, "faceplateImportName"); }))
        return fail("После выбора файла предлагается имя нового типа");
    auto *importName = item(scene, "faceplateImportName");
    if (importName->property("text") != exportedType.value("name") ||
        !importName->setProperty("text", "Импортированная панель") || !click("faceplateImportApply") ||
        !waitFor([&] { return studio.compositeDepth() == 1 && !item(scene, "faceplateImportApply"); }) ||
        studio.faceplates().size() != 1 || studio.canvasSettings().value("name") != "Импортированная панель" ||
        studio.items().size() != exportedType.value("items").toList().size())
        return fail("Импорт под заданным именем открывает холст нового типа");
    const auto replacementId = studio.canvasSettings().value("id").toString();
    studio.leaveComposite();
    studio.insertFaceplate(replacementId);
    const auto instanceId = studio.selectedId();
    const auto propertyId = studio.faceplates().first().toMap().value("properties").toList()
        .first().toMap().value("id").toString();
    if (instanceId.isEmpty() || !studio.configureWidget({{"overrides", QVariantMap{{propertyId, "Свой заголовок"}}}}))
        return fail("Подготовка вставленного экземпляра для замены типа");
    Studio supplier;
    const auto supplierType = supplier.faceplates().first().toMap();
    const auto supplierId = supplierType.value("id").toString();
    auto supplierProperties = supplierType.value("properties").toList();
    auto renamedField = supplierProperties.first().toMap();
    const auto filePropertyId = renamedField.value("id");
    renamedField["name"] = "Заголовок из файла";
    supplierProperties[0] = renamedField;
    const auto previousTypeFile = QUrl::fromLocalFile(temporary.filePath("previous.faceplate.json"));
    if (!QFile::copy(typeFile.toLocalFile(), previousTypeFile.toLocalFile()) ||
        !supplier.enterComposite(supplierId) || !supplier.setFaceplateProperties(supplierProperties) ||
        !supplier.exportFaceplate(supplierId, typeFile))
        return fail("Подготовка файла с другим именем свойства");
    if (!QMetaObject::invokeMethod(importOptions, "edit", Q_ARG(QVariant, QVariant::fromValue(typeFile))) ||
        !waitFor([&] { return item(scene, "faceplateImportReplaceMode"); }))
        return fail("Импорт из файла открывает диалог замены типа");
    if (!click("faceplateImportReplaceMode") || !selectChoice("faceplateImportTarget", replacementId))
        return fail("Выбор существующего типа в режиме замены");
    auto *replaceApply = item(scene, "faceplateImportApply");
    if (!replaceApply || replaceApply->isEnabled() ||
        !waitFor([&] { return item(scene, "faceplateImportMap_0"); }) ||
        !selectChoice("faceplateImportMap_0", filePropertyId) ||
        !waitFor([&] {
            auto *choice = item(scene, "faceplateImportMap_0");
            return choice && choice->property("currentText") == "Заголовок из файла" && replaceApply->isEnabled();
        }))
        return fail("Ручное сопоставление свойства с переименованным полем файла");
    capture("faceplate-import-mapping.png");
    if (!click("faceplateImportApply") ||
        !waitFor([&] { return studio.compositeDepth() == 1 && !item(scene, "faceplateImportApply"); }) ||
        studio.faceplates().size() != 1 || studio.canvasSettings().value("id") != replacementId ||
        studio.canvasSettings().value("name") != "Импортированная панель")
        return fail("Замена из диалога открывает редактор существующего типа");
    studio.leaveComposite();
    studio.selectWidget(instanceId);
    if (studio.resolveFaceplate(studio.widget()).first().toMap().value("title") != "Свой заголовок")
        return fail("Свойство вставленного экземпляра сохраняется после замены");
    const auto previousFileField = exportedType.value("properties").toList().first().toMap().value("id");
    if (!QMetaObject::invokeMethod(importOptions, "edit", Q_ARG(QVariant, QVariant::fromValue(previousTypeFile))) ||
        !waitFor([&] { return item(scene, "faceplateImportReplaceMode"); }) ||
        !click("faceplateImportReplaceMode") || !selectChoice("faceplateImportTarget", replacementId) ||
        !waitFor([&] {
            auto *choice = item(scene, "faceplateImportMap_0");
            auto *apply = item(scene, "faceplateImportApply");
            return choice && choice->property("currentValue") == previousFileField && apply && apply->isEnabled();
        }) || !click("faceplateImportApply") || !waitFor([&] { return studio.compositeDepth() == 1; }))
        return fail("При повторной замене подставляется запомненное соответствие свойства");
    studio.leaveComposite();
    studio.selectWidget(instanceId);
    if (studio.resolveFaceplate(studio.widget()).first().toMap().value("title") != "Свой заголовок")
        return fail("Повторная замена сохраняет подпись экземпляра");
    for (auto *picker : {exportPicker, importPicker})
        picker->setProperty("currentFolder", QUrl::fromLocalFile(QDir::tempPath()));
    QTest::qWait(100);
    qInfo() << "GUI: viewport, fullscreen, screen properties, widgets, faceplate transfer and replacement, JSON telemetry and archive passed";
    return 0;
}
