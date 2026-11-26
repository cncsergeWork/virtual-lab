import QtQuick
import QtQuick.Controls

Canvas {
    id: symbol
    required property string mode
    property bool known: false
    property bool active: false
    property bool vertical: false
    property bool customInk: false
    property color accent: "#078375"
    property color structure: "#8ca1af"
    readonly property color stateInk: customInk || (known && active) ? accent : structure
    onKnownChanged: requestPaint()
    onActiveChanged: requestPaint()
    onVerticalChanged: requestPaint()
    onStateInkChanged: requestPaint()
    onStructureChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        if (width < 12 || height < 10) return
        ctx.translate(width / 2, height / 2)
        ctx.lineCap = "round"
        function line(points, ink, weight) {
            ctx.strokeStyle = ink
            ctx.lineWidth = weight
            ctx.beginPath()
            ctx.moveTo(points[0][0], points[0][1])
            for (let i = 1; i < points.length; ++i)
                ctx.lineTo(points[i][0], points[i][1])
            ctx.stroke()
        }
        function round(x, y, radius) {
            ctx.beginPath()
            ctx.arc(x, y, radius, 0, Math.PI * 2)
            ctx.fillStyle = Qt.rgba(stateInk.r, stateInk.g, stateInk.b, 0.18)
            ctx.fill()
            ctx.lineWidth = 2
            ctx.strokeStyle = stateInk
            ctx.stroke()
        }
        if (mode === "pipe") {
            if (vertical) ctx.rotate(Math.PI / 2)
            const length = (vertical ? height : width) / 2
            const diameter = Math.min(17, (vertical ? width : height) * 0.57)
            line([[-length, 0], [length, 0]], structure, diameter + 3)
            line([[-length, 0], [length, 0]], stateInk, diameter)
            line([[-length, -diameter * 0.20], [length, -diameter * 0.20]], Qt.lighter(stateInk, 1.24), 2)
            if (known && active && length > 35)
                for (const x of [-length * 0.56, 0, length * 0.56])
                    line([[x - 3, -3], [x + 2, 0], [x - 3, 3]], "white", 1.4)
            return
        }
        const r = Math.min(height * 0.34, width * 0.15)
        if (mode === "motor") {
            line([[-2 * r, r * 1.15], [2 * r, r * 1.15]], structure, 3)
            line([[-1.45 * r, r * 1.10], [-r * 0.58, r * 0.55]], structure, 2)
            line([[1.45 * r, r * 1.10], [r * 0.58, r * 0.55]], structure, 2)
            line([[r, 0], [2 * r, 0]], stateInk, 5)
            round(0, 0, r)
            ctx.fillStyle = stateInk
            ctx.font = "bold " + Math.max(8, Math.floor(r * 1.10)) + "px sans-serif"
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            ctx.fillText("M", 0, 0)
            return
        }
        line([[-2 * r, 0], [-r, 0]], stateInk, 4)
        line([[r, 0], [2 * r, 0]], stateInk, 4)
        line([[0, 0], [0, -r * 1.13]], stateInk, 2)
        line([[-r * 0.43, -r * 1.13], [r * 0.43, -r * 1.13]], stateInk, 3)
        for (const side of [-1, 1]) {
            const x = side * r
            ctx.fillStyle = Qt.rgba(stateInk.r, stateInk.g, stateInk.b, 0.24)
            ctx.beginPath()
            ctx.moveTo(x, -r * 0.62)
            ctx.lineTo(0, 0)
            ctx.lineTo(x, r * 0.62)
            ctx.closePath()
            ctx.fill()
            ctx.strokeStyle = stateInk
            ctx.lineWidth = 2
            ctx.stroke()
        }
    }
}
