import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: field
    property string label
    property string fieldKey
    property bool setting: false
    property real low: -1000000000
    property real high: 1000000000
    property int decimals: 2
    property string hint: ""
    property var value: setting ? converter.settings[fieldKey] : converter.profile[fieldKey]
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    spacing: 6
    Label { text: field.label; font.pixelSize: 12; opacity: 0.72; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    FormInput {
        id: input
        objectName: "field-" + field.fieldKey
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        text: String(field.value)
        placeholderText: field.hint
        selectByMouse: true
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        validator: DoubleValidator { bottom: field.low; top: field.high; decimals: field.decimals; locale: "C"; notation: DoubleValidator.StandardNotation }
        Accessible.name: field.label
        onEditingFinished: {
            if (acceptableInput) {
                if (field.setting) converter.setSetting(field.fieldKey, Number(text))
                else converter.setProfileValue(field.fieldKey, Number(text))
            } else text = String(field.value)
        }
        Connections { target: field; function onValueChanged() { if (!input.activeFocus) input.text = String(field.value) } }
    }
}
