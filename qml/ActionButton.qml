import QtQuick
import QtQuick.Controls

Button {
    id: button
    property bool compact: false
    property string fillTone: ""
    property string textTone: ""
    leftPadding: compact ? 9 : 12
    rightPadding: compact ? 9 : 12
    topPadding: compact ? 4 : 7
    bottomPadding: compact ? 4 : 7
    font.pixelSize: 13
    font.weight: highlighted ? Font.DemiBold : Font.Medium
    implicitWidth: Math.max(compact ? 30 : 36, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(compact ? 30 : 35, contentItem.implicitHeight + topPadding + bottomPadding)

    contentItem: Text {
        text: button.text
        font: button.font
        color: button.textTone ? button.textTone : !button.enabled ? "#8494a4" : button.highlighted ? "#ffffff" : "#1c364a"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 5
        color: button.fillTone ? (button.down ? Qt.darker(button.fillTone, 1.16) : button.fillTone) :
            !button.enabled ? "#eff3f6" : button.highlighted ?
            (button.down ? "#07564f" : button.hovered ? "#086e65" : "#0b786d") :
            (button.down ? "#dbe8ee" : button.hovered ? "#e9f2f4" : "#f7fafb")
        border.color: button.visualFocus ? "#2d8e84" : button.highlighted && button.enabled ? "#0b786d" : "#d6e1e9"
    }
}
