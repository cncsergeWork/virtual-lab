import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: form
    required property var api
    property var entry: ({})
    readonly property bool engine: driverBox.currentIndex === 1
    readonly property bool serial: transportBox.currentIndex === 1
    modal: true
    width: 470
    title: entry.id ? "Параметры источника" : "Новый источник"
    function edit(item) {
        entry = item || {}
        driverBox.currentIndex = entry.driver === "engine-json" ? 1 : 0
        transportBox.currentIndex = entry.settings?.transport === "serial" ? 1 : 0
        urlField.text = entry.settings?.url || ""
        serialField.text = entry.settings?.portName || ""
        nameField.text = entry.name || ""
        hostField.text = entry.settings?.host || ""
        portField.text = String(entry.settings?.port ?? 502)
        pollField.text = String(entry.settings?.pollMs ?? 1000)
        timeoutField.text = String(entry.settings?.timeoutMs ?? 1000)
        engineTimeout.text = String(entry.settings?.timeoutMs ?? 2500)
        writeBox.checked = entry.settings?.allowWrite || false
        presetBox.checked = false
        open()
    }
    contentItem: ColumnLayout {
        spacing: 12
        ComboBox {
            id: driverBox
            objectName: "sourceDriver"
            Layout.fillWidth: true
            model: ["Modbus TCP", "JSON-телеметрия"]
            enabled: !form.entry.id
        }
        Field { id: nameField; objectName: "sourceName"; caption: "Имя источника"; placeholderText: "Например, ПЛК участка" }
        Field { id: hostField; objectName: "sourceHost"; visible: !form.engine; caption: "IP-адрес или имя узла"; placeholderText: "192.168.1.10"; enabled: !form.entry.settings?.simulator }
        RowLayout {
            visible: !form.engine
            Layout.fillWidth: true
            Field { id: portField; caption: "Порт" }
            Field { id: pollField; caption: "Опрос, мс" }
            Field { id: timeoutField; caption: "Тайм-аут, мс" }
        }
        CheckBox { id: writeBox; objectName: "sourceAllowWrite"; visible: !form.engine; text: "Разрешить запись в этом подключении" }
        ComboBox {
            id: transportBox
            objectName: "jsonTransport"
            visible: form.engine
            model: ["JSON по WebSocket", "JSON по USB COM · 115200"]
            Layout.fillWidth: true
        }
        LabelValue { visible: form.engine; text: "Протокол телеметрии v1"; mode: "muted" }
        Field {
            id: urlField
            objectName: "engineUrl"
            visible: form.engine && !form.serial
            caption: "Адрес WebSocket"
            placeholderText: "ws://192.168.4.1:81/ws"
        }
        Field { id: serialField; objectName: "sourceSerialPort"; visible: form.engine && form.serial; caption: "Последовательный порт"; placeholderText: "COM7" }
        Field { id: engineTimeout; visible: form.engine; caption: "Тайм-аут телеметрии, мс" }
        CheckBox {
            id: presetBox
            objectName: "jsonSampleTags"
            visible: form.engine && !form.entry.id
            text: "Загрузить пример таблицы тегов"
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ActionButton { objectName: "sourceCancel"; text: "Отмена"; onClicked: form.close() }
            ActionButton {
                text: "Сохранить"
                objectName: "sourceSave"
                highlighted: true
                onClicked: {
                    const settings = form.engine ? { transport: form.serial ? "serial" : "websocket",
                                      url: urlField.text.trim(), portName: serialField.text.trim(),
                                      timeoutMs: Number(engineTimeout.text), allowWrite: false } :
                                    { host: hostField.text.trim(), port: Number(portField.text),
                                      pollMs: Number(pollField.text), timeoutMs: Number(timeoutField.text),
                                      allowWrite: writeBox.checked, simulator: form.entry.settings?.simulator || false }
                    if (form.api.setSource({ id: form.entry.id || "", name: nameField.text,
                                             driver: form.engine ? "engine-json" : "modbus-tcp", settings: settings })) {
                        if (form.engine && !form.entry.id && presetBox.checked)
                            form.api.importEngineTags(form.api.sources[form.api.sources.length - 1].id)
                        form.close()
                    }
                }
            }
        }
    }
}
