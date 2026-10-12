import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Flickable {
    id: page
    property bool compact: width < 660
    property color cardColor: "#1b1e26"
    property color lineColor: "#30343f"
    signal folderRequested()
    contentHeight: body.implicitHeight + 32; clip: true
    ScrollBar.vertical: ScrollBar {}
    ColumnLayout {
        id: body
        width: parent.width - 8; spacing: 16
        Panel {
            heading: "输出与命名"
            surfaceColor: page.cardColor; lineColor: page.lineColor
            GridLayout {
                columns: page.compact ? 2 : 3
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.columnSpan: page.compact ? 2 : 1; text: converter.displayName(converter.settings.outputDirectory); elide: Text.ElideMiddle; font.pixelSize: 13 }
                ActionButton { Layout.fillWidth: page.compact; text: "选择目录"; onClicked: page.folderRequested() }
                ActionButton { text: "清除"; onClicked: converter.setSetting("outputDirectory","") }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Label { text: "文件命名"; font.pixelSize: 12; opacity: 0.7 }
                FormInput { Layout.fillWidth: true; text: converter.settings.namingTemplate; placeholderText: "{name}_{index}"; selectByMouse: true; onEditingFinished: converter.setSetting("namingTemplate",text) }
            }
            ChoiceField { label: "同名文件处理"; fieldKey: "conflict"; setting: true; choices: [{id:"rename",label:"自动重命名"},{id:"skip",label:"跳过"},{id:"overwrite",label:"覆盖"}] }
        }
        Panel {
            heading: "队列与性能"
            surfaceColor: page.cardColor; lineColor: page.lineColor
            GridLayout {
                Layout.fillWidth: true; columns: page.compact ? 1 : 2; columnSpacing: 20; rowSpacing: 14
                NumberField { label: "并行任务数"; fieldKey: "parallel"; setting: true; low: 1; high: 4; decimals: 0 }
                NumberField { label: "默认编码线程"; fieldKey: "threads"; setting: true; low: 1; high: 32; decimals: 0 }
                ToggleField { text: "导入文件夹时包含子目录"; fieldKey: "recursive"; setting: true }
                ToggleField { text: "启动时恢复队列与历史"; fieldKey: "restoreQueue"; setting: true }
            }
        }
        Panel {
            heading: "外观"; surfaceColor: page.cardColor; lineColor: page.lineColor
            ChoiceField { label: "主题"; fieldKey: "theme"; setting: true; choices: [{id:"dark",label:"深色"},{id:"light",label:"浅色"},{id:"system",label:"跟随系统"}] }
            ActionButton { text: "恢复默认设置"; onClicked: converter.resetSettings() }
        }
    }
}
