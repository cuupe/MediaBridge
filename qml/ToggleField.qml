import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
CheckBox {
    id: toggle
    property string fieldKey
    property bool setting: false
    property var value: setting ? converter.settings[fieldKey] : converter.profile[fieldKey]
    checked: Boolean(value)
    Layout.fillWidth: true
    font.pixelSize: 13
    onClicked: {
        if (setting) converter.setSetting(fieldKey, checked)
        else converter.setProfileValue(fieldKey, checked)
    }
    onValueChanged: checked = Boolean(value)
}
