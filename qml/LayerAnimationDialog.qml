import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: form
    required property var api
    readonly property bool enabledEffect: !!effect.currentValue
    readonly property bool blink: effect.currentValue === "blink"
    readonly property bool interval: operator.currentValue === "between" || operator.currentValue === "outside"
    readonly property var tags: [{ id: "", name: "Без привязки" }].concat(api.editorTags.filter(tag => tag.address?.type !== "text"))
    readonly property var operators: [
        { id: "eq", name: "Равно" }, { id: "ne", name: "Не равно" },
        { id: "gte", name: "Больше или равно" }, { id: "gt", name: "Больше" },
        { id: "lte", name: "Меньше или равно" }, { id: "lt", name: "Меньше" },
        { id: "between", name: "В диапазоне" }, { id: "outside", name: "Вне диапазона" }
    ]
    readonly property real lower: number(threshold.text)
    readonly property real upper: number(maximum.text)
    readonly property bool valid: !enabledEffect || Number.isFinite(lower) && Math.abs(lower) <= 1e100 &&
        (!interval || Number.isFinite(upper) && upper >= lower && upper <= 1e100)

    modal: true
    title: "Анимация слоя"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(470, parent ? parent.width - 24 : 470)

    function number(text) { return text.trim() ? Number(text.trim().replace(",", ".")) : NaN }
    function edit(layer) {
        const settings = layer.animation ?? {}
        effect.currentIndex = Math.max(0, effect.indexOfValue(settings.kind ?? ""))
        trigger.currentIndex = Math.max(0, trigger.indexOfValue(settings.tag ?? layer.tag ?? ""))
        operator.currentIndex = Math.max(0, operator.indexOfValue(settings.op ?? "eq"))
        threshold.text = String(settings.limit ?? 1)
        maximum.text = String(settings.max ?? "")
        period.value = settings.periodMs ?? 1000
        direction.currentIndex = settings.reverse ? 1 : 0
        dim.value = settings.dimPercent ?? 25
        open()
    }
    function apply() {
        if (!valid) return
        let settings = null
        if (enabledEffect) {
            settings = { kind: effect.currentValue, tag: trigger.currentValue || "",
                op: operator.currentValue, limit: lower, periodMs: period.value }
            if (interval) settings.max = upper
            if (blink) settings.dimPercent = dim.value
            else settings.reverse = direction.currentIndex === 1
        }
        if (api.configureWidget({ animation: settings })) close()
    }

    contentItem: ColumnLayout {
        spacing: 8
        LabelValue { text: "Эффект"; mode: "muted" }
        ComboBox {
            id: effect
            objectName: "layerAnimationKind"
            Layout.fillWidth: true
            model: [{ id: "", name: "Нет" }, { id: "rotate", name: "Вращение" }, { id: "blink", name: "Мигание" }]
            textRole: "name"; valueRole: "id"
        }
        ColumnLayout {
            visible: form.enabledEffect
            Layout.fillWidth: true
            LabelValue { text: "Тег включения"; mode: "muted" }
            ComboBox {
                id: trigger
                objectName: "layerAnimationTag"
                Layout.fillWidth: true
                model: form.tags; textRole: "name"; valueRole: "id"
            }
            LabelValue { text: "Условие"; mode: "muted" }
            ComboBox {
                id: operator
                objectName: "layerAnimationOperator"
                Layout.fillWidth: true
                model: form.operators; textRole: "name"; valueRole: "id"
            }
            RowLayout {
                Layout.fillWidth: true
                Field { id: threshold; objectName: "layerAnimationLimit"; caption: form.interval ? "Нижняя граница" : "Порог" }
                Field { id: maximum; visible: form.interval; caption: "Верхняя граница" }
            }
            RowLayout {
                Layout.fillWidth: true
                LabelValue { text: "Период цикла, мс"; Layout.fillWidth: true }
                SpinBox {
                    id: period
                    objectName: "layerAnimationPeriod"
                    from: form.blink ? 400 : 100
                    to: 60000
                    stepSize: 100
                    editable: true
                }
            }
            RowLayout {
                visible: !form.blink
                Layout.fillWidth: true
                LabelValue { text: "Направление"; Layout.fillWidth: true }
                ComboBox {
                    id: direction
                    model: ["По часовой", "Против часовой"]
                }
            }
            RowLayout {
                visible: form.blink
                Layout.fillWidth: true
                LabelValue { text: "Непрозрачность в тусклой фазе, %"; Layout.fillWidth: true }
                SpinBox { id: dim; from: 0; to: 95; stepSize: 5; editable: true }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ActionButton { text: "Отмена"; onClicked: form.close() }
            ActionButton {
                objectName: "layerAnimationApply"
                text: "Применить"
                highlighted: true
                enabled: form.valid
                onClicked: form.apply()
            }
        }
    }
}
