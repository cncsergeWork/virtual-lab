import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: form
    required property var api
    property var widget: ({})
    property int revision: 0
    property var colorTarget: null
    readonly property var operators: [{ name: qsTr("Меньше"), id: "lt" }, { name: qsTr("Меньше или равно"), id: "lte" },
        { name: qsTr("Равно"), id: "eq" }, { name: qsTr("Не равно"), id: "ne" }, { name: qsTr("Больше или равно"), id: "gte" },
        { name: qsTr("Больше"), id: "gt" }, { name: qsTr("В диапазоне"), id: "between" }, { name: qsTr("Вне диапазона"), id: "outside" }]
    readonly property var choices: [{ id: "", name: qsTr("Тег виджета") }].concat(api.editorTags.filter(t => t.address?.type !== "text"))
    readonly property var testReading: {
        if (sampleField.text.trim()) {
            const n = number(sampleField.text)
            return isFinite(n) ? { quality: "good", hasValue: true, value: n } : {}
        }
        return api.readings[trigger.currentValue || widget.tag || widget.trend?.series?.find(row => row.tag)?.tag] || {}
    }
    readonly property var preview: revision >= 0 ? api.widgetColors(options(), testReading) : {}
    readonly property bool layerRules: api.compositeDepth > 0 ||
        (widget.appearance?.rules || []).some(rule => rule.visible !== undefined || rule.text !== undefined)
    readonly property var visibilityOptions: [qsTr("Как обычно"), qsTr("Показать"), qsTr("Скрыть")]
    title: layerRules ? qsTr("Оформление и условия слоя") : qsTr("Цвета и пороги")
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(layerRules ? 1120 : 934, parent ? parent.width - 32 : 934)

    ListModel { id: entries }
    function number(text) { return text.trim() ? Number(text.trim().replace(",", ".")) : NaN }
    function valid(color) { return /^#[A-Fa-f0-9]{6}$/.test(color || "") }
    function add(rule) {
        entries.append({ op: rule.op || "gte", low: rule.limit === undefined ? "" : String(rule.limit),
            high: rule.max === undefined ? "" : String(rule.max), bg: rule.background || "", fg: rule.value || "",
            visibility: rule.visible === undefined ? 0 : rule.visible ? 1 : 2, caption: rule.text ?? "" })
        ++revision
    }
    function replace(index, key, value) {
        entries.setProperty(index, key, value)
        ++revision
    }
    function move(index, target) {
        entries.move(index, target, 1)
        ++revision
    }
    function remove(index) {
        entries.remove(index)
        ++revision
    }
    function start(item) {
        widget = item
        const existing = item.appearance || {}
        baseBackground.text = existing.background || ""
        baseValue.text = existing.value || ""
        sampleField.text = ""
        trigger.currentIndex = Math.max(0, choices.findIndex(tag => tag.id === (existing.tag || "")))
        entries.clear()
        for (const rule of existing.rules || []) add(rule)
        ++revision
        open()
    }
    function options() {
        const data = { rules: [] }
        if (trigger.currentValue) data.tag = trigger.currentValue
        if (baseBackground.text.trim()) data.background = baseBackground.text.trim()
        if (baseValue.text.trim()) data.value = baseValue.text.trim()
        for (let index = 0; index < entries.count; ++index) {
            const row = entries.get(index)
            const rule = { op: row.op, limit: number(row.low) }
            if (row.op === "between" || row.op === "outside") rule.max = number(row.high)
            if (row.bg.trim()) rule.background = row.bg.trim()
            if (row.fg.trim()) rule.value = row.fg.trim()
            if (row.visibility > 0) rule.visible = row.visibility === 1
            if (row.caption.trim()) rule.text = row.caption
            data.rules.push(rule)
        }
        return data
    }
    function pick(field) {
        colorTarget = field
        colorPicker.selectedColor = valid(field.text.trim()) ? field.text.trim() : "#ffffff"
        colorPicker.open()
    }
    function rgb(color) {
        return "#" + [color.r, color.g, color.b].map(channel =>
            Math.round(channel * 255).toString(16).padStart(2, "0")).join("")
    }
    function save() {
        if (api.configureWidget({ appearance: options() })) close()
    }
    ColorDialog {
        id: colorPicker
        objectName: "appearanceColorPicker"
        title: qsTr("Выбрать цвет")
        onAccepted: {
            if (form.colorTarget) form.colorTarget.setColor(form.rgb(selectedColor))
            form.colorTarget = null
        }
        onRejected: form.colorTarget = null
    }

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.preferredWidth: 164
                LabelValue { text: qsTr("Фон"); mode: "strong" }
                ColorField {
                    id: baseBackground
                    objectName: "appearanceBackground"
                    caption: qsTr("Цвет фона")
                    Layout.fillWidth: true
                    onChoose: field => form.pick(field)
                }
            }
            ColumnLayout {
                Layout.preferredWidth: 164
                LabelValue { text: qsTr("Показание"); mode: "strong" }
                ColorField {
                    id: baseValue
                    objectName: "appearanceValue"
                    caption: qsTr("Цвет показания")
                    Layout.fillWidth: true
                    onChoose: field => form.pick(field)
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                LabelValue { text: qsTr("Тег для условий"); mode: "strong" }
                ComboBox {
                    id: trigger
                    objectName: "appearanceTag"
                    Layout.fillWidth: true
                    model: form.choices; textRole: "name"; valueRole: "id"
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Field {
                id: sampleField
                objectName: "appearanceSample"
                caption: qsTr("Пробное значение")
                placeholderText: qsTr("Пусто — текущее показание")
                Layout.preferredWidth: 236
                Layout.fillWidth: false
            }
            Rectangle {
                objectName: "appearancePreview"
                Layout.fillWidth: true
                Layout.minimumWidth: 150
                Layout.preferredHeight: 46
                Layout.alignment: Qt.AlignBottom
                color: form.valid(form.preview.background) ? form.preview.background : "#ffffff"
                radius: 5; border.color: "#d7e0e8"
                LabelValue {
                    id: sampleLabel
                    anchors.centerIn: parent
                    text: form.preview.visible === false ? qsTr("Слой скрыт") :
                        form.preview.text ?? (form.testReading.hasValue ? String(form.testReading.value) : qsTr("Пример показания"))
                    color: form.valid(form.preview.value) ? form.preview.value :
                        ((parent.color.r * 0.2126 + parent.color.g * 0.7152 + parent.color.b * 0.0722) < 0.55 ? "#edf4fa" : "#13263b")
                    font.weight: Font.DemiBold
                    font.pixelSize: 18
                }
            }
        }
        LabelValue {
            text: qsTr("Правила проверяются сверху вниз. Срабатывает первое подходящее.")
            mode: "muted"; Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            LabelValue { text: qsTr("Условие"); Layout.preferredWidth: form.layerRules ? 143 : 176 }
            LabelValue { text: qsTr("Порог"); Layout.preferredWidth: form.layerRules ? 67 : 85 }
            LabelValue { text: form.layerRules ? qsTr("До") : qsTr("Верхняя граница"); Layout.preferredWidth: form.layerRules ? 67 : 105 }
            LabelValue { text: qsTr("Фон"); Layout.fillWidth: true; Layout.preferredWidth: 115 }
            LabelValue { text: qsTr("Показание"); Layout.fillWidth: true; Layout.preferredWidth: 115 }
            LabelValue { visible: form.layerRules; text: qsTr("Видимость"); Layout.preferredWidth: 120 }
            LabelValue { visible: form.layerRules; text: qsTr("Подпись"); Layout.fillWidth: true; Layout.preferredWidth: 125 }
            Item { Layout.preferredWidth: 118 }
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(265, Math.max(47, count * 41))
            model: entries
            clip: true
            spacing: 5
            delegate: RowLayout {
                id: row
                required property int index
                required property var model
                readonly property bool interval: model.op === "between" || model.op === "outside"
                width: ListView.view.width
                height: 36
                ComboBox {
                    objectName: "appearanceOp_" + row.index
                    Layout.preferredWidth: form.layerRules ? 143 : 176
                    model: form.operators; textRole: "name"; valueRole: "id"
                    currentIndex: form.operators.findIndex(condition => condition.id === row.model.op)
                    onActivated: form.replace(row.index, "op", currentValue)
                }
                TextField {
                    objectName: "appearanceLimit_" + row.index
                    Layout.preferredWidth: form.layerRules ? 67 : 85
                    Layout.preferredHeight: 33
                    placeholderText: qsTr("Число")
                    text: row.model.low
                    onTextEdited: form.replace(row.index, "low", text)
                }
                TextField {
                    objectName: "appearanceMax_" + row.index
                    Layout.preferredWidth: form.layerRules ? 67 : 105
                    Layout.preferredHeight: 33
                    enabled: row.interval
                    placeholderText: row.interval ? qsTr("Число") : "—"
                    text: row.interval ? row.model.high : ""
                    onTextEdited: form.replace(row.index, "high", text)
                }
                ColorField {
                    objectName: "appearanceRuleBackground_" + row.index
                    Layout.fillWidth: true
                    Layout.preferredWidth: 115
                    text: row.model.bg
                    hint: qsTr("Основной")
                    caption: qsTr("Цвет фона по правилу")
                    onChoose: field => form.pick(field)
                    onEdited: value => form.replace(row.index, "bg", value)
                }
                ColorField {
                    objectName: "appearanceRuleValue_" + row.index
                    Layout.fillWidth: true
                    Layout.preferredWidth: 115
                    text: row.model.fg
                    hint: qsTr("Основной")
                    caption: qsTr("Цвет показания по правилу")
                    onChoose: field => form.pick(field)
                    onEdited: value => form.replace(row.index, "fg", value)
                }
                ComboBox {
                    objectName: "appearanceRuleVisibility_" + row.index
                    visible: form.layerRules
                    Layout.preferredWidth: 120
                    model: form.visibilityOptions
                    currentIndex: row.model.visibility
                    onActivated: form.replace(row.index, "visibility", currentIndex)
                }
                TextField {
                    objectName: "appearanceRuleText_" + row.index
                    visible: form.layerRules
                    Layout.fillWidth: true; Layout.preferredWidth: 125
                    placeholderText: qsTr("Основная")
                    maximumLength: 240
                    text: row.model.caption
                    onTextEdited: form.replace(row.index, "caption", text)
                }
                ActionButton {
                    objectName: "appearanceRuleUp_" + row.index
                    text: "↑"; enabled: row.index > 0
                    onClicked: form.move(row.index, row.index - 1)
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Выше в списке")
                }
                ActionButton {
                    objectName: "appearanceRuleDown_" + row.index
                    text: "↓"; enabled: row.index + 1 < list.count
                    onClicked: form.move(row.index, row.index + 1)
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Ниже в списке")
                }
                ActionButton {
                    objectName: "appearanceRuleRemove_" + row.index
                    text: "×"; onClicked: form.remove(row.index)
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Удалить правило")
                }
            }
            ScrollBar.vertical: ScrollBar {}
            LabelValue {
                anchors.centerIn: parent
                visible: list.count === 0
                text: qsTr("Можно оставить только основные цвета.")
                mode: "muted"
            }
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                objectName: "appearanceAdd"
                enabled: entries.count < 32
                text: qsTr("+ Правило")
                onClicked: { form.add({}); list.positionViewAtEnd() }
            }
            Item { Layout.fillWidth: true }
            ActionButton { text: qsTr("Отмена"); onClicked: form.close() }
            ActionButton { objectName: "appearanceSave"; text: qsTr("Применить"); highlighted: true; onClicked: form.save() }
        }
    }
}
