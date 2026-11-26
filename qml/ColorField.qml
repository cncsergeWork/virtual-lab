import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: field
    property alias text: entry.text
    property string hint: qsTr("Авто")
    property string caption: qsTr("Выбрать цвет")
    signal choose(var field)
    signal edited(string value)
    spacing: 4

    function setColor(value) {
        entry.text = value
        edited(value)
    }
    ActionButton {
        objectName: field.objectName ? field.objectName + "_pick" : ""
        Layout.preferredWidth: 31
        Layout.preferredHeight: 33
        leftPadding: 5
        rightPadding: 5
        Accessible.name: field.caption
        contentItem: Rectangle {
            implicitWidth: 16
            radius: 3
            color: /^#[A-Fa-f0-9]{6}$/.test(entry.text.trim()) ? entry.text.trim() : "#edf1f5"
            border.color: "#a9bac9"
        }
        onClicked: field.choose(field)
        ToolTip.visible: hovered
        ToolTip.text: field.caption
    }
    TextField {
        id: entry
        Layout.fillWidth: true
        Layout.minimumWidth: 47
        Layout.preferredHeight: 33
        font.pixelSize: 12
        selectByMouse: true
        placeholderText: field.hint
        onTextEdited: field.edited(text)
    }
}
