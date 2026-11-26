import QtQuick
import QtQuick.Controls

Item {
    id: chart
    required property var api
    property var tag: ""
    property int minutes: 5
    property double end: 0
    property var seriesDefinitions: []
    property var traces: []
    readonly property int visibleSeriesCount: traces.filter(trace => trace.valid).length
    property bool normalized: false
    property bool dualAxis: false
    property bool operatorStyle: false
    property bool individualAxes: false
    property bool relativeTime: false
    property bool darkSurface: false
    property string textTone: ""
    readonly property color axisInk: textTone || (darkSurface ? "#d5e3f0" : "#53677a")
    readonly property color gridInk: darkSurface ? "#496178" : operatorStyle ? "#cedce3" : "#e3eaf0"
    property var windowRange: null
    property bool showTimeAxis: true
    property bool stepped: false
    readonly property double from: windowRange ? windowRange[0] : end - minutes * 60000
    readonly property double to: windowRange ? windowRange[1] : end
    readonly property var scales: operatorStyle ? operatorScales() : []
    readonly property int axisColumn: width >= 650 ? 75 : width >= 400 ? 61 : 48
    readonly property var drawnScales: scales.slice(0, Math.max(1, Math.floor((width - 112) / axisColumn)))
    readonly property int axisLeft: operatorStyle ?
        Math.max(12, drawnScales.filter(scale => scale.side === "left").length * axisColumn) : 55
    readonly property int axisRight: operatorStyle ?
        Math.max(12, drawnScales.filter(scale => scale.side === "right").length * axisColumn) : dualAxis ? 64 : 20
    readonly property int plotTop: operatorStyle ? (height > 95 ? 26 : 15) : 12
    readonly property int plotBottom: height - (showTimeAxis ? (operatorStyle ? 29 : 25) : 10)
    property color lineColor: "#087c70"
    property bool queued: true

    function refresh() {
        if (!windowRange)
            end = Date.now()
        const definitions = seriesDefinitions.length ? seriesDefinitions : tag ? [{ id: tag, color: lineColor, stepped: stepped }] : []
        const buckets = Math.max(70, Math.min(650, Math.round(width)))
        traces = visible && to > from ? definitions.map(definition => {
            const samples = api.series(definition.id, Math.round(from), Math.round(to), buckets)
            let min = Infinity, max = -Infinity
            for (const sample of samples) {
                if (!sample.hasValue) continue
                min = Math.min(min, sample.min)
                max = Math.max(max, sample.max)
            }
            const valid = min !== Infinity
            const binary = definition.binary ?? definition.stepped
            if (binary && valid) { min = 0; max = 1 }
            if (valid && definition.min !== undefined && definition.max !== undefined) {
                min = definition.min; max = definition.max
            }
            return { id: definition.id, samples, min, max, valid, binary, color: definition.color,
                     stepped: definition.stepped, style: definition.style, side: definition.side || "left",
                     units: definition.units || "", divisions: definition.divisions || 5,
                     fixed: definition.min !== undefined && definition.max !== undefined ?
                         [definition.min, definition.max] : null }
        }) : []
        paint.requestPaint()
    }
    onTagChanged: { queued = true; if (windowRange) defer() }
    onLineColorChanged: if (windowRange) defer()
    onSeriesDefinitionsChanged: defer()
    onNormalizedChanged: if (paint) paint.requestPaint()
    onDualAxisChanged: if (paint) paint.requestPaint()
    onScalesChanged: if (paint) paint.requestPaint()
    onRelativeTimeChanged: if (paint) paint.requestPaint()
    onShowTimeAxisChanged: if (paint) paint.requestPaint()
    onAxisInkChanged: if (paint) paint.requestPaint()
    onGridInkChanged: if (paint) paint.requestPaint()
    onMinutesChanged: queued = true
    onWindowRangeChanged: { queued = true; defer() }
    onVisibleChanged: { queued = true; if (windowRange) defer() }
    Component.onCompleted: if (windowRange) defer()
    function defer() {
        if (visible && queryDelay && !queryDelay.running)
            queryDelay.start()
    }
    function extent(traces) {
        if (!traces.length || normalized) return [0, 100]
        if (traces.every(trace => trace.binary)) return [-0.12, 1.12]
        let min = Math.min(...traces.map(trace => trace.min))
        let max = Math.max(...traces.map(trace => trace.max))
        const padding = max === min ? Math.max(0.5, Math.abs(min) * 0.03) : (max - min) * 0.12
        return [min - padding, max + padding]
    }
    function operatorScales() {
        function scale(rows, side, separate) {
            const first = rows[0]
            const valid = rows.filter(trace => trace.valid)
            const binary = rows.length > 0 && rows.every(trace => trace.binary)
            const units = first && rows.every(trace => trace.units === first.units) ? first.units : ""
            const range = (separate && first.fixed) || (binary ? [-0.12, 1.12] : valid.length ? extent(valid) : [0, 1])
            return { ids: rows.map(trace => trace.id), side, units, range, binary: binary && !(separate && first.fixed),
                     color: first && (separate || dualAxis) ? first.color : axisInk,
                     divisions: separate ? first.divisions : 5 }
        }
        if (individualAxes)
            return traces.map(trace => scale([trace], trace.side, true))
        if (!dualAxis)
            return [scale(traces, "left", false)]
        return ["left", "right"].map(side => traces.filter(trace => trace.side === side))
            .filter(rows => rows.length).map(rows => scale(rows, rows[0].side, false))
    }
    function timeLabel(fraction) {
        if (!to) return ""
        const elapsed = to - from
        if (relativeTime) {
            const seconds = Number((elapsed * (1 - fraction) / 1000).toPrecision(4))
            return (seconds ? "−" : "") + seconds + qsTr(" с")
        }
        return Qt.formatDateTime(new Date(from + elapsed * fraction),
            elapsed >= 86400000 ? "dd.MM HH:mm" : elapsed <= 300000 ? "HH:mm:ss" : "HH:mm")
    }
    function drawOperatorAxes(ctx, left, top, right, bottom) {
        const primary = drawnScales.find(scale => scale.side === "left") || drawnScales[0]
        const fontSize = width >= 450 ? 13 : 11
        ctx.font = fontSize + "px sans-serif"
        ctx.strokeStyle = gridInk
        ctx.lineWidth = 1
        const availableDivisions = Math.max(2, Math.floor((bottom - top) / (fontSize + 3)))
        const gridDivisions = Math.min(primary?.divisions || 5, availableDivisions)
        const line = (x1, y1, x2, y2) => {
            ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke()
        }
        for (let i = 0; i <= gridDivisions; ++i) {
            const y = Math.round(top + (bottom - top) * i / gridDivisions) + 0.5
            line(left, y, right, y)
        }
        ctx.setLineDash([4, 5])
        for (let i = 0; i <= 4; ++i) {
            const x = Math.round(left + (right - left) * i / 4) + 0.5
            line(x, top, x, bottom)
        }
        ctx.setLineDash([])
        let leftIndex = 0, rightIndex = 0
        for (const scale of drawnScales) {
            const onLeft = scale.side === "left"
            const x = onLeft ? left - 9 - leftIndex++ * axisColumn : right + 9 + rightIndex++ * axisColumn
            const [min, max] = scale.range
            const divisions = Math.min(scale.divisions, availableDivisions)
            ctx.fillStyle = scale.color
            ctx.textAlign = onLeft ? "right" : "left"
            ctx.textBaseline = "middle"
            if (height > 95) ctx.fillText(scale.units, x, 7, axisColumn - 11)
            for (let i = 0; i <= (scale.binary ? 1 : divisions); ++i) {
                const value = scale.binary ? i : min + (max - min) * i / divisions
                const y = bottom - (value - min) / (max - min) * (bottom - top)
                const label = Math.abs(value) < (max - min) * 1e-9 ? "0" : String(Number(value.toPrecision(7)))
                ctx.fillText(label, x, y, axisColumn - 11)
            }
        }
        if (!showTimeAxis) return
        ctx.fillStyle = axisInk
        ctx.textBaseline = "alphabetic"
        for (let i = 0; i < 3; ++i) {
            if (i === 1 && right - left < 170) continue
            ctx.textAlign = ["left", "center", "right"][i]
            ctx.fillText(timeLabel(i / 2), left + (right - left) * i / 2, height - 7)
        }
    }
    Timer { id: queryDelay; interval: 45; onTriggered: chart.refresh() }
    Connections {
        target: chart.api
        function onArchiveChanged() { if (!chart.windowRange) chart.queued = true }
    }
    Timer {
        interval: 500
        running: chart.visible && !chart.windowRange
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            if (chart.queued || chart.api.running) {
                chart.queued = false
                chart.refresh()
            }
        }
    }
    Canvas {
        id: paint
        anchors.fill: parent
        renderTarget: Canvas.Image
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            if (width < 120 || height < 60)
                return
            const left = chart.axisLeft, top = chart.plotTop, right = width - chart.axisRight
            const bottom = chart.plotBottom
            const visibleTraces = chart.traces.filter(trace => trace.valid)
            const leftTraces = chart.dualAxis ? visibleTraces.filter(trace => trace.side === "left") : visibleTraces
            const rightTraces = chart.dualAxis ? visibleTraces.filter(trace => trace.side === "right") : []
            const leftScale = chart.extent(leftTraces)
            const rightScale = chart.extent(rightTraces)
            ctx.font = "12px sans-serif"
            function ticks(scale, traces, rightSide) {
                const [min, max] = scale
                const binary = !chart.normalized && traces.length > 0 && traces.every(trace => trace.binary)
                const levels = binary ? [1, 0] : [max, (3 * max + min) / 4, (max + min) / 2, (max + 3 * min) / 4, min]
                ctx.fillStyle = chart.dualAxis && traces.length && !chart.textTone ? traces[0].color : chart.axisInk
                ctx.strokeStyle = chart.gridInk
                ctx.lineWidth = 1
                for (const value of levels) {
                    const y = top + (bottom - top) * (max - value) / (max - min)
                    const label = Number(value.toPrecision(4)).toString() + (chart.normalized ? "%" : "")
                    const x = rightSide ? right + 5 : Math.max(0, left - 7 - ctx.measureText(label).width)
                    if (!chart.dualAxis || traces.length) ctx.fillText(label, x, y + 4)
                    if (!rightSide || leftTraces.length === 0) {
                        ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(right, y); ctx.stroke()
                    }
                }
            }
            if (chart.operatorStyle) {
                chart.drawOperatorAxes(ctx, left, top, right, bottom)
            } else {
                ticks(leftScale, leftTraces, false)
                if (rightTraces.length) ticks(rightScale, rightTraces, true)
                ctx.fillStyle = chart.axisInk
                for (let i = 0; i <= 4; ++i) {
                    const x = left + (right - left) * i / 4
                    if (chart.showTimeAxis) {
                        const label = chart.timeLabel(i / 4)
                        const labelWidth = ctx.measureText(label).width
                        ctx.fillText(label, Math.max(left, Math.min(width - labelWidth, x - labelWidth / 2)), height - 5)
                    }
                    ctx.beginPath(); ctx.moveTo(x, top); ctx.lineTo(x, bottom); ctx.stroke()
                }
            }
            if (visibleTraces.length === 0)
                return
            const xOf = t => left + (t - chart.from) / (chart.to - chart.from) * (right - left)
            ctx.save()
            ctx.beginPath(); ctx.rect(left, top - 3, right - left, bottom - top + 6); ctx.clip()
            for (const trace of visibleTraces) {
                const [min, max] = chart.operatorStyle ? chart.scales.find(scale => scale.ids.includes(trace.id)).range :
                    chart.dualAxis && trace.side === "right" ? rightScale : leftScale
                const yOf = v => bottom - (v - min) / (max - min) * (bottom - top)
                const mapped = v => chart.normalized ? (trace.max === trace.min ? 50 :
                    (v - trace.min) / (trace.max - trace.min) * 100) : v
                ctx.strokeStyle = trace.color
                ctx.fillStyle = trace.color
                ctx.lineWidth = 1
                ctx.globalAlpha = 0.36
                for (const p of trace.samples) {
                    if (!p.hasValue || p.min === p.max) continue
                    const x = xOf(p.time)
                    ctx.beginPath(); ctx.moveTo(x, yOf(mapped(p.min))); ctx.lineTo(x, yOf(mapped(p.max))); ctx.stroke()
                }
                ctx.globalAlpha = 1
                ctx.lineWidth = chart.operatorStyle ? 1.7 : 2
                let previous = null
                for (const p of trace.samples) {
                    if (p.gap) previous = null
                    if (!p.hasValue) continue
                    const x = xOf(p.time), y = yOf(mapped(trace.binary ? (p.value >= 0.5 ? 1 : 0) : p.value))
                    if (previous && trace.style !== "points") {
                        ctx.beginPath(); ctx.moveTo(previous.x, previous.y)
                        if (trace.stepped) ctx.lineTo(x, previous.y)
                        ctx.lineTo(x, y); ctx.stroke()
                    } else {
                        ctx.beginPath(); ctx.arc(x, y, chart.operatorStyle ? 2 : 2.5, 0, 2 * Math.PI); ctx.fill()
                    }
                    previous = p.gap ? null : { x: x, y: y }
                }
            }
            ctx.restore()
        }
    }
    Label {
        anchors.centerIn: parent
        visible: chart.visibleSeriesCount === 0 && (!chart.operatorStyle || !chart.seriesDefinitions.length)
        color: chart.axisInk
        text: chart.tag || chart.seriesDefinitions.length ? qsTr("Нет записей за этот период") : qsTr("Выберите теги")
        font.pixelSize: 14
    }
}
