import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: choice
    property string label
    property bool showLabel: true
    property string controlName: "combo-" + fieldKey
    property string fieldKey
    property bool setting: false
    property var choices: []
    property var value: setting ? converter.settings[fieldKey] : converter.profile[fieldKey]
    function findValue() {
        for (let i = 0; i < choices.length; i++) if (String(choices[i].id) === String(value)) return i
        return 0
    }
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    spacing: 6
    Label { visible: choice.showLabel; text: choice.label; font.pixelSize: 12; opacity: 0.72; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    FormCombo {
        id: combo
        objectName: choice.controlName
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        model: choice.choices
        textRole: "label"
        currentIndex: choice.findValue()
        Accessible.name: choice.label
        onActivated: {
            if (choice.setting) converter.setSetting(choice.fieldKey, choice.choices[currentIndex].id)
            else converter.setProfileValue(choice.fieldKey, choice.choices[currentIndex].id)
            currentIndex = choice.findValue()
        }
        Connections { target: choice; function onValueChanged() { combo.currentIndex = choice.findValue() } }
    }
}
