import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var api
    property var screen: ({})
    property url pendingImage
    property bool clearImage: false
    readonly property var sizes: [
        { label: "Свой размер" },
        { label: "1200 × 692", w: 1200, h: 692 },
        { label: "1024 × 768", w: 1024, h: 768 },
        { label: "1280 × 720", w: 1280, h: 720 },
        { label: "1920 × 1080", w: 1920, h: 1080 },
        { label: "2560 × 1440", w: 2560, h: 1440 },
        { label: "3840 × 2160", w: 3840, h: 2160 }
    ]
    title: "Свойства экрана"
    modal: true
    anchors.centerIn: parent
    width: Math.min(560, parent ? parent.width - 32 : 560)
    function edit(details) {
        screen = details
        screenName.text = details.name || ""
        canvasWidth.text = String(details.width ?? 1200)
        canvasHeight.text = String(details.height ?? 692)
        background.text = details.background || "#f5f8fa"
        gridStep.text = String(details.gridStep ?? 10)
        fit.currentIndex = details.backgroundFit === "contain" ? 1 : details.backgroundFit === "stretch" ? 2 : 0
        operatorFit.currentIndex = details.operatorFit === "fit" ? 1 : 0
        scaleItems.checked = true
        pendingImage = ""
        clearImage = false
        open()
    }
    function save() {
        const fields = { name: screenName.text, width: Number(canvasWidth.text), height: Number(canvasHeight.text),
            background: background.text.trim() || "#f5f8fa", gridStep: Number(gridStep.text),
            backgroundFit: ["cover", "contain", "stretch"][fit.currentIndex],
            operatorFit: operatorFit.currentIndex === 1 ? "fit" : "fill" }
        if (clearImage) fields.backgroundImage = ""
        if (api.setScreenProperties(screen.id, fields, scaleItems.checked, pendingImage))
            close()
    }
    ColorDialog {
        id: colors
        title: "Цвет фона экрана"
        onAccepted: {
            const color = selectedColor
            background.setColor("#" + [color.r, color.g, color.b].map(channel =>
                Math.round(channel * 255).toString(16).padStart(2, "0")).join(""))
        }
    }
    FileDialog {
        id: pictures
        objectName: "screenImagePicker"
        title: "Выбрать фоновое изображение"
        nameFilters: ["Изображения (*.png *.jpg *.jpeg *.bmp)"]
        onAccepted: { dialog.pendingImage = selectedFile; dialog.clearImage = false }
    }
    contentItem: ColumnLayout {
        spacing: 10
        Field { id: screenName; caption: "Название экрана" }
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                LabelValue { text: "Размер мнемосхемы"; mode: "muted" }
                ComboBox {
                    Layout.fillWidth: true
                    model: dialog.sizes
                    textRole: "label"
                    currentIndex: Math.max(0, dialog.sizes.findIndex(size =>
                        size.w === Number(canvasWidth.text) && size.h === Number(canvasHeight.text)))
                    onActivated: index => {
                        if (index === 0) return
                        canvasWidth.text = String(dialog.sizes[index].w)
                        canvasHeight.text = String(dialog.sizes[index].h)
                    }
                }
            }
            Field { id: canvasWidth; objectName: "screenWidth"; caption: "Ширина, px"; Layout.preferredWidth: 105 }
            Field { id: canvasHeight; objectName: "screenHeight"; caption: "Высота, px"; Layout.preferredWidth: 105 }
            Field { id: gridStep; objectName: "screenGrid"; caption: "Шаг сетки, px"; Layout.preferredWidth: 95 }
        }
        RowLayout {
            Layout.fillWidth: true
            CheckBox {
                id: scaleItems
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                text: "Масштабировать элементы"
                ToolTip.visible: hovered
                ToolTip.text: "Масштабировать размещённые элементы при изменении размера экрана"
            }
            ColumnLayout {
                Layout.fillWidth: true
                LabelValue { text: "В операторском режиме"; mode: "muted" }
                ComboBox {
                    id: operatorFit
                    objectName: "screenOperatorFit"
                    Layout.fillWidth: true
                    model: ["Заполнять окно", "Сохранять пропорции экрана"]
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                LabelValue { text: "Цвет фона"; mode: "muted" }
                ColorField {
                    id: background
                    objectName: "screenBackground"
                    Layout.fillWidth: true
                    caption: "Цвет фона экрана"
                    onChoose: {
                        colors.selectedColor = /^#[0-9a-fA-F]{6}$/.test(background.text.trim()) ? background.text : "#f5f8fa"
                        colors.open()
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                LabelValue { text: "Размещение изображения"; mode: "muted" }
                ComboBox {
                    id: fit
                    Layout.fillWidth: true
                    enabled: !!dialog.pendingImage.toString() || (!dialog.clearImage && !!dialog.screen.backgroundImage)
                    model: ["Заполнить с обрезкой", "Вписать целиком", "Растянуть"]
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                objectName: "screenChooseImage"
                text: "Выбрать фон…"
                enabled: !!dialog.api.path
                onClicked: pictures.open()
            }
            LabelValue {
                text: dialog.pendingImage.toString() ? decodeURIComponent(dialog.pendingImage.toString().split("/").pop()) :
                    !dialog.clearImage && dialog.screen.backgroundImage ? "Фоновое изображение" :
                    dialog.api.path ? "Однотонный фон" : "Для изображения сначала сохраните проект"
                Layout.fillWidth: true
                mode: "muted"
            }
            ActionButton {
                objectName: "screenRemoveImage"
                text: "Убрать изображение"
                enabled: fit.enabled
                onClicked: { dialog.pendingImage = ""; dialog.clearImage = true }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ActionButton { text: "Отмена"; onClicked: dialog.close() }
            ActionButton { objectName: "screenPropertiesSave"; text: "Применить"; highlighted: true; onClicked: dialog.save() }
        }
    }
}
