pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "faceplatePropertyInterfaceDialog"
    required property var api
    readonly property var kinds: [
        { id: "title", name: qsTr("Подпись") },
        { id: "background", name: qsTr("Цвет фона") },
        { id: "value", name: qsTr("Цвет показания") },
        { id: "captionColor", name: qsTr("Цвет подписи") },
        { id: "visible", name: qsTr("Видимость") }
    ]
    property string typeName: ""
    property var items: []
    property string errorText: ""
    title: qsTr("Интерфейс свойств")
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    width: Math.min(860, parent ? parent.width - 32 : 860)

    ListModel { id: properties }

    function edit(type) {
        if (!type) return
        typeName = type.name
        const children = type.items || []
        const names = children.map(item => (item.title || "").trim())
        items = children.map((item, index) => ({
            id: item.id,
            name: names[index] && names.indexOf(names[index]) === names.lastIndexOf(names[index])
                ? names[index] : (names[index] || qsTr("Элемент")) + " · " + (index + 1)
        }))
        errorText = ""
        properties.clear()
        for (const property of type.properties || []) {
            properties.append({
                propertyId: property.id, propertyName: property.name, itemId: property.item,
                propertyKind: property.kind, defaultVisible: property.defaultVisible ?? true
            })
        }
        open()
    }

    function save() {
        const definitions = []
        const names = new Set()
        const targets = new Set()
        for (let index = 0; index < properties.count; ++index) {
            const row = properties.get(index)
            const name = row.propertyName.trim()
            const nameKey = name.toLocaleLowerCase()
            const target = row.itemId + "/" + row.propertyKind
            if (!name || !row.itemId) {
                errorText = qsTr("Укажите имя и элемент для каждого свойства.")
                return
            }
            if (names.has(nameKey) || targets.has(target)) {
                errorText = names.has(nameKey) ? qsTr("Имена свойств должны различаться.") :
                    qsTr("Одно свойство элемента можно добавить в интерфейс один раз.")
                return
            }
            names.add(nameKey)
            targets.add(target)
            const definition = { id: row.propertyId, name: name, item: row.itemId, kind: row.propertyKind }
            if (row.propertyKind === "visible") definition.defaultVisible = row.defaultVisible
            definitions.push(definition)
        }
        errorText = ""
        if (api.setFaceplateProperties(definitions)) close()
    }

    contentItem: ColumnLayout {
        spacing: 8
        LabelValue { text: dialog.typeName; mode: "strong"; Layout.fillWidth: true }
        RowLayout {
            visible: properties.count > 0
            Layout.fillWidth: true
            LabelValue { text: qsTr("Имя в экземпляре"); Layout.fillWidth: true }
            LabelValue { text: qsTr("Элемент типа"); Layout.preferredWidth: 215 }
            LabelValue { text: qsTr("Свойство"); Layout.preferredWidth: 148 }
            LabelValue { text: qsTr("Видимость по типу"); Layout.preferredWidth: 132 }
            Item { Layout.preferredWidth: 38 }
        }
        LabelValue {
            visible: properties.count === 0
            text: dialog.items.length ? qsTr("Добавьте свойства, доступные у экземпляров этого типа.") :
                qsTr("Добавьте на холст типа элемент, чтобы открыть его свойства для экземпляров.")
            mode: "muted"
            Layout.fillWidth: true
        }
        ListView {
            id: propertyList
            visible: count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(350, count * 41)
            model: properties
            clip: true
            spacing: 5
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                id: row
                required property int index
                required property var model
                width: propertyList.width - 8
                height: 36
                TextField {
                    objectName: "propertyInterfaceName_" + row.index
                    Accessible.name: qsTr("Имя свойства в экземпляре")
                    Layout.fillWidth: true
                    Layout.minimumWidth: 72
                    placeholderText: qsTr("Например, Заголовок")
                    maximumLength: 80
                    text: row.model.propertyName
                    onTextChanged: if (text !== row.model.propertyName)
                        properties.setProperty(row.index, "propertyName", text)
                }
                ComboBox {
                    objectName: "propertyInterfaceItem_" + row.index
                    Accessible.name: qsTr("Элемент типа")
                    Layout.preferredWidth: 215
                    Layout.minimumWidth: 86
                    model: dialog.items
                    textRole: "name"
                    valueRole: "id"
                    currentIndex: dialog.items.findIndex(item => item.id === row.model.itemId)
                    onActivated: properties.setProperty(row.index, "itemId", currentValue)
                    ToolTip.visible: hovered && currentIndex >= 0
                    ToolTip.text: currentText
                    ToolTip.delay: 650
                }
                ComboBox {
                    objectName: "propertyInterfaceKind_" + row.index
                    Accessible.name: qsTr("Свойство элемента")
                    Layout.preferredWidth: 148
                    model: dialog.kinds
                    textRole: "name"
                    valueRole: "id"
                    currentIndex: dialog.kinds.findIndex(kind => kind.id === row.model.propertyKind)
                    onActivated: properties.setProperty(row.index, "propertyKind", currentValue)
                }
                Item {
                    Layout.preferredWidth: 132
                    CheckBox {
                        objectName: "propertyInterfaceDefaultVisible_" + row.index
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.model.propertyKind === "visible"
                        text: qsTr("Показывать")
                        checked: row.model.defaultVisible
                        onCheckedChanged: if (checked !== row.model.defaultVisible)
                            properties.setProperty(row.index, "defaultVisible", checked)
                    }
                }
                ActionButton {
                    compact: true
                    text: "×"
                    objectName: "propertyInterfaceRemove_" + row.index
                    Accessible.name: qsTr("Удалить свойство из интерфейса")
                    onClicked: properties.remove(row.index)
                }
            }
        }
        Label {
            visible: dialog.errorText !== ""
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#a33434"
            text: dialog.errorText
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                objectName: "propertyInterfaceAdd"
                text: qsTr("+ Свойство")
                enabled: dialog.items.length > 0 && properties.count < 128
                onClicked: {
                    properties.append({ propertyId: "", propertyName: "", itemId: dialog.items[0].id,
                        propertyKind: "title", defaultVisible: true })
                    propertyList.positionViewAtEnd()
                }
            }
            Item { Layout.fillWidth: true }
            ActionButton { objectName: "propertyInterfaceCancel"; text: qsTr("Отмена"); onClicked: dialog.close() }
            ActionButton {
                objectName: "propertyInterfaceApply"
                text: qsTr("Применить")
                highlighted: true
                onClicked: dialog.save()
            }
        }
    }
}
