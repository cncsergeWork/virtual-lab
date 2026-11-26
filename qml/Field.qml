import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: field
    required property string caption
    property alias text: entry.text
    property alias placeholderText: entry.placeholderText
    property alias validator: entry.validator
    readonly property alias acceptableInput: entry.acceptableInput
    readonly property alias inputFocused: entry.activeFocus
    signal accepted()
    signal textEdited()
    spacing: 5
    Layout.fillWidth: true
    Label { text: field.caption; color: "#45596f"; font.pixelSize: 12 }
    TextField {
        id: entry
        objectName: field.objectName ? field.objectName + "_input" : ""
        Layout.fillWidth: true
        implicitHeight: 36
        selectByMouse: true
        onAccepted: field.accepted()
        onTextEdited: field.textEdited()
    }
}
