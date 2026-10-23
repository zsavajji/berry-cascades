import bb.cascades 1.2
import bb.system 1.2

// One page: a bar on top (the address bar, or the selection or find bar), the
// engine's window below (a ForeignWindowControl the engine's child window
// binds to), and the action bar. The engine window is a separate Screen
// window drawn above this one, so it is hidden whenever Cascades has
// something to show in its place: the menu, a sheet, a dialog, suggestions.
Page {
    id: page
    // The engine window keeps its size, so the keyboard covers the page
    // rather than squeezing it.
    resizeBehavior: PageResizeBehavior.None

    property bool finding: false
    property bool dialogOpen: false
    property bool typing: false
    property bool showSuggestions: address.focused && typing && address.text.length > 0
    property bool covered: actionMenuVisualState != ActionMenuVisualState.Hidden
                           || bookmarksSheet.opened || historySheet.opened || tabsSheet.opened
                           || downloadsSheet.opened || settingsSheet.opened
                           || dialogOpen || showSuggestions
    // What the long-press menu's rows do, in order.
    property variant menuActions: []
    property string menuLink
    property string menuText
    property string menuImage
    property bool sharePending: false

    keyListeners: [
        // Keys typed while no Cascades field is focused go to the page.
        KeyListener {
            onKeyPressed: {
                if (!address.focused && !findField.focused && browser.engineState == "running")
                    browser.sendKey(event.key)
            }
        }
    ]

    content: Container {
        background: Color.create("#1b1a26")
        layout: StackLayout {}

        // ---- address bar ---------------------------------------------------
        Container {
            visible: !browser.selecting && !page.finding
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
                hintText: qsTr("Search %1 or type an address").arg(browser.searchName)
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
                    page.typing = false
                    if (focused)
                        address.editor.setSelection(0, address.text.length)
                    else
                        address.text = browser.url
                }
                onTextChanging: {
                    if (!focused)
                        return
                    page.typing = true
                    page.fillSuggestions(text)
                }
            }

            Button {
                visible: browser.editing && !address.focused
                imageSource: "asset:///images/paste.png"
                preferredWidth: 110
                leftMargin: 10
                verticalAlignment: VerticalAlignment.Center
                onClicked: browser.paste()
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

        // ---- selection bar: text selected in the page -------------------------
        Container {
            visible: browser.selecting
            leftPadding: 12
            rightPadding: 12
            topPadding: 10
            bottomPadding: 10
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
            Button {
                imageSource: "asset:///images/copy.png"
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                onClicked: browser.copySelection()
            }
            Button {
                imageSource: "asset:///images/search.png"
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                onClicked: browser.searchSelection()
            }
            Button {
                imageSource: "asset:///images/share.png"
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                onClicked: {
                    page.share(browser.selectionText)
                    browser.clearSelection()
                }
            }
            Button {
                imageSource: "asset:///images/selectall.png"
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                onClicked: browser.selectAll()
            }
            Button {
                imageSource: "asset:///images/stop.png"
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                onClicked: browser.clearSelection()
            }
        }

        // ---- find bar ------------------------------------------------------
        Container {
            visible: page.finding && !browser.selecting
            leftPadding: 12
            rightPadding: 12
            topPadding: 10
            bottomPadding: 10
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
            TextField {
                id: findField
                hintText: qsTr("Find in page")
                verticalAlignment: VerticalAlignment.Center
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                input {
                    submitKey: SubmitKey.Search
                    onSubmitted: browser.find(findField.text, false)
                }
                onTextChanging: {
                    browser.stopFind()
                    if (text.length > 0)
                        browser.find(text, false)
                    else
                        findCount.text = ""
                }
            }
            Label {
                id: findCount
                leftMargin: 10
                verticalAlignment: VerticalAlignment.Center
                textStyle.color: Color.create("#8d89a6")
            }
            Button {
                imageSource: "asset:///images/up.png"
                preferredWidth: 100
                verticalAlignment: VerticalAlignment.Center
                onClicked: browser.find(findField.text, true)
            }
            Button {
                imageSource: "asset:///images/down.png"
                preferredWidth: 100
                verticalAlignment: VerticalAlignment.Center
                onClicked: browser.find(findField.text, false)
            }
            Button {
                imageSource: "asset:///images/stop.png"
                preferredWidth: 100
                verticalAlignment: VerticalAlignment.Center
                onClicked: page.endFind()
            }
        }

        // ---- the page --------------------------------------------------------
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

            // Address bar suggestions: a search, then bookmarks and history.
            Container {
                visible: page.showSuggestions
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                background: Color.create("#1b1a26")
                ListView {
                    id: suggestionList
                    dataModel: ArrayDataModel {
                        id: suggestionModel
                    }
                    listItemComponents: [
                        ListItemComponent {
                            StandardListItem {
                                title: ListItemData.title
                                description: ListItemData.url
                                imageSource: ListItemData.kind == "search" ? "asset:///images/search.png"
                                    : ListItemData.kind == "bookmark" ? "asset:///images/bookmarks.png"
                                    : "asset:///images/history.png"
                            }
                        }
                    ]
                    onTriggered: {
                        var row = dataModel.data(indexPath)
                        browser.open(row.url)
                        address.loseFocus()
                    }
                }
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
            title: qsTr("New tab")
            imageSource: "asset:///images/newtab.png"
            onTriggered: {
                browser.newTab("")
                address.requestFocus()
            }
        },
        ActionItem {
            title: qsTr("Tabs (%1)").arg(browser.tabs.length)
            imageSource: "asset:///images/tabs.png"
            onTriggered: tabsSheet.open()
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
        ActionItem {
            title: qsTr("History")
            imageSource: "asset:///images/history.png"
            onTriggered: historySheet.open()
        },
        ActionItem {
            title: qsTr("Find in page")
            imageSource: "asset:///images/search.png"
            onTriggered: {
                page.finding = true
                findField.requestFocus()
            }
        },
        ActionItem {
            title: qsTr("Select text")
            imageSource: "asset:///images/selectall.png"
            onTriggered: browser.selectAll()
        },
        ActionItem {
            title: qsTr("Paste")
            imageSource: "asset:///images/paste.png"
            enabled: browser.editing
            onTriggered: browser.paste()
        },
        ActionItem {
            title: qsTr("Share link")
            imageSource: "asset:///images/share.png"
            onTriggered: page.share(browser.url)
        },
        ActionItem {
            title: qsTr("Copy link")
            imageSource: "asset:///images/copy.png"
            onTriggered: browser.copyUrl()
        },
        ActionItem {
            title: qsTr("Downloads")
            imageSource: "asset:///images/downloads.png"
            onTriggered: downloadsSheet.open()
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
            id: historySheet
            content: HistoryPage {
                onDone: historySheet.close()
            }
        },
        Sheet {
            id: tabsSheet
            content: TabsPage {
                onDone: tabsSheet.close()
            }
        },
        Sheet {
            id: downloadsSheet
            content: DownloadsPage {
                onDone: downloadsSheet.close()
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
        },
        // The long-press menu on a link or an image.
        SystemListDialog {
            id: menuDialog
            selectionMode: ListSelectionMode.Single
            dismissOnSelection: true
            onFinished: {
                page.dialogOpen = false
                var picked = menuDialog.selectedIndices
                if (picked.length > 0)
                    page.runMenuAction(page.menuActions[picked[0]])
            }
        },
        // Sharing text from a button: set the data, and trigger once armed.
        Invocation {
            id: shareInvocation
            query: InvokeQuery {
                id: shareQuery
                mimeType: "text/plain"
                invokeActionId: "bb.action.SHARE"
            }
            onArmed: {
                if (page.sharePending) {
                    page.sharePending = false
                    trigger("bb.action.SHARE")
                }
            }
        }
    ]

    function share(text) {
        if (!text)
            return
        page.sharePending = true
        shareQuery.data = text
        shareQuery.updateQuery()
    }

    function fillSuggestions(text) {
        suggestionModel.clear()
        if (text.length == 0)
            return
        var search = {
            kind: "search",
            title: qsTr("Search %1 for “%2”").arg(browser.searchName).arg(text),
            url: browser.searchUrl(text)
        }
        var rows = browser.setting("suggestions") ? browser.suggest(text) : []
        if (browser.looksLikeAddress(text))
            rows.push(search)
        else
            rows.unshift(search)
        suggestionModel.append(rows)
    }

    function endFind() {
        page.finding = false
        findField.text = ""
        findCount.text = ""
        browser.stopFind()
    }

    function showMenu(link, linkText, image) {
        var titles = []
        var actions = []
        if (link) {
            titles.push(qsTr("Open"), qsTr("Open in new tab"), qsTr("Copy link"), qsTr("Share link"))
            actions.push("open", "newTab", "copyLink", "shareLink")
            if (linkText) {
                titles.push(qsTr("Copy link text"))
                actions.push("copyText")
            }
        }
        if (image) {
            titles.push(qsTr("Open image"), qsTr("Save image"), qsTr("Copy image address"))
            actions.push("openImage", "saveImage", "copyImage")
            if (!link) {
                titles.push(qsTr("Share image address"))
                actions.push("shareImage")
            }
        }
        page.menuLink = link
        page.menuText = linkText
        page.menuImage = image
        page.menuActions = actions
        menuDialog.title = link ? link : image
        menuDialog.clearList()
        for (var i = 0; i < titles.length; ++i)
            menuDialog.appendItem(titles[i])
        page.dialogOpen = true
        menuDialog.show()
    }

    function runMenuAction(action) {
        if (action == "open") browser.open(page.menuLink)
        else if (action == "newTab") browser.newTab(page.menuLink)
        else if (action == "copyLink") browser.copyText(page.menuLink)
        else if (action == "shareLink") page.share(page.menuLink)
        else if (action == "copyText") browser.copyText(page.menuText)
        else if (action == "openImage") browser.open(page.menuImage)
        else if (action == "saveImage") downloads.save(page.menuImage)
        else if (action == "copyImage") browser.copyText(page.menuImage)
        else if (action == "shareImage") page.share(page.menuImage)
    }

    onCreationCompleted: {
        var notify = function (text) {
            toast.body = text
            toast.show()
        }
        browser.message.connect(notify)
        downloads.message.connect(notify)
        browser.pageTouched.connect(function () {
            address.loseFocus()
            findField.loseFocus()
        })
        browser.pageChanged.connect(function () {
            if (!address.focused)
                address.text = browser.url
        })
        browser.contextMenu.connect(page.showMenu)
        browser.findResult.connect(function (found, count) {
            findCount.text = found || count > 0 ? qsTr("%1 found").arg(count) : qsTr("Not found")
        })
    }
}
