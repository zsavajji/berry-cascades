import bb.cascades 1.2
import bb.system 1.2

// One page: the address bar above, the engine's window below (a
// ForeignWindowControl the engine's child window binds to), and the action
// bar. The engine window is a separate Screen window drawn above this one,
// so it is hidden while the overflow menu or a sheet is open.
Page {
    id: page
    // The engine window keeps its size, so the keyboard covers the page
    // rather than squeezing it.
    resizeBehavior: PageResizeBehavior.None

    property bool covered: actionMenuVisualState != ActionMenuVisualState.Hidden
                           || bookmarksSheet.opened || settingsSheet.opened

    keyListeners: [
        // Keys typed while the address bar is not focused go to the page.
        KeyListener {
            onKeyPressed: {
                if (!address.focused && browser.engineState == "running")
                    browser.sendKey(event.key)
            }
        }
    ]

    content: Container {
        id: root
        background: Color.create("#1b1a26")
        layout: StackLayout {}

        Container {
            leftPadding: 12
            rightPadding: 12
            topPadding: 10
            bottomPadding: 10
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }

            TextField {
                id: address
                text: browser.url
                hintText: qsTr("Search or type an address")
                inputMode: TextFieldInputMode.Url
                clearButtonVisible: true
                verticalAlignment: VerticalAlignment.Center
                layoutProperties: StackLayoutProperties {
                    spaceQuota: 1
                }
                input {
                    submitKey: SubmitKey.Go
                    onSubmitted: {
                        browser.open(address.text)
                        address.loseFocus()
                    }
                }
                onFocusedChanged: {
                    if (!focused)
                        address.text = browser.url
                }
            }

            ActivityIndicator {
                running: browser.loading
                visible: browser.loading
                preferredWidth: 56
                preferredHeight: 56
                leftMargin: 10
                verticalAlignment: VerticalAlignment.Center
            }
        }

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            layout: DockLayout {}
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }

            // Seen while the engine starts or after it stops; the engine
            // window covers it otherwise.
            Container {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                Label {
                    horizontalAlignment: HorizontalAlignment.Center
                    textStyle.color: Color.create("#8d89a6")
                    text: browser.engineState == "stopped" ? qsTr("The browser engine stopped.")
                        : browser.engineState == "failed" ? qsTr("The browser engine could not start.")
                        : qsTr("Starting the browser engine…")
                }
                Button {
                    horizontalAlignment: HorizontalAlignment.Center
                    visible: browser.engineState == "stopped" || browser.engineState == "failed"
                    text: qsTr("Restart engine")
                    onClicked: browser.restartEngine()
                }
            }

            ForeignWindowControl {
                id: webView
                windowId: browser.windowId
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                // The engine sizes its own window (touch mapping depends on
                // it); Cascades only places and shows it.
                updatedProperties: WindowProperty.Position | WindowProperty.Visible
                keyInputForwardingEnabled: false
                visible: !page.covered

                attachedObjects: [
                    LayoutUpdateHandler {
                        onLayoutFrameChanged: {
                            browser.startEngine(layoutFrame.width, layoutFrame.height)
                        }
                    }
                ]
            }
        }
    }

    actions: [
        ActionItem {
            title: qsTr("Back")
            imageSource: "asset:///images/back.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            enabled: browser.canGoBack
            onTriggered: browser.back()
        },
        ActionItem {
            title: browser.loading ? qsTr("Stop") : qsTr("Reload")
            imageSource: browser.loading ? "asset:///images/stop.png" : "asset:///images/reload.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: browser.loading ? browser.stop() : browser.reload()
        },
        ActionItem {
            title: qsTr("Forward")
            imageSource: "asset:///images/forward.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            enabled: browser.canGoForward
            onTriggered: browser.forward()
        },
        ActionItem {
            title: qsTr("Home")
            imageSource: "asset:///images/home.png"
            onTriggered: browser.home()
        },
        ActionItem {
            title: qsTr("Bookmarks")
            imageSource: "asset:///images/bookmarks.png"
            onTriggered: bookmarksSheet.open()
        },
        ActionItem {
            title: qsTr("Add bookmark")
            imageSource: "asset:///images/bookmark_add.png"
            enabled: !browser.bookmarked
            onTriggered: browser.addBookmark()
        },
        InvokeActionItem {
            title: qsTr("Share link")
            imageSource: "asset:///images/share.png"
            query {
                mimeType: "text/plain"
                invokeActionId: "bb.action.SHARE"
            }
            onTriggered: data = browser.url
        },
        ActionItem {
            title: qsTr("Copy link")
            imageSource: "asset:///images/copy.png"
            onTriggered: browser.copyUrl()
        },
        ActionItem {
            title: qsTr("Settings")
            imageSource: "asset:///images/settings.png"
            onTriggered: settingsSheet.open()
        },
        ActionItem {
            title: qsTr("Restart engine")
            imageSource: "asset:///images/restart.png"
            onTriggered: browser.restartEngine()
        }
    ]

    attachedObjects: [
        Sheet {
            id: bookmarksSheet
            content: BookmarksPage {
                onDone: bookmarksSheet.close()
            }
        },
        Sheet {
            id: settingsSheet
            content: SettingsPage {
                onDone: settingsSheet.close()
            }
        },
        SystemToast {
            id: toast
        }
    ]

    onCreationCompleted: {
        browser.message.connect(function (text) {
            toast.body = text
            toast.show()
        })
        browser.pageTouched.connect(function () {
            address.loseFocus()
        })
        browser.pageChanged.connect(function () {
            if (!address.focused)
                address.text = browser.url
        })
    }
}
