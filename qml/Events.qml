import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: page
    required property var api
    property var journal: []
    color: "#ffffff"; radius: 8; border.color: "#d7e0e8"
    function eventName(type) {
        const names = { alarm_on: "Тревога", alarm_off: "Снятие", alarm_ack: "Квитирование",
                        write_request: "Команда", write_ok: "Запись подтверждена", write_error: "Ошибка записи",
                        read_request: "Запрос чтения", read_ok: "Тег прочитан", read_error: "Ошибка чтения" }
        return names[type] || type
    }
    function refresh() { journal = api.events() }
    onVisibleChanged: if (visible) refresh()
    Component.onCompleted: if (visible) refresh()
    Connections { target: page.api; function onArchiveChanged() { if (page.visible) page.refresh() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 22; spacing: 11
        LabelValue { text: "Тревоги"; mode: "strong"; font.pixelSize: 22; Layout.fillWidth: true }
        LabelValue { visible: page.api.alarms.length === 0; text: "Сейчас активных тревог нет."; mode: "muted" }
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(156, count * 50)
            visible: count > 0
            clip: true; spacing: 4
            model: page.api.alarms
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width; height: 46; radius: 6
                color: modelData.ack ? "#f5f1eb" : "#f8e9e4"
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 13; anchors.rightMargin: 8
                    LabelValue { text: modelData.name; mode: "strong"; Layout.preferredWidth: 220 }
                    LabelValue { text: (modelData.high ? "Верхний порог " : "Нижний порог ") + modelData.limit; Layout.fillWidth: true }
                    ActionButton { objectName: "eventAcknowledge_" + modelData.tag; text: modelData.ack ? "Квитирована" : "Квитировать"; enabled: !modelData.ack; onClicked: page.api.acknowledge(modelData.tag) }
                }
            }
            ScrollBar.vertical: ScrollBar {}
        }
        LabelValue { text: "Журнал событий"; mode: "strong"; font.pixelSize: 17; Layout.topMargin: 7 }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: page.journal
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: ListView.view.width; height: 39
                color: index % 2 ? "#ffffff" : "#f0f4f7"
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
                    LabelValue { text: Qt.formatDateTime(new Date(modelData.time), "dd.MM HH:mm:ss"); mode: "muted"; Layout.preferredWidth: 111 }
                    LabelValue { text: page.eventName(modelData.type); mode: "strong"; Layout.preferredWidth: 156 }
                    LabelValue { text: modelData.name; Layout.preferredWidth: 170 }
                    LabelValue { text: modelData.detail; Layout.fillWidth: true }
                }
            }
            LabelValue { anchors.centerIn: parent; visible: parent.count === 0; text: "В журнале пока нет событий."; mode: "muted" }
            ScrollBar.vertical: ScrollBar {}
        }
    }
}
