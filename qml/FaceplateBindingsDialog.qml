pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "faceplateBindingsDialog"
    required property var api
    title: "Привязки тегов экземпляра"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: Math.min(700, parent ? parent.width - 32 : 700)
    property string faceplateName: ""

    ListModel { id: ports }

    function edit(instance, type) {
        if (!type) return
        faceplateName = instance.title || type.name
        ports.clear()
        for (const port of type.ports || [])
            ports.append({ portId: port.id, portName: port.name, typeId: port.type,
                tagId: instance.bindings?.[port.id] || "" })
        open()
    }

    function choices(type) {
        return [{ id: "", name: "Без привязки" }].concat(api.tags.filter(tag => {
            const tagType = tag.address?.type
            return type === "number" ? !!tagType && tagType !== "bool" && tagType !== "text" : tagType === type
        }))
    }

    function typeName(type) {
        if (type === "number") return "Числовой"
        if (type === "bool") return "Логический"
        return "Текстовый"
    }

    function save() {
        const bindings = {}
        for (let index = 0; index < ports.count; ++index) {
            const port = ports.get(index)
            if (port.tagId) bindings[port.portId] = port.tagId
        }
        if (api.configureWidget({ bindings })) close()
    }

    contentItem: ColumnLayout {
        spacing: 8
        LabelValue {
            text: dialog.faceplateName
            mode: "strong"
            Layout.fillWidth: true
        }
        RowLayout {
            visible: ports.count > 0
            Layout.fillWidth: true
            LabelValue { text: "Тег интерфейса"; Layout.preferredWidth: 235 }
            LabelValue { text: "Тег проекта"; Layout.fillWidth: true }
        }
        LabelValue {
            visible: ports.count === 0
            text: "У этого типа пока нет тегов интерфейса."
            mode: "muted"
            Layout.fillWidth: true
        }
        ListView {
            id: bindingList
            visible: count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(325, count * 44)
            model: ports
            clip: true
            spacing: 4
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                id: row
                required property int index
                required property var model
                readonly property var tagChoices: dialog.choices(model.typeId)
                width: bindingList.width - 8
                height: 40
                ColumnLayout {
                    Layout.preferredWidth: 235
                    spacing: 0
                    LabelValue { text: row.model.portName; Layout.fillWidth: true }
                    LabelValue { text: dialog.typeName(row.model.typeId); mode: "muted"; Layout.fillWidth: true }
                }
                ComboBox {
                    objectName: "faceplateBindingTag_" + row.index
                    Accessible.name: "Тег проекта для " + row.model.portName
                    Layout.fillWidth: true
                    model: row.tagChoices
                    textRole: "name"
                    valueRole: "id"
                    currentIndex: Math.max(0, row.tagChoices.findIndex(tag => tag.id === row.model.tagId))
                    onActivated: ports.setProperty(row.index, "tagId", currentValue)
                    ToolTip.visible: hovered && currentIndex > 0
                    ToolTip.text: currentText
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ActionButton { text: "Отмена"; onClicked: dialog.close() }
            ActionButton {
                objectName: "faceplateBindingsApply"
                text: "Применить"
                highlighted: true
                onClicked: dialog.save()
            }
        }
    }
}
