import QtQuick
import QtQuick.Controls

Rectangle {
    id: face
    required property string mode
    required property var reading
    property real lower: 0
    property real upper: 100
    property int precision: 1
    property color faceColor: "#52d6bf"
    property string surface: ""
    property bool customInk: false
    property string units: ""
    property bool alarm: false
    property bool bound: false
    readonly property bool hasReading: !!reading.hasValue
    readonly property bool valid: reading.quality === "good"
    readonly property real value: hasReading ? Number(reading.value) : 0
    readonly property color panelColor: surface || "#14263b"
    readonly property bool lightPanel: (0.2126 * panelColor.r + 0.7152 * panelColor.g + 0.0722 * panelColor.b) >= 0.55
    readonly property color ink: customInk ? faceColor : !valid ? (lightPanel ? "#53677a" : "#90a4b7") : alarm ? "#ff9976" : faceColor
    readonly property string quality: !bound ? "Тег не выбран" : alarm ? "Тревога" :
        reading.quality === "error" ? "Ошибка чтения" : hasReading ? "Данные устарели" : "Нет данных"
    radius: 8
    color: panelColor
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: face.surface ? face.panelColor : "#1b324b" }
        GradientStop { position: 1; color: face.surface ? face.panelColor : "#101f32" }
    }
    function fraction() {
        return Math.max(0, Math.min(1, (value - lower) / (upper - lower)))
    }
    function formatted(number, places) {
        if (Math.abs(number) >= 100000000 || (number !== 0 && Math.abs(number) < Math.pow(10, -places - 1)))
            return number.toExponential(Math.min(places, 3))
        return number.toFixed(places)
    }
    onValueChanged: drawing.requestPaint()
    onHasReadingChanged: drawing.requestPaint()
    onInkChanged: drawing.requestPaint()
    onLowerChanged: drawing.requestPaint()
    onUpperChanged: drawing.requestPaint()
    onPrecisionChanged: drawing.requestPaint()
    onUnitsChanged: drawing.requestPaint()
    onModeChanged: drawing.requestPaint()
    onSurfaceChanged: drawing.requestPaint()
    onVisibleChanged: if (visible) drawing.requestPaint()

    Canvas {
        id: drawing
        anchors.fill: parent
        readonly property color scaleInk: face.lightPanel ? "#4b6174" : "#a9bece"
        readonly property color grid: face.lightPanel ? "#99aaba" : "#425b72"
        readonly property var digits: ({
            "0": "abcdef", "1": "bc", "2": "abdeg", "3": "abcdg", "4": "bcfg",
            "5": "acdfg", "6": "acdefg", "7": "abc", "8": "abcdefg", "9": "abcdfg",
            "E": "adefg", "-": "g"
        })
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        function polar(x, y, radius, angle) {
            return [x + radius * Math.cos(angle), y + radius * Math.sin(angle)]
        }
        function stroke(ctx, a, b, width, color) {
            ctx.strokeStyle = color; ctx.lineWidth = width
            ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.stroke()
        }
        function arc(ctx, x, y, radius, from, to, width, color) {
            ctx.strokeStyle = color; ctx.lineWidth = width
            ctx.beginPath(); ctx.arc(x, y, radius, from, to); ctx.stroke()
        }
        function disc(ctx, x, y, radius, color) {
            ctx.fillStyle = color
            ctx.beginPath(); ctx.arc(x, y, radius, 0, Math.PI * 2); ctx.fill()
        }
        function textAt(ctx, text, x, y, size, color, limit, bold) {
            ctx.fillStyle = color
            ctx.textAlign = "center"; ctx.textBaseline = "middle"
            const style = bold ? "bold " : ""
            ctx.font = style + size + "px sans-serif"
            const measured = ctx.measureText(text).width
            if (measured > limit)
                ctx.font = style + Math.max(7, size * limit / measured) + "px sans-serif"
            ctx.fillText(text, x, y)
        }
        function polygon(ctx, vertices, color) {
            ctx.fillStyle = color
            ctx.beginPath()
            ctx.moveTo(vertices[0][0], vertices[0][1])
            for (let i = 1; i < vertices.length; ++i) ctx.lineTo(vertices[i][0], vertices[i][1])
            ctx.closePath(); ctx.fill()
        }
        function captions(ctx, x, y, size, maxWidth, value) {
            textAt(ctx, face.hasReading ? value : "—", x, y, size, face.ink, maxWidth, true)
            if (face.units && face.mode !== "compass")
                textAt(ctx, face.units, x, y + size * 0.84, Math.max(10, size * 0.38), scaleInk, maxWidth, false)
        }
        function drawDial(ctx, ring) {
            const r = Math.min(width * 0.37, height * 0.40)
            const x = width / 2, y = height * (ring ? 0.49 : 0.52)
            const first = Math.PI * 0.75
            const sweep = Math.PI * 1.50
            if (ring) {
                const count = 32
                const edge = sweep / count
                const lit = face.hasReading ? Math.round(face.fraction() * count) : 0
                for (let i = 0; i < count; ++i)
                    arc(ctx, x, y, r * 0.87, first + edge * (i + 0.10), first + edge * (i + 0.79),
                        Math.max(2, r * 0.095), i < lit ? face.ink : grid)
                captions(ctx, x, y - r * 0.05, Math.min(38, r * 0.51), r * 1.48, face.formatted(face.value, face.precision))
            } else {
                arc(ctx, x, y, r, 0, 2 * Math.PI, 1, grid)
                arc(ctx, x, y, r * 0.87, first, first + sweep, 2, grid)
                for (let i = 0; i <= 30; ++i) {
                    const major = i % 5 === 0
                    const angle = first + sweep * i / 30
                    stroke(ctx, polar(x, y, r * (major ? 0.73 : 0.81), angle),
                           polar(x, y, r * 0.87, angle), major ? 1.5 : 1, major ? scaleInk : grid)
                    if (major && i !== 0 && i !== 30 && r > 48) {
                        const mark = polar(x, y, r * 0.57, angle)
                        const number = (face.upper - face.lower) * i / 30 + face.lower
                        textAt(ctx, face.formatted(number, face.upper - face.lower >= 10 ? 0 : 1),
                               mark[0], mark[1], r * 0.14, scaleInk, r * 0.45, false)
                    }
                }
                if (face.hasReading) {
                    const angle = first + face.fraction() * sweep
                    polygon(ctx, [polar(x, y, r * 0.79, angle), polar(x, y, r * 0.053, angle - Math.PI / 2),
                                  polar(x, y, r * 0.19, angle + Math.PI), polar(x, y, r * 0.053, angle + Math.PI / 2)], face.ink)
                }
                disc(ctx, x, y, Math.max(2, r * 0.062), face.ink)
                captions(ctx, x, y + r * 0.42, Math.min(28, r * 0.33), r * 1.04, face.formatted(face.value, face.precision))
            }
            const a = polar(x, y, r * 0.95, first), b = polar(x, y, r * 0.95, first + sweep)
            const size = Math.max(8, Math.min(13, r * 0.14))
            textAt(ctx, face.formatted(face.lower, 1), a[0] - 3, a[1] + size, size, scaleInk, r * 0.57, false)
            textAt(ctx, face.formatted(face.upper, 1), b[0] + 3, b[1] + size, size, scaleInk, r * 0.57, false)
        }
        function drawColumn(ctx) {
            const x = width * 0.29
            const top = height * 0.14, bottom = height * 0.78
            const radius = Math.min(width * 0.055, height * 0.068)
            ctx.lineCap = "round"
            stroke(ctx, [x, top], [x, bottom], radius * 1.50, grid)
            if (face.hasReading)
                stroke(ctx, [x, bottom - (bottom - top) * face.fraction()], [x, bottom], radius * 0.82, face.ink)
            disc(ctx, x, bottom, radius, grid)
            disc(ctx, x, bottom, radius * 0.62, face.hasReading ? face.ink : "#61768a")
            ctx.lineCap = "butt"
            for (let i = 0; i <= 10; ++i) {
                const y = top + (bottom - top) * i / 10
                stroke(ctx, [x - radius * 1.45, y], [x - radius * (i % 5 === 0 ? 2.32 : 1.86), y], 1, scaleInk)
            }
            const size = Math.min(36, width * 0.18, height * 0.23)
            captions(ctx, width * 0.69, height * 0.46, size, width * 0.49, face.formatted(face.value, face.precision))
            textAt(ctx, face.formatted(face.upper, 1), width * 0.54, top, 11, scaleInk, width * 0.29, false)
            textAt(ctx, face.formatted(face.lower, 1), width * 0.54, bottom, 11, scaleInk, width * 0.29, false)
        }
        function drawBargraph(ctx) {
            const left = width * 0.085, right = width * 0.915, y = height * 0.56
            const count = width >= 190 ? 22 : 12
            const block = (right - left) / count
            const lit = face.hasReading ? Math.round(face.fraction() * count) : 0
            for (let i = 0; i < count; ++i) {
                ctx.fillStyle = i < lit ? face.ink : grid
                ctx.fillRect(left + i * block, y, Math.max(1, block - 2), Math.max(4, height * 0.12))
            }
            for (let i = 0; i <= 4; ++i) {
                const x = left + (right - left) * i / 4
                stroke(ctx, [x, y + height * 0.15], [x, y + height * 0.19], 1, scaleInk)
            }
            const labelSize = Math.max(8, Math.min(12, height * 0.13))
            textAt(ctx, face.formatted(face.lower, face.precision), left + 13, height * 0.87, labelSize, scaleInk, width * 0.28, false)
            textAt(ctx, face.formatted(face.upper, face.precision), right - 13, height * 0.87, labelSize, scaleInk, width * 0.28, false)
            captions(ctx, width * 0.5, height * 0.27, Math.min(29, height * 0.22), width * 0.83,
                face.formatted(face.value, face.precision))
        }
        function drawCompass(ctx) {
            const x = width / 2, y = height * 0.43, r = Math.min(width * 0.30, height * 0.285)
            arc(ctx, x, y, r, 0, 2 * Math.PI, 1.5, grid)
            for (let i = 0; i < 36; ++i) {
                const angle = i * Math.PI / 18
                stroke(ctx, polar(x, y, r * (i % 3 === 0 ? 0.84 : 0.93), angle),
                       polar(x, y, r, angle), i % 3 === 0 ? 1.3 : 0.75, scaleInk)
            }
            const letters = ["В", "Ю", "З", "С"]
            for (let i = 0; i < 4; ++i) {
                const point = polar(x, y, r * 1.21, i * Math.PI / 2)
                textAt(ctx, letters[i], point[0], point[1], r * 0.22, i === 3 ? face.ink : scaleInk, r, true)
            }
            const degrees = ((face.value % 360) + 360) % 360
            if (face.hasReading) {
                const angle = (degrees - 90) * Math.PI / 180
                const sideA = polar(x, y, r * 0.16, angle + Math.PI / 2)
                const sideB = polar(x, y, r * 0.16, angle - Math.PI / 2)
                polygon(ctx, [polar(x, y, r * 0.76, angle), sideA, sideB], face.ink)
                polygon(ctx, [polar(x, y, r * 0.76, angle + Math.PI), sideA, sideB], "#648096")
            }
            disc(ctx, x, y, Math.max(2, r * 0.053), "#f2f8fb")
            captions(ctx, x, height * 0.90, Math.min(25, r * 0.32), width * 0.78, face.formatted(degrees, face.precision) + "°")
        }
        function drawLamp(ctx) {
            const active = face.hasReading && face.value !== 0
            const lit = face.valid && (active || face.customInk)
            const radius = Math.min(width * 0.095, height * 0.27)
            const x = width * 0.24, y = height * 0.49
            disc(ctx, x, y, radius * 1.20, "#0c1725")
            arc(ctx, x, y, radius * 1.09, 0, 2 * Math.PI, 1.5, grid)
            if (lit) {
                const halo = ctx.createRadialGradient(x, y, 0, x, y, radius)
                halo.addColorStop(0, "#ffffff")
                halo.addColorStop(0.42, face.ink)
                halo.addColorStop(1, "#24465a")
                disc(ctx, x, y, radius, halo)
            } else {
                disc(ctx, x, y, radius, "#294259")
                disc(ctx, x - radius * 0.26, y - radius * 0.30, radius * 0.22, "#45627a")
            }
            const status = face.hasReading ? (active ? "Включено" : "Выключено") : "—"
            textAt(ctx, status, width * 0.66, y, Math.min(23, height * 0.29), lit ? face.ink : scaleInk, width * 0.52, true)
        }
        function drawSegments(ctx) {
            const display = face.hasReading ? face.formatted(face.value, face.precision).toUpperCase().replace("+", "") : "-"
            const cells = display.replace(/\./g, "").length
            const digitWidth = Math.min(height * 0.32, width * 0.89 / Math.max(3, cells * 1.21))
            const digitHeight = digitWidth * 1.70
            const band = digitWidth * 0.14
            const step = digitWidth * 1.21
            const y = height * 0.44 - digitHeight / 2
            let x = (width - step * cells) / 2
            function segment(x1, y1, x2, y2, width, on) {
                const tone = on ? face.ink : "#2a4159"
                const dx = x2 - x1, dy = y2 - y1, length = Math.hypot(dx, dy)
                const u = dx / length * width, v = dy / length * width
                polygon(ctx, [[x1, y1], [x1 + u - v / 2, y1 + v + u / 2], [x2 - u - v / 2, y2 - v + u / 2],
                              [x2, y2], [x2 - u + v / 2, y2 - v - u / 2], [x1 + u + v / 2, y1 + v - u / 2]], tone)
            }
            for (const ch of display) {
                if (ch === ".") {
                    disc(ctx, x - step * 0.09, y + digitHeight, band * 0.44, face.ink)
                    continue
                }
                const lit = digits[ch] || ""
                const x2 = x + digitWidth
                const center = y + digitHeight / 2
                const bottom = y + digitHeight
                segment(x + band, y, x2 - band, y, band, lit.includes("a"))
                segment(x2, y + band, x2, center - band, band, lit.includes("b"))
                segment(x2, center + band, x2, bottom - band, band, lit.includes("c"))
                segment(x + band, bottom, x2 - band, bottom, band, lit.includes("d"))
                segment(x, center + band, x, bottom - band, band, lit.includes("e"))
                segment(x, y + band, x, center - band, band, lit.includes("f"))
                segment(x + band, center, x2 - band, center, band, lit.includes("g"))
                x += step
            }
            if (face.units)
                textAt(ctx, face.units, width / 2, height * 0.87, Math.min(16, height * 0.18), scaleInk, width * 0.86, false)
        }
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            if (width < 22 || height < 18) return
            if (face.mode === "dial") drawDial(ctx, false)
            else if (face.mode === "ring") drawDial(ctx, true)
            else if (face.mode === "column") drawColumn(ctx)
            else if (face.mode === "compass") drawCompass(ctx)
            else if (face.mode === "signal") drawLamp(ctx)
            else if (face.mode === "segments") drawSegments(ctx)
            else if (face.mode === "bargraph") drawBargraph(ctx)
        }
    }
    Label {
        visible: (face.alarm || !face.valid) && face.height >= 64
        anchors.left: parent.left; anchors.top: parent.top
        anchors.leftMargin: 7; anchors.topMargin: 5
        width: parent.width - 14
        text: face.quality
        color: face.alarm ? (face.lightPanel ? "#963923" : "#ffc09e") : drawing.scaleInk
        font.pixelSize: 10
        elide: Text.ElideRight
    }
}
