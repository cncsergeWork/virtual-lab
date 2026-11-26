pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: previewDialog
    objectName: "widgetPreviewDialog"
    required property var api
    property var samples: ({})
    property var journal: []
    readonly property var ports: api.editorTags
    readonly property var canvas: api.canvasSettings
    readonly property var eventNames: ({ press: qsTr("Нажал"), release: qsTr("Отпустил"), click: qsTr("Щёлкнул"), change: qsTr("Изменил") })
    readonly property var simulation: ({ samples: samples, record: record })

    title: qsTr("Предпросмотр · ") + (canvas.name ?? "")
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1040, parent ? parent.width - 32 : 1040)
    height: Math.min(670, parent ? parent.height - 32 : 670)
    modal: true

    function updateSample(id, value) {
        const next = Object.assign({}, samples)
        next[id] = value === undefined ? {} : { value: value, quality: "good", hasValue: true }
        samples = next
    }
    function record(layer, eventName, requests) {
        const lines = []
        for (const request of requests) {
            const target = ports.find(port => port.id === request.tag)
            if (!target) continue
            let action = qsTr("прочитать «") + target.name + "»"
            if (request.action === "write") {
                const value = request.valueTag ? samples[request.valueTag]?.value : request.value
                action = qsTr("записать в «") + target.name + "» " + (value ?? qsTr("— (нет значения)"))
                if (value !== undefined) updateSample(request.tag, target.address?.type === "bool" ? !!Number(value) : value)
            }
            lines.push((eventNames[eventName] ?? eventName) + " · " + layer + " · " + action)
        }
        journal = lines.reverse().concat(journal).slice(0, 32)
    }
    function start() {
        journal = []
        const defaults = {}
        for (const port of ports) {
            const type = port.address?.type
            defaults[port.id] = { quality: "good", hasValue: true,
                value: type === "bool" ? false : type === "text" ? "" : 0 }
        }
        samples = defaults
        open()
    }

    contentItem: RowLayout {
        spacing: 14
        ColumnLayout {
            Layout.preferredWidth: 238
            Layout.minimumWidth: 156
            Layout.maximumWidth: 238
            Layout.fillWidth: false
            Layout.fillHeight: true
            LabelValue { text: qsTr("Пробные значения"); mode: "strong" }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: parent.width
                    Repeater {
                        model: previewDialog.visible ? previewDialog.ports : []
                        delegate: ColumnLayout {
                            id: input
                            required property var modelData
                            readonly property string type: modelData.address?.type ?? "number"
                            Layout.fillWidth: true
                            CheckBox {
                                objectName: "previewBool_" + input.modelData.id
                                visible: input.type === "bool"
                                text: input.modelData.name
                                checked: !!previewDialog.samples[input.modelData.id]?.value
                                onToggled: previewDialog.updateSample(input.modelData.id, checked)
                            }
                            Field {
                                id: sample
                                objectName: "previewValue_" + input.modelData.id
                                visible: input.type !== "bool"
                                caption: input.modelData.name
                                Binding on text {
                                    when: !sample.inputFocused
                                    value: String(previewDialog.samples[input.modelData.id]?.value ?? "")
                                    restoreMode: Binding.RestoreNone
                                }
                                onTextEdited: {
                                    const value = input.type === "text" ? text :
                                        text.trim() ? Number(text.trim().replace(",", ".")) : undefined
                                    previewDialog.updateSample(input.modelData.id, typeof value === "number" && !Number.isFinite(value) ?
                                        undefined : value)
                                }
                            }
                        }
                    }
                }
            }
            ActionButton { text: qsTr("Сбросить"); onClicked: previewDialog.start() }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                color: "#e9f0f5"; radius: 8; clip: true
                Item {
                    id: previewBoard
                    readonly property real baseWidth: previewDialog.canvas.width ?? 480
                    readonly property real baseHeight: previewDialog.canvas.height ?? 280
                    readonly property real factor: Math.max(0.001, Math.min((parent.width - 22) / baseWidth,
                                                                          (parent.height - 22) / baseHeight))
                    readonly property real horizontalRatio: 1
                    readonly property real verticalRatio: 1
                    readonly property bool darkBackground: false
                    readonly property string moving: ""
                    readonly property real dx: 0
                    readonly property real dy: 0
                    readonly property int gridStep: 10
                    readonly property bool faceplateLayer: true
                    anchors.centerIn: parent
                    width: baseWidth; height: baseHeight
                    scale: factor
                    Repeater {
                        model: previewDialog.visible ? previewDialog.api.items : []
                        delegate: ScreenItem {
                            required property var modelData
                            objectName: "previewLayer_" + modelData.id
                            api: previewDialog.api
                            board: previewBoard
                            config: modelData
                            editing: false
                            preview: true
                            simulation: previewDialog.simulation
                            snap: false
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                LabelValue { text: qsTr("Действия в симуляции"); mode: "strong" }
                Item { Layout.fillWidth: true }
                ActionButton { objectName: "widgetPreviewClose"; text: qsTr("Закрыть"); onClicked: previewDialog.close() }
            }
            ListView {
                objectName: "previewJournal"
                Layout.fillWidth: true; Layout.preferredHeight: 84
                model: previewDialog.journal; clip: true
                delegate: LabelValue {
                    required property string modelData
                    width: ListView.view.width
                    text: modelData
                }
                ScrollBar.vertical: ScrollBar {}
            }
        }
    }
}
