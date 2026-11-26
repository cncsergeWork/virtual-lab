import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: panel
    required property var api
    readonly property var item: api.widget
    readonly property string kind: item.kind || ""
    readonly property var faceplateType: kind === "faceplate" ? api.faceplates.find(type => type.id === item.faceplate) : null
    readonly property bool canCreateFaceplate: api.compositeDepth === 0 && api.selectedIds.length > 0 &&
        !api.items.some(entry => entry.kind === "faceplate" && api.selectedIds.includes(entry.id))
    readonly property bool industrial: ["dial", "column", "ring", "segments", "signal", "compass", "bargraph"].includes(kind)
    readonly property bool analogControl: ["setpoint", "slider", "knob"].includes(kind)
    readonly property bool binaryControl: ["switch", "valve"].includes(kind)
    readonly property bool writeControl: analogControl || binaryControl || ["selector", "command", "hitArea"].includes(kind)
    readonly property var shapes: [{ id: "rectangle", label: "Прямоугольник" },
        { id: "ellipse", label: "Эллипс" }, { id: "line", label: "Линия" }]
    readonly property bool rangeWidget: analogControl || ["gauge", "tank", "dial", "column", "ring", "bargraph"].includes(kind)
    readonly property var tagChoices: [{ id: "", name: "Выбрать тег" }].concat(api.editorTags.filter(tag => {
        const type = tag.address?.type
        if (binaryControl) return type === "bool"
        if (analogControl || kind === "selector") return type !== "bool" && type !== "text"
        return kind === "value" || type !== "text"
    }))
    readonly property var feedbackChoices: [{ id: "", name: "По основному тегу" }].concat(api.editorTags.filter(tag => tag.address?.type === "bool"))
    signal applied()
    color: "#ffffff"
    border.color: "#d7e0e8"
    radius: 8
    function sync() {
        titleField.text = panel.item.title ?? ""
        minField.text = String(panel.item.min ?? 0)
        maxField.text = String(panel.item.max ?? 100)
        commandField.text = String(panel.item.command ?? 1)
        stepField.text = panel.item.step === undefined ? "" : String(panel.item.step)
        direction.currentIndex = panel.item.vertical ? 1 : 0
        shapeType.currentIndex = Math.max(0, shapeType.indexOfValue(panel.item.shape || "rectangle"))
        visibleByDefault.checked = panel.item.visible !== false
        precisionField.text = String(panel.item.decimals ?? (panel.kind === "compass" ? 0 : 1))
        accentField.text = String(panel.item.accent ?? "#52d6bf")
        xField.text = String(Math.round(panel.item.x ?? 0))
        yField.text = String(Math.round(panel.item.y ?? 0))
        wField.text = String(Math.round(panel.item.w ?? 80))
        hField.text = String(Math.round(panel.item.h ?? 52))
        Qt.callLater(syncBindings)
    }
    function syncBindings() {
        tags.currentIndex = Math.max(0, tags.indexOfValue(panel.item.tag))
        feedback.currentIndex = Math.max(0, feedback.indexOfValue(panel.item.feedback))
        screens.currentIndex = Math.max(0, screens.indexOfValue(panel.item.screen))
    }
    Component.onCompleted: sync()
    onItemChanged: sync()
    function apply() {
        const data = { title: titleField.text }
        if (tags.visible) data.tag = tags.currentValue
        if (screens.visible) data.screen = screens.currentValue
        if (rangeWidget) {
            data.min = Number(minField.text.replace(",", "."))
            data.max = Number(maxField.text.replace(",", "."))
        }
        if (industrial) {
            data.accent = accentField.text.trim()
            if (kind !== "signal") data.decimals = Number(precisionField.text)
        }
        if (analogControl && (stepField.text.trim() || panel.item.step !== undefined)) {
            const step = stepField.text.trim().replace(",", ".")
            data.step = !step ? null : Number.isFinite(Number(step)) ? Number(step) : step
        }
        if (analogControl) data.decimals = Number(precisionField.text)
        if (binaryControl) data.feedback = feedback.currentValue
        if (kind === "pipe" || kind === "shape") data.vertical = direction.currentIndex === 1
        if (kind === "shape") data.shape = shapeType.currentValue
        if (api.compositeDepth) data.visible = visibleByDefault.checked
        if (kind === "command" || kind === "hitArea") data.command = Number(commandField.text.replace(",", "."))
        if (api.configureWidget(data))
            panel.applied()
    }
    ScrollView {
        anchors.fill: parent
        anchors.margins: 13
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: parent.width
            spacing: 10
            LabelValue {
                text: panel.api.selectedIds.length > 1 ? "Выбрано элементов: " + panel.api.selectedIds.length : "Свойства элемента"
                mode: "strong"; font.pixelSize: 15; Layout.fillWidth: true
            }
            Label {
                visible: panel.kind === ""
                text: "Выберите элемент на экране."
                color: "#5d6d80"; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            ColumnLayout {
                visible: panel.kind !== ""
                Layout.fillWidth: true
                ActionButton {
                    objectName: "faceplateCreate"
                    visible: panel.canCreateFaceplate
                    Layout.fillWidth: true
                    text: "Создать тип из выбранных…"
                    onClicked: faceplateCreateDialog.edit("", "")
                }
                Field { id: titleField; objectName: "widgetTitle"; caption: "Подпись"; onAccepted: panel.apply() }
                CheckBox {
                    id: visibleByDefault
                    visible: panel.api.compositeDepth > 0
                    text: "Виден без условия"
                    onToggled: panel.api.configureWidget({ visible: checked })
                }
                ComboBox {
                    id: shapeType
                    objectName: "widgetShape"
                    visible: panel.kind === "shape"
                    Layout.fillWidth: true
                    model: panel.shapes; textRole: "label"; valueRole: "id"
                    onActivated: panel.api.configureWidget({ shape: currentValue })
                }
                LabelValue {
                    visible: !!panel.faceplateType
                    Layout.fillWidth: true
                    mode: "muted"
                    text: panel.faceplateType ? "Тип · " + panel.faceplateType.name : ""
                }
                ActionButton {
                    objectName: "faceplateBindings"
                    visible: !!panel.faceplateType
                    Layout.fillWidth: true
                    text: "Привязки тегов…"
                    onClicked: faceplateBindings.edit(panel.item, panel.faceplateType)
                }
                ActionButton {
                    objectName: "faceplateInstanceProperties"
                    visible: !!panel.faceplateType
                    Layout.fillWidth: true
                    text: "Свойства экземпляра…"
                    onClicked: faceplateProperties.edit(panel.item, panel.faceplateType)
                }
                ActionButton {
                    objectName: "faceplateOpenType"
                    visible: !!panel.faceplateType
                    Layout.fillWidth: true
                    text: "Открыть тип"
                    onClicked: panel.api.enterComposite(panel.item.id)
                }
                ActionButton {
                    objectName: "faceplateChangeType"
                    visible: !!panel.faceplateType
                    Layout.fillWidth: true
                    text: "Сменить тип…"
                    onClicked: changeTypeDialog.edit(panel.item, panel.faceplateType)
                }
                ActionButton {
                    objectName: "faceplateCopyType"
                    visible: !!panel.faceplateType
                    Layout.fillWidth: true
                    text: "Создать копию типа…"
                    onClicked: faceplateCreateDialog.edit(panel.item.id,
                        panel.api.suggestFaceplateCopyName(panel.faceplateType.id))
                }
                LabelValue {
                    visible: tags.visible
                    text: panel.kind === "command" ? "Тег команды" :
                        panel.writeControl ? "Тег показания" : "Привязка к тегу"
                    mode: "muted"
                }
                ComboBox {
                    id: tags
                    objectName: "widgetTag"
                    Layout.fillWidth: true
                    visible: panel.industrial || panel.writeControl ||
                        ["value", "gauge", "indicator", "tank", "pump", "motor", "pipe", "alarm"].includes(panel.kind)
                    model: panel.tagChoices
                    textRole: "name"; valueRole: "id"
                }
                LabelValue { visible: panel.binaryControl; text: "Обратная связь"; mode: "muted" }
                ComboBox {
                    id: feedback
                    visible: panel.binaryControl; Layout.fillWidth: true
                    model: panel.feedbackChoices
                    textRole: "name"; valueRole: "id"
                }
                ActionButton {
                    objectName: "editSelectorPositions"
                    visible: panel.kind === "selector"; Layout.fillWidth: true
                    text: "Положения и значения…"
                    onClicked: selectorDialog.edit(panel.item.positions)
                }
                LabelValue { visible: direction.visible; text: "Направление трубы"; mode: "muted" }
                ComboBox {
                    id: direction
                    visible: panel.kind === "pipe" || (panel.kind === "shape" && shapeType.currentValue === "line")
                    Layout.fillWidth: true
                    model: ["Горизонтальное", "Вертикальное"]
                }
                ActionButton {
                    objectName: "editTrend"
                    visible: panel.kind === "trend"
                    Layout.fillWidth: true
                    text: "Ряды, шкалы и период…"
                    onClicked: trendDialog.edit(panel.item)
                }
                LabelValue { visible: screens.visible; text: "Экран назначения"; mode: "muted" }
                ComboBox {
                    id: screens
                    Layout.fillWidth: true
                    visible: panel.kind === "link"
                    model: [{ id: "", name: "Выбрать экран" }].concat(panel.api.screens)
                    textRole: "name"; valueRole: "id"
                }
                RowLayout {
                    visible: panel.rangeWidget
                    Field { id: minField; caption: "Минимум" }
                    Field { id: maxField; caption: "Максимум" }
                }
                RowLayout {
                    visible: panel.industrial || panel.analogControl
                    Field { id: precisionField; caption: "Знаков после запятой"; visible: panel.kind !== "signal" }
                    Field { id: accentField; caption: "Цвет · #RRGGBB"; visible: panel.industrial }
                }
                Field { id: stepField; visible: panel.analogControl; caption: "Шаг изменения"; placeholderText: "Авто" }
                Field {
                    id: commandField
                    visible: panel.kind === "command" || panel.kind === "hitArea"
                    caption: panel.kind === "hitArea" ? "Значение зоны" : "Значение для записи"
                }
                ActionButton {
                    objectName: "widgetActions"
                    visible: panel.writeControl
                    Layout.fillWidth: true
                    text: "События и действия…" + (panel.item.actions?.length ? " (" + panel.item.actions.length + ")" : "")
                    onClicked: actionsDialog.edit(panel.item)
                }
                ActionButton {
                    objectName: "chooseWidgetImage"
                    visible: panel.kind === "image"
                    text: "Выбрать изображение…"
                    Layout.fillWidth: true
                    onClicked: imagePicker.open()
                }
                ActionButton {
                    objectName: "editAppearance"
                    text: panel.api.compositeDepth ? "Оформление и условия…" : "Цвета и пороги…"
                    Layout.fillWidth: true
                    onClicked: appearanceDialog.start(panel.item)
                }
                ActionButton {
                    objectName: "editLayerAnimation"
                    visible: panel.api.compositeDepth > 0 && panel.kind !== "hitArea"
                    text: panel.item.animation?.kind === "rotate" ? "Анимация · вращение…" :
                        panel.item.animation?.kind === "blink" ? "Анимация · мигание…" : "Анимация слоя…"
                    Layout.fillWidth: true
                    onClicked: animationDialog.edit(panel.item)
                }
                ActionButton {
                    objectName: "applyWidget"
                    Layout.fillWidth: true
                    text: "Применить свойства"
                    highlighted: true
                    onClicked: panel.apply()
                }
                Rectangle { color: "#e1e8ef"; height: 1; Layout.fillWidth: true; Layout.topMargin: 7 }
                RowLayout {
                    Field { id: xField; objectName: "widgetX"; caption: "X" }
                    Field { id: yField; objectName: "widgetY"; caption: "Y" }
                }
                RowLayout {
                    Field { id: wField; caption: "Ширина" }
                    Field { id: hField; caption: "Высота" }
                }
                ActionButton {
                    objectName: "widgetApplyPosition"
                    text: "Применить положение"; Layout.fillWidth: true
                    enabled: !panel.item.locked
                    onClicked: panel.api.moveWidget(panel.api.selectedId, Number(xField.text), Number(yField.text),
                                                     Number(wField.text), Number(hField.text))
                }
                RowLayout {
                    Layout.fillWidth: true
                    ActionButton { objectName: "widgetSendBack"; text: "На задний план"; Layout.fillWidth: true; onClicked: panel.api.layerWidget(false) }
                    ActionButton { objectName: "widgetBringFront"; text: "Вперёд"; onClicked: panel.api.layerWidget(true) }
                }
                RowLayout {
                    Layout.fillWidth: true
                    ActionButton { objectName: "widgetDuplicate"; text: "Копировать"; Layout.fillWidth: true; onClicked: panel.api.duplicateWidget() }
                    ActionButton { objectName: "widgetRemove"; text: "Удалить"; onClicked: panel.api.removeWidget() }
                }
                RowLayout {
                    visible: panel.api.selectedIds.length > 1
                    Layout.fillWidth: true
                    ActionButton { objectName: "widgetAlignLeft"; text: "По левому краю"; Layout.fillWidth: true; onClicked: panel.api.alignWidgets(false) }
                    ActionButton { objectName: "widgetAlignTop"; text: "По верху"; Layout.fillWidth: true; onClicked: panel.api.alignWidgets(true) }
                }
                ActionButton {
                    objectName: "groupWidgets"
                    visible: panel.api.selectedIds.length > 1 || !!panel.item.group
                    text: panel.item.group ? "Разгруппировать" : "Сгруппировать"
                    Layout.fillWidth: true
                    onClicked: panel.api.groupWidgets(!panel.item.group)
                }
                ActionButton {
                    objectName: "createTemplate"
                    text: "Сохранить как шаблон…"
                    Layout.fillWidth: true
                    onClicked: { templateName.clear(); templateDialog.open(); templateName.forceActiveFocus() }
                }
            }
        }
    }
    FileDialog {
        id: imagePicker
        objectName: "widgetImagePicker"
        title: "Выбрать изображение"
        nameFilters: ["Изображения (*.png *.jpg *.jpeg *.bmp *.svg)"]
        onAccepted: panel.api.setImage(selectedFile)
    }
    TrendDialog { id: trendDialog; api: panel.api }
    SelectorDialog { id: selectorDialog; api: panel.api }
    WidgetActionsDialog { id: actionsDialog; api: panel.api }
    AppearanceDialog { id: appearanceDialog; api: panel.api }
    LayerAnimationDialog { id: animationDialog; api: panel.api }
    FaceplateBindingsDialog { id: faceplateBindings; api: panel.api }
    FaceplateInstancePropertiesDialog { id: faceplateProperties; api: panel.api }
    Dialog {
        id: changeTypeDialog
        title: "Сменить тип фейсплейса"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(660, parent ? parent.width - 32 : 660)
        property string instanceId: ""
        property string sourceId: ""
        property string sourceName: ""
        property string errorText: ""
        property bool applying: false
        readonly property var alternatives: panel.api.faceplates.filter(type => type.id !== sourceId)
        readonly property bool canApply: !!typeTarget.currentValue && changeMapping.problem === ""
        function edit(instance, type) {
            instanceId = instance.id
            sourceId = type.id
            sourceName = type.name
            typeTarget.currentIndex = 0
            refreshMapping()
            open()
        }
        function refreshMapping() {
            if (!changeMapping) return
            const fields = typeTarget.currentValue ?
                panel.api.previewFaceplateTypeChange(instanceId, typeTarget.currentValue).fields : []
            changeMapping.setFields(fields)
            errorText = ""
        }
        function applyChange() {
            if (!canApply) return
            errorText = ""
            applying = true
            const saved = panel.api.changeFaceplateType(instanceId, typeTarget.currentValue, changeMapping.selection)
            applying = false
            if (saved) close()
        }
        Connections {
            target: panel.api
            enabled: changeTypeDialog.applying
            function onNotice(message, error) {
                if (error) changeTypeDialog.errorText = message
            }
        }
        contentItem: ColumnLayout {
            spacing: 10
            LabelValue { text: "Текущий тип · " + changeTypeDialog.sourceName; Layout.fillWidth: true }
            ComboBox {
                id: typeTarget
                objectName: "faceplateChangeTarget"
                Accessible.name: "Новый тип фейсплейса"
                visible: changeTypeDialog.alternatives.length > 0
                Layout.fillWidth: true
                model: [{ id: "", name: "Выберите тип" }].concat(changeTypeDialog.alternatives)
                textRole: "name"
                valueRole: "id"
                onCurrentValueChanged: changeTypeDialog.refreshMapping()
            }
            FaceplateFieldMapping {
                id: changeMapping
                choicePrefix: "faceplateChangeMap_"
                destinationHeading: "Поле нового типа"
                Layout.fillWidth: true
                onEdited: changeTypeDialog.errorText = ""
            }
            Label {
                objectName: "faceplateChangeStatus"
                visible: text !== ""
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: changeTypeDialog.alternatives.length ? "#873831" : "#506479"
                text: !changeTypeDialog.alternatives.length ? "Сначала создайте копию в списке «Фейсплейсы»." :
                    changeTypeDialog.errorText || changeMapping.problem
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: "Отмена"; onClicked: changeTypeDialog.close() }
                ActionButton {
                    objectName: "faceplateChangeApply"
                    text: "Сменить тип"
                    enabled: changeTypeDialog.canApply
                    highlighted: true
                    onClicked: changeTypeDialog.applyChange()
                }
            }
        }
    }
    Dialog {
        id: faceplateCreateDialog
        objectName: "faceplateCreateDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 420
        modal: true
        focus: true
        property string copyInstanceId: ""
        title: copyInstanceId ? "Создать копию типа" : "Новый тип составного виджета"
        onOpened: {
            faceplateName.forceActiveFocus()
            if (copyInstanceId) faceplateName.selectAll()
        }
        function edit(instanceId, name) {
            copyInstanceId = instanceId
            faceplateName.text = name
            open()
        }
        function save() {
            const name = faceplateName.text.trim()
            if (!name) return
            if (copyInstanceId ? panel.api.duplicateFaceplateForInstance(copyInstanceId, name) :
                panel.api.createFaceplate(name))
                close()
        }
        contentItem: ColumnLayout {
            TextField {
                id: faceplateName
                objectName: faceplateCreateDialog.copyInstanceId ? "faceplateCopyName" : "faceplateName"
                placeholderText: "Название типа"
                Accessible.name: placeholderText
                Layout.fillWidth: true
                onAccepted: if (text.trim()) faceplateCreateDialog.save()
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton {
                    objectName: "faceplateNameCancel"
                    text: "Отмена"
                    onClicked: faceplateCreateDialog.close()
                }
                ActionButton {
                    objectName: faceplateCreateDialog.copyInstanceId ? "faceplateCopyApply" : "faceplateCreateApply"
                    text: faceplateCreateDialog.copyInstanceId ? "Создать копию" : "Создать"
                    enabled: faceplateName.text.trim() !== ""
                    highlighted: true
                    onClicked: faceplateCreateDialog.save()
                }
            }
        }
    }
    Dialog {
        id: templateDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 400
        modal: true
        title: "Новый шаблон элементов"
        function save() { if (panel.api.saveTemplate(templateName.text)) close() }
        contentItem: ColumnLayout {
            TextField {
                id: templateName
                objectName: "templateName"
                placeholderText: "Название шаблона"
                Layout.fillWidth: true
                onAccepted: templateDialog.save()
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: "Отмена"; onClicked: templateDialog.close() }
                ActionButton { objectName: "templateSave"; text: "Сохранить"; highlighted: true; onClicked: templateDialog.save() }
            }
        }
    }
}
