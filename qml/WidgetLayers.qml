pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: panel
    required property var api
    readonly property var layers: api.items.slice().reverse()
    readonly property int selected: layers.findIndex(layer => layer.id === api.selectedId)

    LabelValue { text: "Слои"; mode: "strong"; font.pixelSize: 15 }
    LabelValue {
        visible: panel.layers.length === 0
        text: "Добавьте элемент"
        mode: "muted"
    }
    ListView {
        objectName: "widgetLayers"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(148, count * 32)
        visible: count > 0
        clip: true
        model: panel.layers
        delegate: ActionButton {
            id: layer
            required property var modelData
            objectName: "widgetLayer_" + modelData.id
            width: ListView.view.width
            height: 31
            compact: true
            text: (modelData.locked ? "▣ " : "") + (modelData.title || "Слой")
            highlighted: panel.api.selectedIds.includes(modelData.id)
            onClicked: panel.api.selectWidget(modelData.id)
            ToolTip.visible: hovered
            ToolTip.text: modelData.title || "Слой"
        }
        ScrollBar.vertical: ScrollBar {}
    }
    RowLayout {
        visible: panel.layers.length > 0
        Layout.fillWidth: true
        spacing: 3
        ActionButton {
            objectName: "widgetLayerUp"
            compact: true
            text: "↑"
            enabled: panel.selected > 0
            Accessible.name: "Поднять слой"
            ToolTip.visible: hovered
            ToolTip.text: Accessible.name
            onClicked: panel.api.moveLayer(1)
        }
        ActionButton {
            objectName: "widgetLayerDown"
            compact: true
            text: "↓"
            enabled: panel.selected >= 0 && panel.selected < panel.layers.length - 1
            Accessible.name: "Опустить слой"
            ToolTip.visible: hovered
            ToolTip.text: Accessible.name
            onClicked: panel.api.moveLayer(-1)
        }
        ActionButton {
            objectName: "widgetLayerLock"
            compact: true
            Layout.fillWidth: true
            text: panel.api.widget.locked ? "▣" : "□"
            enabled: panel.selected >= 0
            Accessible.name: panel.api.widget.locked ? "Разблокировать слой" : "Заморозить положение"
            ToolTip.visible: hovered
            ToolTip.text: Accessible.name
            onClicked: panel.api.configureWidget({ locked: !panel.api.widget.locked })
        }
    }
}
