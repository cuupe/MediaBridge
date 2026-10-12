import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: section
    property string heading
    property color surfaceColor: "#1b1e26"
    property color lineColor: "#30343f"
    readonly property real inset: width < 400 ? 16 : 24
    default property alias contents: inner.data
    Layout.fillWidth: true
    implicitHeight: inner.implicitHeight + 2 * inset
    color: surfaceColor
    radius: 14
    border.color: lineColor
    ColumnLayout {
        id: inner
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: section.inset }
        spacing: 20
        Label { text: section.heading; font.pixelSize: 15; font.weight: Font.DemiBold }
    }
}
