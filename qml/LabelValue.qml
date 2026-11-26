import QtQuick
import QtQuick.Controls

Label {
    property string mode: "normal"
    font.pixelSize: 13
    font.weight: mode === "strong" ? Font.DemiBold : Font.Normal
    color: mode === "muted" ? "#5d6d80" : mode === "error" ? "#993b34" : "#16293d"
    elide: Text.ElideRight
    verticalAlignment: Text.AlignVCenter
}
