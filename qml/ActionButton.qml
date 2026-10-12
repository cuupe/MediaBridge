import QtQuick
import QtQuick.Controls
Button {
    id: button
    property bool primary: false
    property bool danger: false
    property color surface: palette.button
    property color edge: palette.mid
    property color ink: palette.buttonText
    property color accent: palette.highlight
    implicitHeight: Math.max(converter.android ? 48 : 40, contentItem.implicitHeight + topPadding + bottomPadding)
    font.pixelSize: 13
    leftPadding: 16; rightPadding: 16
    hoverEnabled: true
    contentItem: Text {
        text: button.text; font: button.font
        color: !button.enabled ? Qt.alpha(button.ink, 0.38) : button.primary ? button.palette.highlightedText : button.danger ? "#db667c" : button.ink
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 8
        color: button.primary ? (!button.enabled ? Qt.alpha(button.accent, 0.15) : button.down ? Qt.darker(button.accent, 1.12) : button.hovered ? Qt.lighter(button.accent, 1.06) : button.accent) :
               button.down ? button.edge : button.hovered && button.enabled ? Qt.tint(button.surface, "#10ffffff") : button.surface
        border.color: button.activeFocus ? button.accent : button.primary ? "transparent" : button.edge
        Behavior on color { ColorAnimation { duration: 120 } }
    }
}
