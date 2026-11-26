pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "widgetActionsDialog"
    required property var api
    property string kind: ""
    property string defaultTag: ""
    property string error: ""
    readonly property bool analog: ["setpoint", "slider", "knob"].includes(kind)
    readonly property var events: [
        { id: "press", name: qsTr("Нажал") },
        { id: "release", name: qsTr("Отпустил") },
        { id: "click", name: analog ? qsTr("Записал значение") : qsTr("Щёлкнул") }
    ].concat(analog ? [{ id: "change", name: qsTr("Изменил значение") }] : [])
    readonly property var operations: [{ id: "write", name: qsTr("Записать") }, { id: "read", name: qsTr("Прочитать") }]
    readonly property var modes: [
        { id: "constant", name: qsTr("Заданное число") },
        { id: "widget", name: qsTr("Из виджета") },
        { id: "tag", name: qsTr("Из другого тега") }
    ]
    readonly property var unboundTag: ({ id: "", name: qsTr("Без привязки") })
    readonly property var readTags: [unboundTag].concat(api.editorTags.filter(tag =>
        tag.interfacePort ? tag.address?.type !== "text" :
            api.sources.some(source => source.id === tag.source && source.driver === "modbus-tcp")))
    readonly property var numericTags: api.editorTags.filter(tag => tag.address?.type !== "text")
    readonly property var valueTags: [unboundTag].concat(numericTags)
    readonly property var writeTags: [unboundTag].concat(numericTags.map(tag => ({
        id: tag.id,
        name: tag.name + (tag.writable ? "" : qsTr(" · только чтение"))
    })))

    title: qsTr("События и действия")
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: Math.min(930, parent ? parent.width - 32 : 930)

    ListModel { id: rows }

    function edit(widget) {
        kind = widget.kind
        defaultTag = widget.tag || ""
        error = ""
        rows.clear()
        for (const row of widget.actions || []) {
            rows.append({
                eventId: row.event,
                actionId: row.action,
                tagId: row.tag,
                modeId: row.valueMode || "constant",
                sourceTagId: row.valueTag || "",
                numberText: String(row.value ?? 1)
            })
        }
        open()
    }

    function addRow() {
        rows.append({
            eventId: "click",
            actionId: "write",
            tagId: writeTags.some(tag => tag.id === defaultTag) ? defaultTag : "",
            modeId: "constant",
            sourceTagId: "",
            numberText: "1"
        })
        rowsView.positionViewAtEnd()
    }

    function apply() {
        error = ""
        const actions = []
        for (let i = 0; i < rows.count; ++i) {
            const row = rows.get(i)
            const entry = { event: row.eventId, action: row.actionId, tag: row.tagId }
            if (row.actionId === "write") {
                entry.valueMode = row.modeId
                if (row.modeId === "constant") {
                    const number = row.numberText.trim().replace(",", ".")
                    const value = Number(number)
                    if (!number || !Number.isFinite(value) || Math.abs(value) > 1e100) {
                        error = qsTr("Введите число для записи в строке ") + (i + 1) + "."
                        return
                    }
                    entry.value = value
                } else if (row.modeId === "tag") {
                    entry.valueTag = row.sourceTagId
                }
            }
            actions.push(entry)
        }
        if (api.configureWidget({ actions })) close()
    }

    contentItem: ColumnLayout {
        spacing: 8
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: dialog.kind === "slider" || dialog.kind === "knob" ?
                qsTr("Нажатие и отпускание относятся к самому регулятору. «Записал значение» относится к кнопке «Записать».") :
                dialog.kind === "setpoint" ?
                    qsTr("Нажатие, отпускание и запись относятся к кнопке «Записать». «Изменил значение» относится к полю ввода и кнопкам ±.") :
                    qsTr("«Щёлкнул» срабатывает после нажатия и отпускания кнопки.")
            color: "#526579"
        }
        RowLayout {
            Layout.fillWidth: true
            visible: rows.count > 0
            LabelValue { text: qsTr("Событие"); Layout.preferredWidth: 174 }
            LabelValue { text: qsTr("Действие"); Layout.preferredWidth: 112 }
            LabelValue { text: qsTr("Тег действия"); Layout.fillWidth: true }
            LabelValue { text: qsTr("Значение"); Layout.preferredWidth: 143 }
            LabelValue { text: qsTr("Число или тег-источник"); Layout.preferredWidth: 158 }
            Item { Layout.preferredWidth: 103 }
        }
        LabelValue {
            Layout.fillWidth: true
            visible: rows.count === 0
            text: qsTr("Виджет использует встроенное действие. Добавьте строку, чтобы настроить своё.")
            mode: "muted"
        }
        ListView {
            id: rowsView
            visible: rows.count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(360, rows.count * 39)
            clip: true
            spacing: 3
            model: rows
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                id: row
                required property var model
                required property int index
                readonly property var tagChoices: model.actionId === "write" ? dialog.writeTags : dialog.readTags
                width: rowsView.width - 8
                height: 36

                ComboBox {
                    objectName: "widgetActionEvent_" + row.index
                    Layout.preferredWidth: 174
                    model: dialog.events
                    textRole: "name"
                    valueRole: "id"
                    currentIndex: Math.max(0, dialog.events.findIndex(event => event.id === row.model.eventId))
                    onActivated: rows.setProperty(row.index, "eventId", currentValue)
                }
                ComboBox {
                    objectName: "widgetActionType_" + row.index
                    Layout.preferredWidth: 112
                    model: dialog.operations
                    textRole: "name"
                    valueRole: "id"
                    currentIndex: row.model.actionId === "write" ? 0 : 1
                    onActivated: {
                        const operation = currentValue
                        const choices = operation === "write" ? dialog.writeTags : dialog.readTags
                        if (!choices.some(tag => tag.id === row.model.tagId))
                            rows.setProperty(row.index, "tagId", "")
                        rows.setProperty(row.index, "actionId", operation)
                    }
                }
                ComboBox {
                    objectName: "widgetActionTag_" + row.index
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Тег действия")
                    model: row.tagChoices
                    textRole: "name"
                    valueRole: "id"
                    currentIndex: Math.max(0, row.tagChoices.findIndex(tag => tag.id === row.model.tagId))
                    onActivated: rows.setProperty(row.index, "tagId", currentValue)
                    ToolTip.visible: hovered && currentIndex > 0
                    ToolTip.delay: 650
                    ToolTip.text: currentText
                }
                Item {
                    Layout.preferredWidth: 143
                    Layout.preferredHeight: 36
                    ComboBox {
                        objectName: "widgetActionValueMode_" + row.index
                        anchors.fill: parent
                        visible: row.model.actionId === "write"
                        Accessible.name: qsTr("Источник записываемого значения")
                        model: dialog.modes
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: dialog.modes.findIndex(mode => mode.id === row.model.modeId)
                        onActivated: rows.setProperty(row.index, "modeId", currentValue)
                    }
                }
                Item {
                    Layout.preferredWidth: 158
                    Layout.preferredHeight: 36
                    TextField {
                        objectName: "widgetActionValue_" + row.index
                        anchors.fill: parent
                        visible: row.model.actionId === "write" && row.model.modeId === "constant"
                        text: row.model.numberText
                        onTextEdited: rows.setProperty(row.index, "numberText", text)
                        onAccepted: dialog.apply()
                    }
                    ComboBox {
                        objectName: "widgetActionSourceTag_" + row.index
                        anchors.fill: parent
                        visible: row.model.actionId === "write" && row.model.modeId === "tag"
                        Accessible.name: qsTr("Тег-источник значения")
                        model: dialog.valueTags
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: Math.max(0, dialog.valueTags.findIndex(tag => tag.id === row.model.sourceTagId))
                        onActivated: rows.setProperty(row.index, "sourceTagId", currentValue)
                        ToolTip.visible: hovered && currentIndex > 0
                        ToolTip.delay: 650
                        ToolTip.text: currentText
                    }
                }
                ActionButton {
                    objectName: "widgetActionUp_" + row.index
                    text: "↑"
                    compact: true
                    enabled: row.index > 0
                    Accessible.name: qsTr("Переместить выше")
                    onClicked: rows.move(row.index, row.index - 1, 1)
                }
                ActionButton {
                    objectName: "widgetActionDown_" + row.index
                    text: "↓"
                    compact: true
                    enabled: row.index + 1 < rows.count
                    Accessible.name: qsTr("Переместить ниже")
                    onClicked: rows.move(row.index, row.index + 1, 1)
                }
                ActionButton {
                    objectName: "widgetActionRemove_" + row.index
                    text: "×"
                    compact: true
                    Accessible.name: qsTr("Удалить действие")
                    onClicked: rows.remove(row.index)
                }
            }
        }
        LabelValue {
            Layout.fillWidth: true
            visible: dialog.error !== ""
            text: dialog.error
            mode: "error"
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                objectName: "widgetActionAdd"
                text: qsTr("+ Действие")
                enabled: rows.count < 32
                onClicked: dialog.addRow()
            }
            Item { Layout.fillWidth: true }
            ActionButton { text: qsTr("Отмена"); onClicked: dialog.close() }
            ActionButton {
                objectName: "widgetActionApply"
                text: qsTr("Применить")
                highlighted: true
                onClicked: dialog.apply()
            }
        }
    }
}
