import QtQuick
import QtQuick.Controls
ComboBox {
    id: control
    implicitHeight: Math.max(converter.android ? 48 : 42, implicitContentHeight + topPadding + bottomPadding)
    leftPadding: 12; rightPadding: 32
    font.pixelSize: 13
    popup: Popup {
        y: control.height
        width: control.width
        padding: 4
        implicitHeight: Math.min(contentItem.implicitHeight + 2 * padding,
                                 Math.max(60, ApplicationWindow.window.availableHeight - ApplicationWindow.window.keyboardInset - 16))
        topMargin: ApplicationWindow.window.safeTop + 8
        bottomMargin: ApplicationWindow.window.safeBottom + ApplicationWindow.window.keyboardInset + 8
        leftMargin: ApplicationWindow.window.safeLeft + 8
        rightMargin: ApplicationWindow.window.safeRight + 8
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle { color: control.palette.base; border.color: control.palette.mid; radius: 8 }
    }
    indicator: Canvas {
        width: 16; height: 16
        x: control.width - width - 12; y: (control.height - height) / 2
        property color stroke: control.palette.placeholderText
        onStrokeChanged: requestPaint()
        onPaint: {
            let context = getContext("2d")
            context.clearRect(0, 0, width, height)
            context.strokeStyle = stroke
            context.lineWidth = 1.5; context.lineCap = "round"; context.lineJoin = "round"
            context.beginPath(); context.moveTo(4, 6); context.lineTo(8, 10); context.lineTo(12, 6); context.stroke()
        }
    }
    background: Rectangle {
        radius: 8; color: control.palette.base
        border.color: control.activeFocus ? control.palette.highlight : control.palette.mid
        Behavior on border.color { ColorAnimation { duration: 120 } }
    }
}
