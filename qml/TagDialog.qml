import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: form
    required property var api
    property var entry: ({})
    readonly property bool engine: form.api.sources.find(source => source.id === sourceBox.currentValue)?.driver === "engine-json"
    readonly property bool textTag: engine && jsonType.currentIndex === 2
    readonly property bool bits: engine ? jsonType.currentIndex === 1 : tableBox.currentIndex === 0 || tableBox.currentIndex === 1
    readonly property bool writeableTable: tableBox.currentIndex === 0 || tableBox.currentIndex === 3
    modal: true
    width: 580
    title: entry.id ? "Параметры тега" : "Новый тег"
    function edit(item) {
        entry = item || {}
        nameField.text = entry.name || ""
        unitField.text = String(entry.address?.unit ?? 1)
        offsetField.text = String(entry.address?.offset ?? 0)
        sourceBox.currentIndex = Math.max(0, sourceBox.indexOfValue(entry.source))
        jsonPath.text = entry.address?.path || ""
        jsonType.currentIndex = entry.address?.type === "text" ? 2 : entry.address?.type === "bool" ? 1 : 0
        tableBox.currentIndex = Math.max(0, tableBox.indexOfValue(entry.address?.table || "holding"))
        typeBox.currentIndex = Math.max(0, typeBox.indexOfValue(entry.address?.type || "u16"))
        orderBox.currentIndex = Math.max(0, orderBox.indexOfValue(entry.address?.order || "ABCD"))
        unitsField.text = entry.units || ""
        scaleField.text = String(entry.scale ?? 1)
        biasField.text = String(entry.bias ?? 0)
        archiveField.text = String(entry.archiveMs ?? 1000)
        writeBox.checked = entry.writable || false
        alarmBox.currentIndex = entry.alarm?.direction === "high" ? 1 : entry.alarm?.direction === "low" ? 2 : 0
        limitField.text = String(entry.alarm?.limit ?? 0)
        hysteresisField.text = String(entry.alarm?.hysteresis ?? 0)
        open()
    }
    function number(text) {
        return text.trim().length > 0 ? Number(text.replace(",", ".")) : NaN
    }
    contentItem: ColumnLayout {
        spacing: 9
        RowLayout {
            Layout.fillWidth: true
            Field { id: nameField; objectName: "tagName"; caption: "Имя тега"; Layout.preferredWidth: 270 }
            ColumnLayout {
                Layout.preferredWidth: 235
                Layout.fillWidth: true
                LabelValue { text: "Источник"; mode: "muted" }
                ComboBox { id: sourceBox; Layout.fillWidth: true; model: form.api.sources; textRole: "name"; valueRole: "id" }
            }
        }
        RowLayout {
            visible: !form.engine
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                LabelValue { text: "Таблица Modbus"; mode: "muted" }
                ComboBox {
                        id: tableBox
                        objectName: "tagModbusTable"
                    Layout.fillWidth: true
                    textRole: "name"
                    valueRole: "code"
                    model: [ { name: "Coils", code: "coils" }, { name: "Discrete Inputs", code: "discrete" },
                             { name: "Input Registers", code: "input" }, { name: "Holding Registers", code: "holding" } ]
                    onActivated: typeBox.currentIndex = 0
                }
            }
            Field { id: unitField; caption: "Unit ID"; Layout.preferredWidth: 75 }
            Field { id: offsetField; caption: "Адрес с нуля"; Layout.preferredWidth: 100 }
        }
        RowLayout {
            visible: form.engine
            Layout.fillWidth: true
            Field { id: jsonPath; objectName: "tagJsonPath"; caption: "Путь JSON"; placeholderText: "values.rpm" }
            ComboBox { id: jsonType; model: ["Число", "Логический", "Текст"]; Layout.preferredWidth: 160 }
        }
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                visible: !form.engine
                Layout.fillWidth: true
                LabelValue { text: "Тип"; mode: "muted" }
                ComboBox {
                    id: typeBox
                    Layout.fillWidth: true
                    textRole: "name"; valueRole: "code"
                    model: form.bits ? [{ name: "Bool", code: "bool" }] :
                           [{ name: "UInt16", code: "u16" }, { name: "Int16", code: "i16" },
                            { name: "UInt32", code: "u32" }, { name: "Int32", code: "i32" },
                            { name: "Float32", code: "f32" }]
                }
            }
            ColumnLayout {
                visible: !form.engine
                Layout.fillWidth: true
                LabelValue { text: "Порядок байтов"; mode: "muted" }
                ComboBox {
                    id: orderBox
                    Layout.fillWidth: true
                    enabled: !form.bits
                    model: ["ABCD", "BADC", "CDAB", "DCBA"]
                }
            }
            Field { id: unitsField; caption: "Ед. измерения"; Layout.preferredWidth: 110 }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !form.textTag
            Field { id: scaleField; caption: "Масштаб"; enabled: !form.bits }
            Field { id: biasField; caption: "Смещение"; enabled: !form.bits }
            Field { id: archiveField; caption: "Архив, мс · 0 выкл." }
        }
        RowLayout {
            visible: !form.textTag
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                LabelValue { text: "Тревога"; mode: "muted" }
                ComboBox { id: alarmBox; Layout.fillWidth: true; model: ["Выключена", "Верхний порог", "Нижний порог"] }
            }
            Field { id: limitField; caption: "Порог"; enabled: alarmBox.currentIndex > 0 }
            Field { id: hysteresisField; caption: "Гистерезис"; enabled: alarmBox.currentIndex > 0 }
        }
        CheckBox { id: writeBox; objectName: "tagAllowWrite"; visible: !form.engine; enabled: form.writeableTable; text: "Разрешить запись в тег" }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ActionButton { objectName: "tagCancel"; text: "Отмена"; onClicked: form.close() }
            ActionButton {
                text: "Сохранить"
                objectName: "tagSave"
                highlighted: true
                onClicked: {
                    const tag = { id: form.entry.id || "", name: nameField.text, source: sourceBox.currentValue,
                        units: unitsField.text.trim(), scale: form.bits || form.textTag ? 1 : form.number(scaleField.text),
                        bias: form.bits || form.textTag ? 0 : form.number(biasField.text),
                        archiveMs: form.textTag ? 0 : form.number(archiveField.text),
                        writable: !form.engine && form.writeableTable && writeBox.checked,
                        address: form.engine ? { path: jsonPath.text.trim(), type: ["number", "bool", "text"][jsonType.currentIndex] } :
                                 { table: tableBox.currentValue, unit: form.number(unitField.text),
                                   offset: form.number(offsetField.text), type: typeBox.currentValue,
                                   order: form.bits ? "ABCD" : orderBox.currentText } }
                    if (!form.textTag && alarmBox.currentIndex > 0)
                        tag.alarm = { direction: alarmBox.currentIndex === 1 ? "high" : "low",
                                      limit: form.number(limitField.text), hysteresis: form.number(hysteresisField.text) }
                    if (form.api.setTag(tag))
                        form.close()
                }
            }
        }
    }
}
