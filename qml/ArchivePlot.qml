import QtQuick

Trend {
    id: plot
    required property var navigator
    required property string gestureName
    property bool interactive: true
    property bool periodMenu: false
    signal menuRequested()
    api: navigator.api
    windowRange: navigator.range
    showTimeAxis: false
    readonly property real plotWidth: Math.max(1, width - axisLeft - axisRight)

    Repeater {
        model: [{ time: plot.navigator.markA, title: "A", color: "#906e32" },
                { time: plot.navigator.markB, title: "B", color: "#8b559b" },
                { time: plot.navigator.cursor, title: "", color: "#5b7593" }]
        delegate: Rectangle {
            required property var modelData
            visible: modelData.time > 0 && modelData.time >= plot.navigator.startTime && modelData.time <= plot.navigator.endTime
            x: plot.axisLeft + (modelData.time - plot.navigator.startTime) / plot.navigator.span * plot.plotWidth
            y: plot.operatorStyle ? plot.plotTop : 6
            width: 1
            height: plot.operatorStyle ? Math.max(0, plot.plotBottom - plot.plotTop) : plot.height - 14
            color: modelData.color
            LabelValue { x: 4; y: -5; text: parent.modelData.title; color: parent.color; font.pixelSize: 11 }
        }
    }
    MouseArea {
        enabled: plot.interactive
        objectName: plot.gestureName
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: plot.axisLeft
        width: plot.plotWidth
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: plot.periodMenu ? Qt.LeftButton | Qt.RightButton : Qt.LeftButton
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.CrossCursor
        property real downX: 0
        property real initialStart: 0
        property real initialEnd: 0
        property bool dragged: false
        function fraction(x) { return Math.max(0, Math.min(1, x / width)) }
        function time(x) { return plot.navigator.startTime + plot.navigator.span * fraction(x) }
        onPressed: mouse => {
            if (mouse.button !== Qt.LeftButton) return
            downX = mouse.x
            initialStart = plot.navigator.startTime
            initialEnd = plot.navigator.endTime
            dragged = false
        }
        onPositionChanged: mouse => {
            if ((mouse.buttons & Qt.LeftButton) && (dragged || Math.abs(mouse.x - downX) >= 4)) {
                dragged = true
                const delta = (mouse.x - downX) / width * (initialEnd - initialStart)
                plot.navigator.setRange(initialStart - delta, initialEnd - delta)
            } else if (!plot.navigator.pinned && !pressed) {
                plot.navigator.selectCursor(time(mouse.x))
            }
        }
        onReleased: mouse => {
            if (mouse.button === Qt.RightButton) {
                plot.menuRequested()
                return
            }
            if (dragged) return
            plot.navigator.pinned = !plot.navigator.pinned
            plot.navigator.selectCursor(time(mouse.x))
        }
        onDoubleClicked: mouse => { if (mouse.button === Qt.LeftButton) plot.navigator.fitAll() }
        onExited: {
            if (plot.operatorStyle && !pressed && !plot.navigator.pinned)
                plot.navigator.selectCursor(0)
        }
        onWheel: wheel => {
            if (wheel.modifiers & Qt.ShiftModifier) { wheel.accepted = false; return }
            const delta = wheel.angleDelta.y || wheel.pixelDelta.y
            if (delta) plot.navigator.zoom(delta > 0 ? 0.77 : 1.30, fraction(wheel.x))
        }
    }
}
