import bb.cascades 1.3
import bb.system 1.2

Page {
    id: settingsPage
    signal done()

    titleBar: TitleBar {
        title: qsTr("Settings")
        dismissAction: ActionItem {
            title: qsTr("Close")
            onTriggered: settingsPage.done()
        }
    }

    ScrollView {
        Container {
            leftPadding: 20
            rightPadding: 20
            topPadding: 20
            bottomPadding: 20

            Container {
                visible: browser.restartNeeded
                bottomPadding: 20
                Label {
                    text: qsTr("Some changes apply after the engine restarts.")
                    multiline: true
                }
                Button {
                    text: qsTr("Restart engine now")
                    onClicked: {
                        browser.restartEngine()
                        settingsPage.done()
                    }
                }
            }

            Header {
                title: qsTr("Browsing")
            }
            DropDown {
                title: qsTr("Search engine")
                Option { text: "DuckDuckGo"; value: "duckduckgo" }
                Option { text: qsTr("DuckDuckGo Lite (no JavaScript)"); value: "ddglite" }
                Option { text: "Google"; value: "google" }
                Option { text: "Bing"; value: "bing" }
                Option { text: "Wikipedia"; value: "wikipedia" }
                id: searchDropDown
                onCreationCompleted: selectByValue(searchDropDown, browser.setting("search"))
                onSelectedValueChanged: browser.setSetting("search", selectedValue)
            }
            Label {
                text: qsTr("Home page (empty for the start page)")
                topMargin: 20
            }
            TextField {
                inputMode: TextFieldInputMode.Url
                hintText: qsTr("Start page")
                text: browser.setting("homepage")
                onTextChanged: browser.setSetting("homepage", text)
            }
            CheckBox {
                topMargin: 20
                text: qsTr("Address bar suggestions from bookmarks and history")
                checked: browser.setting("suggestions")
                onCheckedChanged: browser.setSetting("suggestions", checked)
            }
            CheckBox {
                topMargin: 20
                text: qsTr("Swipe sideways to go back and forward")
                checked: browser.setting("swipeNavigation")
                onCheckedChanged: browser.setSetting("swipeNavigation", checked)
            }
            CheckBox {
                topMargin: 20
                text: qsTr("Request desktop sites")
                checked: browser.setting("desktop")
                onCheckedChanged: browser.setSetting("desktop", checked)
            }
            CheckBox {
                topMargin: 20
                text: qsTr("JavaScript")
                checked: browser.setting("javascript")
                onCheckedChanged: browser.setSetting("javascript", checked)
            }
            Label {
                text: qsTr("Long-press menus, text selection and find in page need JavaScript.")
                multiline: true
                textStyle.base: SystemDefaults.TextStyles.SubtitleText
            }
            CheckBox {
                topMargin: 20
                text: qsTr("Load images")
                checked: browser.setting("images")
                onCheckedChanged: browser.setSetting("images", checked)
            }

            Header {
                topMargin: 30
                title: qsTr("Storage")
            }
            Label {
                text: qsTr("Downloads are saved to %1").arg(downloads.folder)
                multiline: true
                textStyle.base: SystemDefaults.TextStyles.SubtitleText
            }
            Button {
                topMargin: 20
                horizontalAlignment: HorizontalAlignment.Fill
                text: qsTr("Clear history (%1)").arg(history.count)
                enabled: history.count > 0
                onClicked: clearHistoryDialog.show()
            }
            Button {
                horizontalAlignment: HorizontalAlignment.Fill
                text: qsTr("Clear cache")
                enabled: browser.engineState == "running"
                onClicked: browser.clearCache()
            }
            Button {
                horizontalAlignment: HorizontalAlignment.Fill
                text: qsTr("Clear cookies")
                enabled: browser.engineState == "running"
                onClicked: browser.clearCookies()
            }

            Header {
                topMargin: 30
                title: qsTr("Engine")
            }
            DropDown {
                title: qsTr("Rendering resolution")
                Option { text: qsTr("Fastest (3/8)"); value: 0 }
                Option { text: qsTr("Fast (1/2)"); value: 1 }
                Option { text: qsTr("Sharp (3/4)"); value: 2 }
                Option { text: qsTr("Native"); value: 3 }
                id: qualityDropDown
                onCreationCompleted: selectByValue(qualityDropDown, browser.setting("quality"))
                onSelectedValueChanged: browser.setSetting("quality", selectedValue)
            }
            DropDown {
                title: qsTr("Frame rate cap")
                Option { text: qsTr("Automatic (45 fps on quad-core, 30 on dual-core)"); value: 0 }
                Option { text: "30 fps"; value: 30 }
                Option { text: "45 fps"; value: 45 }
                Option { text: "60 fps"; value: 60 }
                id: fpsDropDown
                onCreationCompleted: selectByValue(fpsDropDown, browser.setting("fps"))
                onSelectedValueChanged: browser.setSetting("fps", selectedValue)
            }
            CheckBox {
                topMargin: 20
                text: qsTr("Lighter WebAssembly (less CPU while web apps start)")
                checked: browser.setting("wasmLite")
                onCheckedChanged: browser.setSetting("wasmLite", checked)
            }
            CheckBox {
                topMargin: 20
                text: qsTr("Pause pages while the browser is minimized")
                checked: browser.setting("pauseInBackground")
                onCheckedChanged: browser.setSetting("pauseInBackground", checked)
            }
            Label {
                text: qsTr("Turn this off to keep music or calls in a page playing in the background.")
                multiline: true
                textStyle.base: SystemDefaults.TextStyles.SubtitleText
            }
            CheckBox {
                topMargin: 20
                text: qsTr("Verify HTTPS certificates (slower)")
                checked: browser.setting("verifyCerts")
                onCheckedChanged: browser.setSetting("verifyCerts", checked)
            }

            CheckBox {
                topMargin: 20
                text: qsTr("Show scrollbars")
                checked: browser.setting("scrollbars")
                onCheckedChanged: browser.setSetting("scrollbars", checked)
            }

            Header {
                topMargin: 30
                title: qsTr("Engine log")
            }
            Label {
                text: browser.logPath()
                multiline: true
                textStyle.base: SystemDefaults.TextStyles.SubtitleText
            }
            CheckBox {
                topMargin: 20
                text: qsTr("Detailed engine log (slower)")
                checked: browser.setting("detailedLog")
                onCheckedChanged: browser.setSetting("detailedLog", checked)
            }
            Container {
                topMargin: 20
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }
                Button {
                    text: logView.visible ? qsTr("Refresh log") : qsTr("Show log")
                    onClicked: {
                        logView.text = browser.logTail()
                        logView.visible = true
                    }
                }
                Button {
                    text: qsTr("Copy log")
                    onClicked: browser.copyText(browser.logTail())
                }
            }
            TextArea {
                id: logView
                visible: false
                editable: false
                preferredHeight: ui.du(60)
                textStyle.base: SystemDefaults.TextStyles.SmallText
                textStyle.fontFamily: "Monospace"
            }

            Header {
                topMargin: 30
                title: qsTr("Engine window")
            }
            Label {
                text: qsTr("If the page shows turned or taps land in the wrong place on your phone, these correct it.")
                multiline: true
                textStyle.base: SystemDefaults.TextStyles.SubtitleText
            }
            DropDown {
                title: qsTr("Rotation")
                Option { text: "0°"; value: 0 }
                Option { text: "90°"; value: 90 }
                Option { text: "180°"; value: 180 }
                Option { text: "270°"; value: 270 }
                id: rotationDropDown
                onCreationCompleted: selectByValue(rotationDropDown, browser.setting("rotation"))
                onSelectedValueChanged: browser.setSetting("rotation", selectedValue)
            }
            DropDown {
                title: qsTr("Tap mapping")
                Option { text: qsTr("Automatic"); value: 0 }
                Option { text: qsTr("Window"); value: 1 }
                Option { text: qsTr("Display"); value: 2 }
                id: touchDropDown
                onCreationCompleted: selectByValue(touchDropDown, browser.setting("touchMode"))
                onSelectedValueChanged: browser.setSetting("touchMode", selectedValue)
            }
        }
    }

    attachedObjects: [
        SystemDialog {
            id: clearHistoryDialog
            title: qsTr("Clear history")
            body: qsTr("Forget all %1 visited pages?").arg(history.count)
            onFinished: {
                if (result == SystemUiResult.ConfirmButtonSelection)
                    history.clear()
            }
        }
    ]

    function selectByValue(dropDown, value) {
        for (var i = 0; i < dropDown.count(); ++i) {
            if (dropDown.at(i).value == value) {
                dropDown.selectedIndex = i
                return
            }
        }
    }
}
