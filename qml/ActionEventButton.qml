import QtQuick

ActionButton {
    id: button
    signal occurred(string eventName)
    property bool held: false

    function finishPress() {
        if (!held) return
        held = false
        occurred("release")
    }

    onPressed: {
        held = true
        occurred("press")
    }
    onReleased: finishPress()
    onCanceled: finishPress()
    onVisibleChanged: if (!visible) finishPress()
    onEnabledChanged: if (!enabled) finishPress()
    onClicked: occurred("click")
}
