import bb.cascades 1.2

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
                Option { text: "Google"; value: "google" }
                Option { text: "Bing"; value: "bing" }
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
                text: qsTr("Request desktop sites")
                checked: browser.setting("desktop")
                onCheckedChanged: browser.setSetting("desktop", checked)
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
            CheckBox {
                topMargin: 20
                text: qsTr("Verify HTTPS certificates (slower)")
                checked: browser.setting("verifyCerts")
                onCheckedChanged: browser.setSetting("verifyCerts", checked)
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

    function selectByValue(dropDown, value) {
        for (var i = 0; i < dropDown.count(); ++i) {
            if (dropDown.at(i).value == value) {
                dropDown.selectedIndex = i
                return
            }
        }
    }
}
