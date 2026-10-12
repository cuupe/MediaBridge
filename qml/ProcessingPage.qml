import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Flickable {
    id: page
    property int currentTab: 0
    property bool compact: width < 660
    property color cardColor: "#1b1e26"
    property color lineColor: "#30343f"
    readonly property bool transcode: converter.profile.mode === "transcode"
    contentHeight: body.implicitHeight + 32
    clip: true
    ScrollBar.vertical: ScrollBar {}
    ColumnLayout {
        id: body
        width: parent.width - 8
        spacing: 16
        Panel {
            heading: "输出方案"
            surfaceColor: page.cardColor; lineColor: page.lineColor
            GridLayout {
                columns: page.compact ? 1 : 2
                Layout.fillWidth: true; columnSpacing: 20; rowSpacing: 14
                ChoiceField { label: "目标格式"; fieldKey: "format"; choices: converter.formats }
                ChoiceField {
                    label: "处理方式"; fieldKey: "mode"
                    choices: [{id:"transcode",label:"转码 / 提取音频"},{id:"remux",label:"无损换封装"}]
                }
            }
        }
        TabBar {
            id: advancedTabs
            currentIndex: page.currentTab
            Layout.fillWidth: true; padding: 4; spacing: 4
            background: Rectangle { color: palette.button; radius: 10 }
            Repeater {
                model: ["视频","音频","时间","元数据"]
                TabButton {
                    id: sectionTab
                    required property string modelData
                    text: modelData; implicitHeight: 34
                    background: Rectangle { radius: 7; color: sectionTab.checked ? palette.base : "transparent" }
                    contentItem: Text {
                        text: sectionTab.text; font: sectionTab.font
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        color: sectionTab.checked ? palette.highlight : palette.placeholderText
                    }
                }
            }
        }
        Panel {
            visible: advancedTabs.currentIndex === 0
            heading: "编码与画面"
            surfaceColor: page.cardColor; lineColor: page.lineColor
            GridLayout {
                Layout.fillWidth: true; columns: page.compact ? 1 : 3; columnSpacing: 18; rowSpacing: 16
                enabled: page.transcode
                ChoiceField { label: "视频编码器"; fieldKey: "videoEncoder"; choices: converter.videoEncoders }
                ChoiceField { label: "编码速度"; fieldKey: "encoderPreset"; choices: ["ultrafast","superfast","veryfast","faster","fast","medium","slow","slower","veryslow"].map(x => ({id:x,label:x})) }
                ChoiceField { label: "画面适配"; fieldKey: "fit"; choices: [{id:"fit",label:"等比留边"},{id:"fill",label:"等比填满"},{id:"stretch",label:"拉伸"}] }
                Repeater {
                    model: [
                        {key:"width",name:"宽度 / px",min:0,max:16384,d:0},{key:"height",name:"高度 / px",min:0,max:16384,d:0},
                        {key:"fps",name:"帧率 / fps",min:0,max:240,d:3},
                        {key:"videoKbps",name:"视频码率 / kbps",min:100,max:100000,d:0},
                        {key:"crf",name:"CRF",min:-1,max:51,d:0},
                        {key:"gop",name:"关键帧间隔 / 帧",min:1,max:600,d:0}]
                    NumberField { required property var modelData; label: modelData.name; fieldKey: modelData.key; low: modelData.min; high: modelData.max; decimals: modelData.d }
                }
            }
            GridLayout {
                Layout.fillWidth: true; columns: page.compact ? 1 : 3; columnSpacing: 18; rowSpacing: 14; enabled: page.transcode
                ChoiceField { label: "顺时针旋转"; fieldKey: "rotation"; choices: [0,90,180,270].map(x=>({id:x,label:String(x)+"°"})) }
                ToggleField { text: "自动校正手机视频方向"; fieldKey: "autoRotate" }
                ToggleField { text: "去隔行"; fieldKey: "deinterlace" }
                ToggleField { text: "水平翻转"; fieldKey: "flipHorizontal" }
                ToggleField { text: "垂直翻转"; fieldKey: "flipVertical" }
                ToggleField { text: "不输出视频"; fieldKey: "disableVideo" }
            }
        }
        Panel {
            visible: advancedTabs.currentIndex === 0
            heading: "裁剪与色彩"
            surfaceColor: page.cardColor; lineColor: page.lineColor
            GridLayout {
                Layout.fillWidth: true; columns: page.compact ? 2 : 4; columnSpacing: 18; rowSpacing: 14; enabled: page.transcode
                Repeater {
                    model: [
                        {key:"cropX",name:"裁剪 X",min:0,max:16384,d:0},{key:"cropY",name:"裁剪 Y",min:0,max:16384,d:0},
                        {key:"cropWidth",name:"裁剪宽度",min:0,max:16384,d:0},{key:"cropHeight",name:"裁剪高度",min:0,max:16384,d:0},
                        {key:"brightness",name:"亮度",min:-1,max:1,d:2},{key:"contrast",name:"对比度",min:0.1,max:3,d:2},
                        {key:"saturation",name:"饱和度",min:0,max:3,d:2},{key:"threads",name:"编码线程",min:1,max:32,d:0}]
                    NumberField { required property var modelData; label: modelData.name; fieldKey: modelData.key; low: modelData.min; high: modelData.max; decimals: modelData.d }
                }
            }
        }
        Panel {
            visible: advancedTabs.currentIndex === 1
            heading: "音频编码与响度"
            surfaceColor: page.cardColor; lineColor: page.lineColor
            GridLayout {
                Layout.fillWidth: true; columns: page.compact ? 1 : 2; columnSpacing: 18; rowSpacing: 16; enabled: page.transcode
                ChoiceField { label: "音频编码器"; fieldKey: "audioEncoder"; choices: converter.audioEncoders }
                ChoiceField { label: "声道"; fieldKey: "channels"; choices: [{id:0,label:"保留原声道"},{id:1,label:"单声道"},{id:2,label:"双声道"}] }
                ChoiceField { label: "采样率"; fieldKey: "sampleRate"; choices: [0,8000,16000,22050,32000,44100,48000,96000,192000].map(x=>({id:x,label:x===0?"原始 / 自动":String(x)+" Hz"})) }
                NumberField { label: "音频码率 / kbps"; fieldKey: "audioKbps"; low: 8; high: 1536; decimals: 0 }
                NumberField { label: "音量增益 / dB"; fieldKey: "volumeDb"; low: -60; high: 24 }
                NumberField { label: "目标响度 / LUFS"; fieldKey: "loudness"; low: -40; high: -5 }
                ToggleField { text: "响度标准化"; fieldKey: "normalizeAudio" }
                ToggleField { text: "不输出音频"; fieldKey: "disableAudio" }
            }
        }
        Panel {
            visible: advancedTabs.currentIndex === 2
            heading: "截取与变速"
            surfaceColor: page.cardColor; lineColor: page.lineColor
            GridLayout {
                Layout.fillWidth: true; columns: page.compact ? 1 : 2; columnSpacing: 18; rowSpacing: 16; enabled: page.transcode
                NumberField { label: "开始时间 / 秒"; fieldKey: "start"; low: 0; high: 1000000000; decimals: 3 }
                NumberField { label: "截取时长 / 秒"; fieldKey: "duration"; low: 0; high: 1000000000; decimals: 3 }
                NumberField { label: "播放速度"; fieldKey: "speed"; low: 0.25; high: 4 }
                Item { visible: !page.compact; Layout.fillWidth: true }
                NumberField { label: "淡入 / 秒"; fieldKey: "fadeIn"; low: 0; high: 3600 }
                NumberField { label: "淡出 / 秒"; fieldKey: "fadeOut"; low: 0; high: 3600 }
            }
        }
        Panel {
            visible: advancedTabs.currentIndex === 3
            heading: "流选择与元数据"
            surfaceColor: page.cardColor; lineColor: page.lineColor
            GridLayout {
                Layout.fillWidth: true; columns: page.compact ? 1 : 2; columnSpacing: 18; rowSpacing: 16
                NumberField { label: "视频流索引"; fieldKey: "videoStream"; low: -1; high: 100000; decimals: 0 }
                NumberField { label: "音频流索引"; fieldKey: "audioStream"; low: -1; high: 100000; decimals: 0 }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: "标题"; font.pixelSize: 12; opacity: 0.7 }
                    FormInput { Layout.fillWidth: true; text: converter.profile.title; selectByMouse: true; onEditingFinished: converter.setProfileValue("title",text) }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: "作者"; font.pixelSize: 12; opacity: 0.7 }
                    FormInput { Layout.fillWidth: true; text: converter.profile.artist; selectByMouse: true; onEditingFinished: converter.setProfileValue("artist",text) }
                }
                ToggleField { text: "复制源元数据"; fieldKey: "copyMetadata" }
                ToggleField { text: "复制字幕（仅无损封装）"; fieldKey: "copySubtitles"; enabled: !page.transcode }
            }
        }
    }
}
