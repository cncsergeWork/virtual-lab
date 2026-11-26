pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: table
    required property string choicePrefix
    required property string destinationHeading
    property var rows: []
    property var selection: ({})
    readonly property string problem: {
        const selected = new Set()
        for (const row of rows) {
            const id = selection[row.id]
            if (!id) return qsTr("Выберите поле для «") + row.name + "»."
            if (selected.has(id)) return qsTr("Выберите разные поля назначения.")
            selected.add(id)
        }
        return ""
    }
    readonly property var kindLabels: ({
        number: qsTr("Числовой тег"), bool: qsTr("Логический тег"), text: qsTr("Текстовый тег"),
        title: qsTr("Подпись"), background: qsTr("Цвет фона"), value: qsTr("Цвет значения"),
        captionColor: qsTr("Цвет подписи"), visible: qsTr("Видимость")
    })
    signal edited()
    spacing: 7
    visible: rows.length > 0

    function setFields(fields) {
        const selected = {}
        for (const field of fields) selected[field.id] = field.automatic
        selection = selected
        rows = fields
    }
    function selectField(id, choice) {
        const selected = Object.assign({}, selection)
        selected[id] = choice
        selection = selected
        edited()
    }
    RowLayout {
        Layout.fillWidth: true
        LabelValue { text: qsTr("Используемое поле"); mode: "strong"; Layout.preferredWidth: 224 }
        LabelValue { text: table.destinationHeading; mode: "strong"; Layout.fillWidth: true }
    }
    ListView {
        id: mappingList
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(0, Math.min(310, count * 47 - 4))
        clip: true
        spacing: 4
        model: table.rows
        ScrollBar.vertical: ScrollBar {}
        delegate: RowLayout {
            id: mappingRow
            required property var modelData
            required property int index
            width: mappingList.width - 10
            height: 43
            ColumnLayout {
                Layout.preferredWidth: 224
                Layout.maximumWidth: 224
                Layout.minimumWidth: 0
                spacing: 0
                LabelValue {
                    text: mappingRow.modelData.name
                    Layout.fillWidth: true
                    ToolTip.visible: truncated && mappingHover.hovered
                    ToolTip.text: text
                    HoverHandler { id: mappingHover }
                }
                LabelValue {
                    text: table.kindLabels[mappingRow.modelData.kind] ?? ""
                    mode: "muted"
                }
            }
            ComboBox {
                objectName: table.choicePrefix + mappingRow.index
                Accessible.name: table.destinationHeading + qsTr(" для ") + mappingRow.modelData.name
                Layout.fillWidth: true
                model: [{ id: "", name: mappingRow.modelData.choices.length ? qsTr("Выберите поле") :
                    qsTr("Нет совместимых полей") }].concat(mappingRow.modelData.choices)
                textRole: "name"
                valueRole: "id"
                currentIndex: mappingRow.modelData.choices.findIndex(choice =>
                    choice.id === table.selection[mappingRow.modelData.id]) + 1
                onActivated: table.selectField(mappingRow.modelData.id, currentValue)
            }
        }
    }
}
