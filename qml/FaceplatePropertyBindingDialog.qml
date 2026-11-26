pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "faceplatePropertyBindingDialog"
    required property var api
    property string propertyId: ""
    property string fieldName: ""
    property string fieldKind: ""
    property bool existingBinding: false
    property string errorText: ""
    readonly property bool visibilityRule: fieldKind === "visible"
    readonly property bool usesAlarm: condition.currentValue === "alarm"
    readonly property bool usesRange: condition.currentValue === "between" || condition.currentValue === "outside"
    readonly property var tagChoices: [{ id: "", name: "Выберите тег" }].concat(
        api.tags.filter(tag => tag.address?.type && tag.address.type !== "text"))
    readonly property var operators: [
        { id: "alarm", name: "Авария активна" },
        { id: "lt", name: "Меньше" },
        { id: "lte", name: "Меньше или равно" },
        { id: "eq", name: "Равно" },
        { id: "ne", name: "Не равно" },
        { id: "gte", name: "Больше или равно" },
        { id: "gt", name: "Больше" },
        { id: "between", name: "В диапазоне" },
        { id: "outside", name: "Вне диапазона" }
    ]
    signal bindingEdited(string propertyId, var binding)
    signal chooseColor(var field)

    title: "Динамическая привязка"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    width: Math.min(560, parent ? parent.width - 32 : 560)

    function edit(id, name, kind, binding) {
        propertyId = id
        fieldName = name
        fieldKind = kind
        existingBinding = !!binding
        errorText = ""
        sourceTag.currentIndex = Math.max(0, sourceTag.indexOfValue(binding?.tag ?? ""))
        condition.currentIndex = Math.max(0, condition.indexOfValue(binding?.op ?? "eq"))
        limit.text = String(binding?.limit ?? 1)
        upperLimit.text = String(binding?.max ?? 1)
        matchVisible.checked = visibilityRule ? binding?.value ?? true : true
        matchColor.text = visibilityRule ? "" : binding?.value ?? "#b32636"
        useOtherwise.checked = binding ? binding.otherwise !== undefined : visibilityRule
        otherwiseVisible.checked = visibilityRule ? binding?.otherwise ?? false : false
        otherwiseColor.text = visibilityRule ? "" : binding?.otherwise ?? "#0b776f"
        open()
    }

    function number(text) {
        const value = text.trim()
        return value ? Number(value.replace(",", ".")) : NaN
    }

    function validColor(value) {
        return /^#[0-9a-fA-F]{6}$/.test(value)
    }

    function save() {
        if (!sourceTag.currentValue) {
            errorText = "Выберите тег проекта."
            return
        }
        const binding = { tag: sourceTag.currentValue, op: condition.currentValue }
        if (!usesAlarm) {
            binding.limit = number(limit.text)
            if (!Number.isFinite(binding.limit)) {
                errorText = "Укажите числовой порог."
                return
            }
        }
        if (usesRange) {
            binding.max = number(upperLimit.text)
            if (!Number.isFinite(binding.max) || binding.max < binding.limit) {
                errorText = "Верхняя граница должна быть числом и быть не меньше нижней."
                return
            }
        }
        binding.value = visibilityRule ? matchVisible.checked : matchColor.text.trim()
        if (useOtherwise.checked)
            binding.otherwise = visibilityRule ? otherwiseVisible.checked : otherwiseColor.text.trim()
        if (!visibilityRule && (!validColor(binding.value) ||
            (useOtherwise.checked && !validColor(binding.otherwise)))) {
            errorText = "Укажите цвет в формате #RRGGBB."
            return
        }
        bindingEdited(propertyId, binding)
        close()
    }

    contentItem: ColumnLayout {
        spacing: 8
        LabelValue { text: dialog.fieldName; mode: "strong"; Layout.fillWidth: true }
        GridLayout {
            columns: 2
            Layout.fillWidth: true
            rowSpacing: 6
            columnSpacing: 12
            LabelValue { text: "Тег проекта"; Layout.preferredWidth: 175 }
            ComboBox {
                id: sourceTag
                objectName: "faceplateBindingConditionTag"
                Accessible.name: "Тег проекта"
                Layout.fillWidth: true
                model: dialog.tagChoices
                textRole: "name"
                valueRole: "id"
                ToolTip.visible: hovered && currentIndex > 0
                ToolTip.text: currentText
            }
            LabelValue { text: "Условие" }
            ComboBox {
                id: condition
                objectName: "faceplateBindingConditionOp"
                Accessible.name: "Условие"
                Layout.fillWidth: true
                model: dialog.operators
                textRole: "name"
                valueRole: "id"
            }
            LabelValue {
                visible: dialog.usesAlarm && sourceTag.currentIndex > 0 &&
                    !dialog.tagChoices[sourceTag.currentIndex]?.alarm?.direction
                Layout.columnSpan: 2
                Layout.fillWidth: true
                mode: "muted"
                text: "Для этого условия настройте аварию в свойствах тега."
            }
            LabelValue { visible: !dialog.usesAlarm; text: dialog.usesRange ? "Нижняя граница" : "Порог" }
            TextField {
                id: limit
                objectName: "faceplateBindingConditionLimit"
                Accessible.name: dialog.usesRange ? "Нижняя граница" : "Порог"
                visible: !dialog.usesAlarm
                Layout.fillWidth: true
                selectByMouse: true
            }
            LabelValue { visible: dialog.usesRange; text: "Верхняя граница" }
            TextField {
                id: upperLimit
                objectName: "faceplateBindingConditionMax"
                Accessible.name: "Верхняя граница"
                visible: dialog.usesRange
                Layout.fillWidth: true
                selectByMouse: true
            }
            LabelValue { text: "Если выполнено" }
            CheckBox {
                id: matchVisible
                objectName: "faceplateBindingMatchVisible"
                visible: dialog.visibilityRule
                text: "Показывать"
            }
            ColorField {
                id: matchColor
                objectName: "faceplateBindingMatchColor"
                visible: !dialog.visibilityRule
                Layout.fillWidth: true
                caption: "Цвет при выполнении условия"
                hint: "#RRGGBB"
                onChoose: field => dialog.chooseColor(field)
            }
            LabelValue { text: "Иначе" }
            CheckBox {
                id: useOtherwise
                objectName: "faceplateBindingUseOtherwise"
                text: "Задать значение"
            }
            Item { implicitWidth: 1; implicitHeight: 1 }
            LabelValue {
                visible: !useOtherwise.checked
                Layout.fillWidth: true
                mode: "muted"
                text: "Значение из свойств экземпляра или типа"
            }
            CheckBox {
                id: otherwiseVisible
                objectName: "faceplateBindingOtherwiseVisible"
                visible: dialog.visibilityRule && useOtherwise.checked
                text: "Показывать"
            }
            ColorField {
                id: otherwiseColor
                objectName: "faceplateBindingOtherwiseColor"
                visible: !dialog.visibilityRule && useOtherwise.checked
                Layout.fillWidth: true
                caption: "Цвет, когда условие не выполнено"
                hint: "#RRGGBB"
                onChoose: field => dialog.chooseColor(field)
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
                objectName: "faceplateBindingConditionRemove"
                visible: dialog.existingBinding
                text: "Удалить привязку"
                onClicked: {
                    dialog.bindingEdited(dialog.propertyId, null)
                    dialog.close()
                }
            }
            Item { Layout.fillWidth: true }
            ActionButton { text: "Отмена"; onClicked: dialog.close() }
            ActionButton {
                objectName: "faceplateBindingConditionApply"
                text: "Применить"
                highlighted: true
                onClicked: dialog.save()
            }
        }
    }
}
