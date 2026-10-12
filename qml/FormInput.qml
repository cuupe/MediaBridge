import QtQuick
import QtQuick.Controls
TextField {
    id: control
    implicitHeight: Math.max(converter.android ? 48 : 42, contentHeight + topPadding + bottomPadding)
    leftPadding: 12; rightPadding: 12
    font.pixelSize: 13
    selectByMouse: true
    selectionColor: Qt.alpha(palette.highlight, 0.3)
    selectedTextColor: palette.text
    background: Rectangle {
        radius: 8; color: control.palette.base
        border.color: control.activeFocus ? control.palette.highlight : control.palette.mid
        Behavior on border.color { ColorAnimation { duration: 120 } }
    }
}
