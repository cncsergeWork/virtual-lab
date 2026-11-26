pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "faceplateInstancePropertiesDialog"
    required property var api
    property string instanceName: ""
    property string errorText: ""
    property var colorTarget: null
    property var propertyBindings: ({})
    title: "Свойства экземпляра"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    width: Math.min(870, parent ? parent.width - 32 : 870)

    ListModel { id: fields }

    function propertyName(kind) {
        switch (kind) {
        case "title": return "Подпись"
        case "background": return "Цвет фона"
        case "value": return "Цвет показания"
        case "captionColor": return "Цвет подписи"
        case "visible": return "Видимость"
        default: return ""
        }
    }

    function edit(instance, type) {
        if (!type) return
        instanceName = instance.title || type.name
        errorText = ""
        const overrides = instance.overrides || {}
        propertyBindings = Object.assign({}, instance.propertyBindings || {})
        fields.clear()
        for (const property of type.properties || []) {
            const child = type.items.find(item => item.id === property.item)
            const kind = property.kind
            const ownValue = Object.prototype.hasOwnProperty.call(overrides, property.id)
            const defaultText = kind === "title" ? child?.title ?? "" : child?.appearance?.[kind] ?? ""
            const defaultVisible = property.defaultVisible ?? true
            fields.append({
                propertyId: property.id, name: property.name, childName: child?.title || "",
                propertyKind: kind, ownValue, defaultText, defaultVisible,
                textValue: ownValue && kind !== "visible" ? overrides[property.id] : defaultText,
                visibleValue: ownValue && kind === "visible" ? overrides[property.id] : defaultVisible
            })
        }
        open()
    }

    function updateBinding(propertyId, binding) {
        const next = Object.assign({}, propertyBindings)
        if (binding) next[propertyId] = binding
        else delete next[propertyId]
        propertyBindings = next
    }

    function validColor(value) {
        return /^#[a-fA-F0-9]{6}$/.test(value)
    }

    function pickColor(field) {
        colorTarget = field
        const value = field.text.trim()
        colorPicker.selectedColor = validColor(value) ? value : "#ffffff"
        colorPicker.open()
    }

    function save() {
        const overrides = {}
        for (let index = 0; index < fields.count; ++index) {
            const row = fields.get(index)
            if (!row.ownValue) continue
            if (row.propertyKind === "visible") {
                overrides[row.propertyId] = row.visibleValue
            } else if (row.propertyKind === "title") {
                overrides[row.propertyId] = row.textValue
            } else {
                const value = row.textValue.trim()
                if (!validColor(value)) {
                    errorText = "Укажите цвет «" + row.name + "» в формате #RRGGBB."
                    return
                }
                overrides[row.propertyId] = value
            }
        }
        errorText = ""
        if (api.configureWidget({ overrides, propertyBindings })) close()
    }

    ColorDialog {
        id: colorPicker
        title: "Выбрать цвет"
        onAccepted: {
            if (dialog.colorTarget) {
                const color = selectedColor
                const rgb = [color.r, color.g, color.b].map(channel =>
                    Math.round(channel * 255).toString(16).padStart(2, "0")).join("")
                dialog.colorTarget.setColor("#" + rgb)
            }
            dialog.colorTarget = null
        }
        onRejected: dialog.colorTarget = null
    }

    FaceplatePropertyBindingDialog {
        id: bindingDialog
        api: dialog.api
        onBindingEdited: (propertyId, binding) => dialog.updateBinding(propertyId, binding)
        onChooseColor: field => dialog.pickColor(field)
    }

    contentItem: ColumnLayout {
        spacing: 8
        LabelValue { text: dialog.instanceName; mode: "strong"; Layout.fillWidth: true }
        Label {
            visible: fields.count === 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#526579"
            text: "Откройте тип и добавьте поля через «Интерфейс свойств…»."
        }
        ListView {
            id: propertyList
            objectName: "faceplatePropertiesList"
            visible: count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(408, count * 51,
                dialog.parent ? Math.max(100, dialog.parent.height - 155) : 408)
            model: fields
            clip: true
            spacing: 5
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                id: row
                required property int index
                required property var model
                width: propertyList.width - 8
                height: 46
                ColumnLayout {
                    Layout.fillWidth: false
                    Layout.preferredWidth: Math.min(280, row.width * 0.32)
                    spacing: 0
                    LabelValue { text: row.model.name; Layout.fillWidth: true }
                    LabelValue {
                        text: dialog.propertyName(row.model.propertyKind) +
                            (row.model.childName ? " · " + row.model.childName : "")
                        mode: "muted"
                        Layout.fillWidth: true
                    }
                }
                CheckBox {
                    objectName: "faceplatePropertyOwn_" + row.index
                    Layout.preferredWidth: 146
                    Layout.fillWidth: false
                    text: "Своё значение"
                    Accessible.name: "Своё значение для «" + row.model.name + "»"
                    checked: row.model.ownValue
                    onCheckedChanged: if (checked !== row.model.ownValue)
                        fields.setProperty(row.index, "ownValue", checked)
                }
                TextField {
                    objectName: "faceplatePropertyText_" + row.index
                    Accessible.name: row.model.name
                    visible: row.model.propertyKind === "title"
                    Layout.fillWidth: true
                    enabled: row.model.ownValue
                    text: row.model.ownValue ? row.model.textValue : row.model.defaultText
                    onTextChanged: if (visible && row.model.ownValue && text !== row.model.textValue)
                        fields.setProperty(row.index, "textValue", text)
                }
                ColorField {
                    objectName: "faceplatePropertyColor_" + row.index
                    visible: row.model.propertyKind !== "title" && row.model.propertyKind !== "visible"
                    Layout.fillWidth: true
                    enabled: row.model.ownValue
                    caption: row.model.name
                    hint: row.model.ownValue ? "#RRGGBB" : "Авто"
                    text: row.model.ownValue ? row.model.textValue : row.model.defaultText
                    onTextChanged: if (visible && row.model.ownValue && text !== row.model.textValue)
                        fields.setProperty(row.index, "textValue", text)
                    onChoose: field => dialog.pickColor(field)
                }
                CheckBox {
                    objectName: "faceplatePropertyVisible_" + row.index
                    visible: row.model.propertyKind === "visible"
                    Layout.fillWidth: true
                    text: "Показывать"
                    enabled: row.model.ownValue
                    checked: row.model.ownValue ? row.model.visibleValue : row.model.defaultVisible
                    onCheckedChanged: if (visible && row.model.ownValue && checked !== row.model.visibleValue)
                        fields.setProperty(row.index, "visibleValue", checked)
                }
                ActionButton {
                    readonly property var bindingRule: dialog.propertyBindings[row.model.propertyId]
                    objectName: "faceplatePropertyBinding_" + row.index
                    Accessible.name: "Динамическая привязка для «" + row.model.name + "»"
                    visible: row.model.propertyKind !== "title"
                    Layout.preferredWidth: 112
                    text: bindingRule?.tag ? "Настроено" : bindingRule ? "Выбрать тег…" : "По тегу…"
                    onClicked: bindingDialog.edit(row.model.propertyId, row.model.name, row.model.propertyKind, bindingRule)
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
            Item { Layout.fillWidth: true }
            ActionButton { objectName: "faceplatePropertiesCancel"; text: "Отмена"; onClicked: dialog.close() }
            ActionButton {
                objectName: "faceplatePropertiesApply"
                text: "Применить"
                enabled: fields.count > 0
                highlighted: true
                onClicked: dialog.save()
            }
        }
    }
}
