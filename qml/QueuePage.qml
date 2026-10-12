import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    id: page
    property bool compact: width < 600
    property color cardColor: "#1b1e26"
    property color lineColor: "#30343f"
    property color accentColor: "#9db5ff"
    signal saveRequested(string taskId)
    signal editRequested()
    signal addRequested()
    ColumnLayout {
        anchors.fill: parent
        spacing: 16
        RowLayout {
            visible: converter.count > 0
            Layout.fillWidth: true
            CheckBox { text: "全选"; checked: converter.count > 0 && converter.selectedCount === converter.count; onClicked: converter.selectAll(checked) }
            Label { text: converter.selectedCount + " 项已选"; opacity: 0.65; font.pixelSize: 12; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
            ActionButton { text: "重试"; leftPadding: page.compact ? 8 : 16; rightPadding: leftPadding; enabled: converter.selectedCount > 0; onClicked: converter.retrySelected() }
            ActionButton { text: "移除"; leftPadding: page.compact ? 8 : 16; rightPadding: leftPadding; enabled: converter.selectedCount > 0; onClicked: converter.removeSelected() }
            ActionButton { visible: !page.compact; text: "清理已完成"; onClicked: converter.clearFinished() }
        }
        ListView {
            id: list
            objectName: "queue-list"
            Layout.fillWidth: true; Layout.fillHeight: true
            model: converter.tasks
            clip: true; spacing: 12
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property string taskId
                required property string fileName
                required property string inputUrl
                required property string outputUrl
                required property string taskState
                required property string stateText
                required property real taskProgress
                required property bool taskSelected
                required property string taskDetails
                required property string taskMessage
                required property string sourceSummary
                required property string taskSummary
                required property string localResult
                property bool expanded: false
                width: list.width - 8
                height: content.implicitHeight + 36
                color: row.taskSelected ? Qt.tint(page.cardColor,Qt.alpha(page.accentColor,0.035)) : page.cardColor; radius: 12
                border.color: taskSelected ? Qt.alpha(page.accentColor,0.5) : page.lineColor
                ColumnLayout {
                    id: content
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 18 }
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        CheckBox { checked: row.taskSelected; onClicked: converter.setSelected(row.taskId,checked) }
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 4
                            Label { text: row.fileName; font.weight: Font.DemiBold; elide: Text.ElideMiddle; Layout.fillWidth: true }
                            Label { text: row.sourceSummary || (row.taskState === "queued" || row.taskState === "inspecting" || row.taskState === "running" ? "正在识别格式…" : "未识别媒体格式"); font.pixelSize: 11; opacity: 0.6; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label { text: "→ " + row.taskSummary; font.pixelSize: 11; opacity: 0.6; elide: Text.ElideRight; Layout.fillWidth: true }
                        }
                        Rectangle {
                            width: 64; height: 26; radius: 6
                            color: Qt.alpha(row.taskState === "failed" ? "#db667c" : row.taskState === "done" ? "#56b993" : page.accentColor,0.12)
                            Label {
                                anchors.centerIn: parent; text: row.stateText; font.pixelSize: 11
                                color: row.taskState === "failed" ? "#db667c" : row.taskState === "done" ? "#56b993" : page.accentColor
                            }
                        }
                    }
                    ProgressBar { Layout.fillWidth: true; visible: row.taskState === "running"; value: row.taskProgress; indeterminate: row.taskProgress === 0 }
                    Label {
                        visible: row.taskMessage !== ""; Layout.fillWidth: true; text: row.taskMessage
                        color: row.taskState === "failed" ? "#ef8f9b" : palette.text
                        opacity: 0.65; font.pixelSize: 11; elide: Text.ElideMiddle
                        ToolTip.visible: messageHover.hovered; ToolTip.text: text
                        HoverHandler { id: messageHover }
                    }
                    Flow {
                        Layout.fillWidth: true; spacing: 6
                        height: childrenRect.height
                        ActionButton { text: "信息"; implicitHeight: converter.android ? 44 : 29; onClicked: { row.expanded = !row.expanded; if (row.taskDetails === "") converter.inspectTask(row.taskId) } }
                        ActionButton { text: "编辑"; implicitHeight: converter.android ? 44 : 29; enabled: row.taskState !== "running" && row.taskState !== "inspecting"; onClicked: { converter.selectAll(false); converter.setSelected(row.taskId,true); converter.loadTaskProfile(row.taskId); page.editRequested() } }
                        ActionButton { text: "↑"; Accessible.name: "上移任务"; implicitHeight: converter.android ? 44 : 29; implicitWidth: 32; leftPadding: 4; rightPadding: 4; font.pixelSize: 16; onClicked: converter.moveTask(row.taskId,-1) }
                        ActionButton { text: "↓"; Accessible.name: "下移任务"; implicitHeight: converter.android ? 44 : 29; implicitWidth: 32; leftPadding: 4; rightPadding: 4; font.pixelSize: 16; onClicked: converter.moveTask(row.taskId,1) }
                        ActionButton { text: "取消"; implicitHeight: converter.android ? 44 : 29; visible: row.taskState === "queued" || row.taskState === "running" || row.taskState === "inspecting"; onClicked: converter.cancelTask(row.taskId) }
                        ActionButton { text: "打开结果"; implicitHeight: converter.android ? 44 : 29; visible: row.taskState === "done" || row.taskState === "pending"; onClicked: converter.openResult(row.taskId) }
                        ActionButton { text: "另存结果"; primary: true; implicitHeight: converter.android ? 44 : 29; visible: row.taskState === "pending"; onClicked: page.saveRequested(row.taskId) }
                    }
                    Label { visible: row.expanded; text: row.taskDetails || (row.taskState === "inspecting" ? "正在分析文件…" : "未取得媒体信息"); Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; opacity: 0.75 }
                    Label {
                        visible: row.expanded; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                        text: (row.taskMessage ? row.taskMessage + "\n" : "") + "输入：" + converter.displayName(row.inputUrl) + (row.outputUrl ? "\n输出：" + converter.displayName(row.outputUrl) : "")
                        font.pixelSize: 11; opacity: 0.65
                    }
                }
            }
        }
    }
    Rectangle {
        visible: converter.count === 0
        TapHandler { onTapped: page.addRequested() }
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: parent.bottom; topMargin: 0 }
        color: page.cardColor; radius: 14; border.color: page.lineColor
        Label {
            anchors.centerIn: parent; text: converter.android ? "选择文件" : "拖入文件"
            font.pixelSize: page.compact ? 20 : 23; color: palette.placeholderText
        }
    }
}
