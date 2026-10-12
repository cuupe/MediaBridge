import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
ApplicationWindow {
    id: window
    width: 1280; height: 850
    minimumWidth: converter.android ? 0 : 280; minimumHeight: converter.android ? 0 : 260
    visible: true
    title: "MediaBridge"
    readonly property real safeTop: SafeArea.margins.top
    readonly property real safeBottom: SafeArea.margins.bottom
    readonly property real safeLeft: SafeArea.margins.left
    readonly property real safeRight: SafeArea.margins.right
    readonly property real availableWidth: width - safeLeft - safeRight
    readonly property real availableHeight: height - safeTop - safeBottom
    property bool keyboardOpen: Qt.inputMethod.visible
    property real keyboardInset: keyboardOpen && Qt.inputMethod.keyboardRectangle.height > 0
        ? Math.max(0, height - Qt.inputMethod.keyboardRectangle.y) : 0
    readonly property bool shortScreen: availableHeight - keyboardInset < 520
    readonly property real contentGutter: compact ? (availableWidth < 360 || shortScreen ? 12 : 16) : 32
    property int route: 0
    property int processingTab: 0
    property string savingTask: ""
    property string documentAction: ""
    readonly property bool compact: availableWidth < 780 || shortScreen
    readonly property bool dark: converter.settings.theme === "dark" || (converter.settings.theme === "system" && Qt.styleHints.colorScheme === Qt.Dark)
    readonly property color backgroundColor: dark ? "#111318" : "#f4f5f7"
    readonly property color cardColor: dark ? "#1b1e26" : "#ffffff"
    readonly property color lineColor: dark ? "#30343f" : "#e2e5ec"
    readonly property color textColor: dark ? "#eff1f6" : "#242936"
    readonly property color mutedColor: dark ? "#989faf" : "#727b8d"
    readonly property color accentColor: dark ? "#9db5ff" : "#4a6ee5"
    property var routes: ["任务队列","高级处理","设置","运行日志"]
    color: backgroundColor
    font.family: uiFontFamily
    font.pixelSize: 14
    palette.window: cardColor
    palette.windowText: textColor
    palette.base: dark ? "#151820" : "#f7f8fa"
    palette.text: textColor
    palette.placeholderText: mutedColor
    palette.button: dark ? "#252a35" : "#f0f2f7"
    palette.buttonText: textColor
    palette.highlight: accentColor
    palette.highlightedText: dark ? "#17233f" : "#ffffff"
    palette.mid: lineColor
    palette.light: lineColor
    palette.dark: backgroundColor
    function commitEdit() { let focus = window.activeFocusItem; if (focus && typeof focus.editingFinished === "function") focus.editingFinished() }
    function revealEditor() {
        let editor = window.activeFocusItem
        if (!editor || !keyboardOpen) return
        for (let view = editor.parent; view; view = view.parent) {
            if (typeof view.contentY !== "number" || !view.contentItem) continue
            let point = editor.mapToItem(view.contentItem, 0, 0)
            let next = view.contentY
            if (point.y < next + 16) next = point.y - 16
            else if (point.y + editor.height > next + view.height - 16) next = point.y + editor.height - view.height + 16
            view.contentY = Math.max(0, Math.min(next, view.contentHeight - view.height))
        }
    }
    onActiveFocusItemChanged: Qt.callLater(revealEditor)
    onKeyboardInsetChanged: Qt.callLater(revealEditor)
    onKeyboardOpenChanged: Qt.callLater(revealEditor)
    onHeightChanged: Qt.callLater(revealEditor)
    Connections {
        target: Qt.inputMethod
        function onVisibleChanged() { Qt.callLater(window.revealEditor) }
    }
    onClosing: function(close) {
        if (converter.busy) { close.accepted = false; closingDialog.open() }
    }
    Dialog {
        id: closingDialog; parent: Overlay.overlay; modal: true
        width: Math.min(360, window.availableWidth - 32)
        x: window.safeLeft + (window.availableWidth - width) / 2
        y: window.safeTop + Math.max(0, (window.availableHeight - window.keyboardInset - height) / 2)
        title: "活动任务正在处理"; standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: "停止处理并退出？"; wrapMode: Text.WordWrap; width: closingDialog.availableWidth }
        onAccepted: { converter.cancelAll(); exitTimer.start() }
    }
    Timer { id: exitTimer; interval: 100; repeat: true; onTriggered: if (!converter.busy) { stop(); Qt.quit() } }
    FileDialog {
        id: filesDialog; title: "批量添加媒体文件"; fileMode: FileDialog.OpenFiles
        // Android combines every extension filter into MIME restrictions; a wildcard alone lets the core inspect all files.
        nameFilters: converter.android ? ["所有文件 (*)"] : ["所有文件 (*)", "媒体文件 (*.mp4 *.mkv *.mov *.avi *.webm *.ts *.mts *.m2ts *.mp3 *.m4a *.aac *.wav *.flac *.ogg *.opus *.wma)"]
        onAccepted: converter.addFiles(selectedFiles)
    }
    FolderDialog { id: importFolder; title: "导入媒体目录"; onAccepted: converter.addFolder(selectedFolder) }
    FolderDialog { id: outputFolder; title: "选择批量输出目录"; onAccepted: converter.setSetting("outputDirectory",selectedFolder.toString()) }
    FileDialog {
        id: resultDialog; title: "另存已处理结果"; fileMode: FileDialog.SaveFile
        defaultSuffix: converter.taskExtension(window.savingTask)
        onAccepted: converter.exportResult(window.savingTask,selectedFile)
    }
    FileDialog {
        id: documentDialog; title: window.documentAction === "report" ? "导出任务报告" : "导出处理方案"
        fileMode: FileDialog.SaveFile; defaultSuffix: "json"; nameFilters: ["JSON 文件 (*.json)"]
        onAccepted: {
            if (window.documentAction === "report") converter.exportReport(selectedFile)
            else converter.exportProfile(selectedFile)
        }
    }
    FileDialog { id: profileImport; title: "导入处理方案"; fileMode: FileDialog.OpenFile; nameFilters: ["JSON 文件 (*.json)"]; onAccepted: converter.importProfile(selectedFile) }
    Dialog {
        id: profileName; title: "保存自定义方案"; parent: Overlay.overlay; modal: true
        width: Math.min(360, window.availableWidth - 32)
        x: window.safeLeft + (window.availableWidth - width) / 2
        y: window.safeTop + Math.max(0, (window.availableHeight - window.keyboardInset - height) / 2)
        standardButtons: Dialog.Save | Dialog.Cancel
        FormInput { id: profileNameInput; width: profileName.availableWidth; placeholderText: "方案名称"; maximumLength: 64 }
        onAccepted: converter.saveProfile(profileNameInput.text)
    }
    Shortcut { sequence: "Ctrl+O"; onActivated: filesDialog.open() }
    Shortcut { sequence: "Ctrl+Return"; onActivated: converter.startQueue() }
    header: Rectangle {
        objectName: "mobile-header"
        visible: window.compact && !window.keyboardOpen
        color: window.cardColor; height: window.compact ? (window.shortScreen ? 60 : 108) + window.safeTop : 0
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: window.lineColor }
        Label {
            visible: !window.shortScreen
            anchors { left: parent.left; top: parent.top; leftMargin: 20 + window.safeLeft; topMargin: 17 + window.safeTop }
            text: "MediaBridge"; font.pixelSize: 18; font.weight: Font.DemiBold; font.letterSpacing: -0.4
        }
        RowLayout {
            objectName: "mobile-navigation"
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 8; leftMargin: 8 + window.safeLeft; rightMargin: 8 + window.safeRight }
            height: 44; spacing: 4
            Repeater {
                model: window.routes
                ActionButton {
                    required property int index
                    required property string modelData
                    objectName: "mobile-route-" + index
                    Layout.fillWidth: true; Layout.minimumWidth: 0; implicitHeight: 44
                    leftPadding: 4; rightPadding: 4; font.pixelSize: 12
                    surface: window.route === index ? (window.dark ? "#293248" : "#e9edfc") : window.cardColor
                    edge: "transparent"
                    ink: window.route === index ? window.accentColor : window.mutedColor
                    text: modelData
                    onClicked: window.route = index
                }
            }
        }
    }
    RowLayout {
        objectName: "main-content"
        anchors.fill: parent; anchors.bottomMargin: window.keyboardInset; spacing: 0
        Rectangle {
            visible: !window.compact; Layout.fillHeight: true; Layout.preferredWidth: 200
            color: window.dark ? "#171a21" : window.cardColor
            Rectangle { anchors.right: parent.right; height: parent.height; width: 1; color: window.lineColor }
            ColumnLayout {
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 18; topMargin: 32 }
                spacing: 8
                Label {
                    text: "MediaBridge"; font.pixelSize: 19; font.weight: Font.DemiBold
                    font.letterSpacing: -0.5; leftPadding: 14; bottomPadding: 28
                }
                Repeater {
                    model: window.routes
                    Button {
                        id: navigation
                        required property string modelData
                        required property int index
                        objectName: "route-" + index
                        Layout.fillWidth: true; implicitHeight: 44; text: modelData
                        hoverEnabled: true
                        background: Rectangle {
                            radius: 9
                            color: window.route === navigation.index ? (window.dark ? "#293248" : "#e9edfc") :
                                   navigation.hovered ? window.palette.button : "transparent"
                            Behavior on color { ColorAnimation { duration: 120 } }
                        }
                        contentItem: Text {
                            text: navigation.text; font: navigation.font
                            color: window.route === navigation.index ? window.accentColor : window.mutedColor
                            verticalAlignment: Text.AlignVCenter; leftPadding: 14
                        }
                        onClicked: window.route = index
                    }
                }
            }
            ColumnLayout {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 32 }
                spacing: 10; visible: converter.count > 0
                Label { text: converter.activeCount + "  正在处理"; color: window.accentColor; font.pixelSize: 12 }
                Label { text: converter.waitingCount + "  等待处理"; color: window.mutedColor; font.pixelSize: 12 }
                Label { text: converter.completedCount + "  已完成"; color: window.mutedColor; font.pixelSize: 12 }
            }
        }
        Flickable {
            id: workspaceScroll
            objectName: "workspace-scroll"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
            contentWidth: width
            contentHeight: workspaceBody.implicitHeight + 2 * window.contentGutter
            ScrollBar.vertical: ScrollBar {}
            onHeightChanged: returnToBounds()
            ColumnLayout {
            id: workspaceBody
            x: window.contentGutter; y: window.contentGutter
            width: workspaceScroll.width - 2 * window.contentGutter
            spacing: window.shortScreen ? 12 : 20
            readonly property var tools: [pageHeading, quickChoices, schemeToolbar, compactApplyToolbar, profileTransfer, outputToolbar]
            readonly property real toolbarHeight: tools.reduce((total, item) => total + (item.visible ? item.implicitHeight + spacing : 0), 0)
            RowLayout {
                id: pageHeading
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 5
                    Label { Layout.fillWidth: true; Layout.minimumWidth: 0; text: window.routes[window.route]; font.pixelSize: window.compact ? 23 : 26; font.weight: Font.DemiBold }
                }
                ActionButton { visible: window.route === 0 && !converter.android && !window.compact; text: "导入目录"; onClicked: importFolder.open() }
                ActionButton { visible: window.route === 0; text: "添加文件"; primary: true; onClicked: filesDialog.open() }
                ActionButton { visible: window.route === 1 && !window.compact; objectName: "apply-profile"; text: "完成设置"; primary: true; enabled: converter.selectedCount > 0; onClicked: { window.commitEdit(); converter.applyProfile(); window.route = 0 } }
                ActionButton { visible: window.route === 3; text: "导出报告"; onClicked: { window.documentAction = "report"; documentDialog.open() } }
            }
            RowLayout {
                id: quickChoices
                visible: window.route === 0
                Layout.fillWidth: true; spacing: 16
                ChoiceField { label: "输出格式"; showLabel: !window.shortScreen; controlName: "quick-format"; fieldKey: "format"; choices: converter.formats }
                ChoiceField { label: "处理方式"; showLabel: !window.shortScreen; controlName: "quick-mode"; fieldKey: "mode"; choices: [{id:"transcode",label:"转码"},{id:"remux",label:"无损换封装"}] }
            }
            RowLayout {
                id: schemeToolbar
                visible: window.route === 1
                Layout.fillWidth: true; spacing: 8
                FormCombo { id: schemes; Layout.fillWidth: true; Layout.minimumWidth: 0; model: converter.profiles; onActivated: converter.useProfile(currentText) }
                ActionButton { text: "保存方案"; onClicked: profileName.open() }
                ActionButton { visible: !window.compact; text: "重置"; onClicked: converter.resetProfile() }
                ActionButton { visible: !window.compact; text: "删除"; onClicked: converter.deleteProfile(schemes.currentText) }
            }
            RowLayout {
                id: compactApplyToolbar
                visible: window.route === 1 && window.compact; Layout.fillWidth: true
                ActionButton { objectName: "apply-profile-mobile"; text: "完成设置"; primary: true; Layout.fillWidth: true; enabled: converter.selectedCount > 0; onClicked: { window.commitEdit(); converter.applyProfile(); window.route = 0 } }
                ActionButton { text: "重置"; onClicked: converter.resetProfile() }
            }
            StackLayout {
                objectName: "page-stack"
                Layout.fillWidth: true
                Layout.minimumHeight: 180
                Layout.preferredHeight: Math.max(180, workspaceScroll.height - workspaceBody.toolbarHeight - 2 * window.contentGutter)
                Layout.maximumHeight: Layout.preferredHeight
                currentIndex: window.route
                QueuePage {
                    cardColor: window.cardColor; lineColor: window.lineColor; accentColor: window.accentColor
                    onSaveRequested: function(taskId) { window.savingTask = taskId; resultDialog.open() }
                    onEditRequested: window.route = 1
                    onAddRequested: filesDialog.open()
                }
                ProcessingPage { currentTab: window.processingTab; cardColor: window.cardColor; lineColor: window.lineColor }
                SettingsPage { cardColor: window.cardColor; lineColor: window.lineColor; onFolderRequested: outputFolder.open() }
                ScrollView {
                    clip: true
                    TextArea {
                        text: converter.logText || "暂无日志"
                        readOnly: true; selectByMouse: true; wrapMode: TextArea.WrapAnywhere
                        font.pixelSize: 13; padding: 20
                        background: Rectangle { color: window.cardColor; radius: 14; border.color: window.lineColor }
                    }
                }
            }
            RowLayout {
                id: profileTransfer
                visible: window.route === 1; Layout.fillWidth: true
                ActionButton { text: "导入方案"; onClicked: profileImport.open() }
                ActionButton { text: "导出方案"; onClicked: { window.documentAction = "profile"; documentDialog.open() } }
                Label { visible: !window.compact; Layout.fillWidth: true; text: converter.selectedCount + " 个任务已选中"; horizontalAlignment: Text.AlignRight; color: window.mutedColor; font.pixelSize: 12 }
            }
            RowLayout {
                id: outputToolbar
                visible: window.route === 0 && converter.count > 0
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true; Layout.minimumWidth: 0; text: "输出：" + converter.displayName(converter.settings.outputDirectory)
                    color: window.mutedColor; font.pixelSize: 12; elide: Text.ElideMiddle
                }
                ActionButton { text: "输出设置"; implicitHeight: converter.android ? 44 : 30; onClicked: window.route = 2 }
                ActionButton { visible: !window.compact; text: "任务报告"; implicitHeight: 30; onClicked: { window.documentAction = "report"; documentDialog.open() } }
            }
            }
        }
    }
    footer: Rectangle {
        objectName: "queue-footer"
        readonly property bool narrow: window.compact && window.availableWidth < 380
        readonly property real gutter: window.shortScreen ? 8 : window.compact ? 14 : 20
        visible: !window.keyboardOpen
        height: visible ? footerContents.implicitHeight + 2 * gutter + window.safeBottom : 0
        color: window.cardColor
        Rectangle { width: parent.width; height: 1; color: window.lineColor }
        ColumnLayout {
            id: footerContents; objectName: "footer-controls"
            anchors { fill: parent; margins: parent.gutter; leftMargin: parent.gutter + window.safeLeft; rightMargin: parent.gutter + window.safeRight; bottomMargin: parent.gutter + window.safeBottom }
            spacing: 8
            RowLayout {
                visible: window.compact && !window.shortScreen; Layout.fillWidth: true; spacing: 12
                Label { Layout.fillWidth: true; Layout.minimumWidth: 0; text: converter.count > 0 ? converter.status : "就绪"; elide: Text.ElideMiddle; color: window.mutedColor; font.pixelSize: 12 }
                Label { visible: converter.count > 0; text: converter.completedCount + " / " + converter.count; color: window.mutedColor; font.pixelSize: 12 }
            }
            GridLayout {
                Layout.fillWidth: true; columnSpacing: 10; rowSpacing: 8
                columns: window.footer.narrow ? 2 : window.compact ? 3 : 4
                ColumnLayout {
                    visible: !window.compact; Layout.fillWidth: true; Layout.rightMargin: 20; spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; Layout.minimumWidth: 0; text: converter.count > 0 ? converter.status : "就绪"; elide: Text.ElideMiddle; color: window.mutedColor; font.pixelSize: 12 }
                        Label { visible: converter.count > 0; text: converter.completedCount + " / " + converter.count; color: window.mutedColor; font.pixelSize: 12 }
                    }
                    ProgressBar {
                    Layout.fillWidth: true; value: converter.progress
                    background: Rectangle { implicitHeight: 4; radius: 2; color: window.palette.button }
                    contentItem: Item {
                        implicitHeight: 4
                        Rectangle { width: parent.width * converter.progress; height: 4; radius: 2; color: window.accentColor }
                    }
                }
                }
                ActionButton { text: "取消全部"; danger: true; Layout.minimumWidth: 0; Layout.fillWidth: window.compact; enabled: converter.busy || converter.waitingCount > 0; onClicked: converter.cancelAll() }
                ActionButton { text: converter.paused ? "继续排队" : "暂停排队"; Layout.minimumWidth: 0; Layout.fillWidth: window.compact; enabled: converter.busy || converter.waitingCount > 0; onClicked: converter.pauseQueue() }
                ActionButton { objectName: "start-queue"; text: "开始处理"; primary: true; Layout.minimumWidth: 0; Layout.columnSpan: window.footer.narrow ? 2 : 1; Layout.fillWidth: window.compact; enabled: converter.waitingCount > 0; onClicked: { window.commitEdit(); converter.startQueue() } }
            }
        }
    }
    DropArea {
        id: drops; anchors.fill: parent
        onDropped: function(drop) { if (drop.hasUrls) { converter.addFiles(drop.urls); window.route = 0; drop.acceptProposedAction() } }
    }
    Rectangle {
        anchors.fill: parent; visible: drops.containsDrag
        color: Qt.alpha(window.backgroundColor,0.94); border.width: 3; border.color: window.accentColor; radius: 14
        Label { anchors.centerIn: parent; text: "释放以添加文件"; color: window.accentColor; font.pixelSize: window.compact ? 21 : 32; font.weight: Font.DemiBold }
    }
}
