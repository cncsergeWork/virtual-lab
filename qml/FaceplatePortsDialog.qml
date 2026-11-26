pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "faceplatePortsDialog"
    required property var api
    readonly property var types: [
        { id: "number", name: qsTr("Числовой") },
        { id: "bool", name: qsTr("Логический") },
        { id: "text", name: qsTr("Текстовый") }
    ]
    property string typeName: ""
    title: qsTr("Интерфейс тегов")
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    width: Math.min(650, parent ? parent.width - 32 : 650)

    ListModel { id: ports }

    function edit(type) {
        if (!type) return
        typeName = type.name
        ports.clear()
        for (const port of type.ports || [])
            ports.append({ portId: port.id, portName: port.name, typeId: port.type })
        open()
    }

    function save() {
        const values = []
        for (let index = 0; index < ports.count; ++index) {
            const port = ports.get(index)
            values.push({ id: port.portId, name: port.portName.trim(), type: port.typeId })
        }
        if (api.setFaceplatePorts(values)) close()
    }

    contentItem: ColumnLayout {
        spacing: 8
        LabelValue {
            text: dialog.typeName
            mode: "strong"
            Layout.fillWidth: true
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Внутренние элементы используют теги интерфейса. Теги проекта выбираются у каждого экземпляра.")
            color: "#526579"
            wrapMode: Text.WordWrap
        }
        RowLayout {
            visible: ports.count > 0
            Layout.fillWidth: true
            LabelValue { text: qsTr("Тег интерфейса"); Layout.fillWidth: true }
            LabelValue { text: qsTr("Тип данных"); Layout.preferredWidth: 146 }
            Item { Layout.preferredWidth: 38 }
        }
        LabelValue {
            visible: ports.count === 0
            text: qsTr("Добавьте тег интерфейса, чтобы привязать данные к виджету.")
            mode: "muted"
            Layout.fillWidth: true
        }
        ListView {
            id: portList
            visible: count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(320, count * 41)
            model: ports
            clip: true
            spacing: 5
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                id: row
                required property int index
                required property var model
                width: portList.width - 8
                height: 36
                TextField {
                    objectName: "faceplatePortName_" + row.index
                    Accessible.name: qsTr("Имя тега интерфейса")
                    Layout.fillWidth: true
                    placeholderText: qsTr("Например, Давление")
                    text: row.model.portName
                    onTextChanged: if (text !== row.model.portName)
                        ports.setProperty(row.index, "portName", text)
                }
                ComboBox {
                    objectName: "faceplatePortType_" + row.index
                    Accessible.name: qsTr("Тип данных тега интерфейса")
                    Layout.preferredWidth: 146
                    model: dialog.types
                    textRole: "name"
                    valueRole: "id"
                    currentIndex: dialog.types.findIndex(type => type.id === row.model.typeId)
                    onActivated: ports.setProperty(row.index, "typeId", currentValue)
                }
                ActionButton {
                    compact: true
                    text: "×"
                    objectName: "faceplatePortRemove_" + row.index
                    Accessible.name: qsTr("Удалить тег интерфейса")
                    onClicked: ports.remove(row.index)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                objectName: "faceplatePortAdd"
                text: qsTr("+ Тег интерфейса")
                onClicked: {
                    ports.append({ portId: "", portName: "", typeId: "number" })
                    portList.positionViewAtEnd()
                }
            }
            Item { Layout.fillWidth: true }
            ActionButton { objectName: "faceplatePortsCancel"; text: qsTr("Отмена"); onClicked: dialog.close() }
            ActionButton {
                objectName: "faceplatePortsApply"
                text: qsTr("Применить")
                highlighted: true
                onClicked: dialog.save()
            }
        }
    }
}
