import bb.cascades 1.3
import bb.system 1.2

// Laid out like the stock BlackBerry 10 browser: the page fills the screen
// and one dark bar along the bottom carries Tabs, Back, the address and the
// ⋮ menu, with a thin blue line above it while a page loads. Selecting text
// or finding in the page swaps the bar for its own.
//
// The page is the engine's own Screen window, bound to the
// ForeignWindowControl and drawn above this one, so it is hidden whenever
// Cascades has something to show there (the menu, a sheet, a dialog, the
// address suggestions). Under the menu and dialogs a snapshot of the page
// stands in for it.
Page {
    id: page
    actionBarVisibility: ChromeVisibility.Hidden
    // The engine window keeps its size, so the keyboard covers the page
    // rather than squeezing it.
    resizeBehavior: PageResizeBehavior.None

    property real rootWidth: 720
    property bool menuOpen: false
    property bool dialogOpen: false
    property string pending          // "menu" or "dialog" while a snapshot is taken
    property string snapshotUrl
    property bool finding: false
    property bool typing: false
    property bool showSuggestions: address.focused && typing && address.text.length > 0
    property bool sheetOpen: bookmarksSheet.opened || historySheet.opened || tabsSheet.opened
                             || downloadsSheet.opened || settingsSheet.opened
    property bool covered: menuOpen || dialogOpen || sheetOpen || showSuggestions
    property bool startPage: browser.url.indexOf("file://") == 0
    // What the long-press menu's rows do, in order.
    property variant menuActions: []
    property string menuLink
    property string menuText
    property string menuImage
    property bool sharePending: false
    property bool backCancels: false   // the address was being edited when Back went down

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
        id: root
        background: Color.create("#262626")
        layout: DockLayout {}
        attachedObjects: [
            LayoutUpdateHandler {
                onLayoutFrameChanged: page.rootWidth = layoutFrame.width
            }
        ]

        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            layout: StackLayout {}

            // ---- the page ------------------------------------------------
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                background: Color.create("#f2f2f2")
                layout: DockLayout {}
                layoutProperties: StackLayoutProperties {
                    spaceQuota: 1
                }

                // Seen while the engine starts or after it stops.
                Container {
                    visible: browser.engineState != "running"
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                    Label {
                        horizontalAlignment: HorizontalAlignment.Center
                        textStyle.color: Color.create("#7a7a7a")
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

                ImageView {
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    scalingMethod: ScalingMethod.Fill
                    visible: (page.menuOpen || page.dialogOpen || page.sheetOpen) && page.snapshotUrl != ""
                    imageSource: page.snapshotUrl
                }

                ForeignWindowControl {
                    id: webView
                    windowId: browser.windowId
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    // The engine sizes its own window (touch mapping depends
                    // on it); Cascades only places and shows it.
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

                // Address suggestions: a search, then bookmarks and history.
                Container {
                    visible: page.showSuggestions
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    background: Color.create("#121212")
                    ListView {
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
                            browser.open(dataModel.data(indexPath).url)
                            address.loseFocus()
                        }
                    }
                }
            }

            // ---- load progress: the thin blue line -------------------------
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                preferredHeight: ui.du(0.5)
                background: Color.create("#1a1a1a")
                layout: DockLayout {}
                Container {
                    visible: browser.loading
                    preferredWidth: page.rootWidth * browser.progress
                    verticalAlignment: VerticalAlignment.Fill
                    background: Color.create("#00a8df")
                }
            }

            // ---- the bar ---------------------------------------------------
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                preferredHeight: ui.du(8.5)
                background: Color.create("#262626")
                layout: DockLayout {}

                // Browsing: Tabs | Back [address] | ⋮
                Container {
                    visible: !browser.selecting && !page.finding
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }

                    Container {
                        verticalAlignment: VerticalAlignment.Center
                        layout: DockLayout {}
                        leftPadding: ui.du(1.2)
                        rightPadding: ui.du(1.2)
                        visible: !address.focused
                        ImageButton {
                            defaultImageSource: "asset:///images/tabs.png"
                            pressedImageSource: "asset:///images/tabs.png"
                            preferredWidth: ui.du(5.5)
                            preferredHeight: ui.du(5.5)
                            onClicked: tabsSheet.open()
                        }
                        Label {
                            visible: browser.tabs.length > 1
                            horizontalAlignment: HorizontalAlignment.Center
                            verticalAlignment: VerticalAlignment.Center
                            translationX: -ui.du(0.4)
                            translationY: ui.du(0.4)
                            text: browser.tabs.length
                            textStyle.fontSize: FontSize.XXSmall
                            textStyle.color: Color.White
                            touchPropagationMode: TouchPropagationMode.None
                        }
                    }
                    Separator {
                        visible: !address.focused
                    }

                    // Back; while the address is being edited, it leaves editing.
                    ImageButton {
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(1.2)
                        rightMargin: ui.du(0.5)
                        defaultImageSource: "asset:///images/back.png"
                        pressedImageSource: "asset:///images/back.png"
                        disabledImageSource: "asset:///images/back.png"
                        enabled: address.focused || browser.canGoBack
                        opacity: enabled ? 1.0 : 0.35
                        preferredWidth: ui.du(5.5)
                        preferredHeight: ui.du(5.5)
                        onTouch: {
                            if (event.isDown())
                                page.backCancels = address.focused
                        }
                        onClicked: {
                            if (page.backCancels || address.focused)
                                address.loseFocus()
                            else
                                browser.back()
                            page.backCancels = false
                        }
                    }

                    // The address, in a darker pill: a padlock on https, the
                    // address without its scheme, and Stop or Reload.
                    Container {
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(1)
                        rightMargin: ui.du(1)
                        leftPadding: ui.du(1)
                        rightPadding: ui.du(0.5)
                        background: Color.create("#121212")
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }
                        ImageView {
                            visible: !address.focused && browser.url.indexOf("https://") == 0
                            imageSource: "asset:///images/lock.png"
                            preferredWidth: ui.du(3.8)
                            preferredHeight: ui.du(3.8)
                            verticalAlignment: VerticalAlignment.Center
                        }
                        TextField {
                            id: address
                            text: page.displayUrl(browser.url)
                            hintText: qsTr("Search or enter an address")
                            inputMode: TextFieldInputMode.Url
                            backgroundVisible: false
                            clearButtonVisible: true
                            textStyle.color: Color.White
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
                                if (focused) {
                                    address.text = page.startPage ? "" : browser.url
                                    address.editor.setSelection(0, address.text.length)
                                } else {
                                    address.text = page.displayUrl(browser.url)
                                }
                            }
                            onTextChanging: {
                                if (!focused)
                                    return
                                page.typing = true
                                page.fillSuggestions(text)
                            }
                        }
                        ImageButton {
                            visible: !address.focused
                            verticalAlignment: VerticalAlignment.Center
                            defaultImageSource: browser.loading ? "asset:///images/stop.png" : "asset:///images/reload.png"
                            pressedImageSource: defaultImageSource
                            preferredWidth: ui.du(4.5)
                            preferredHeight: ui.du(4.5)
                            onClicked: browser.loading ? browser.stop() : browser.reload()
                        }
                    }

                    ImageButton {
                        visible: browser.editing && !address.focused
                        verticalAlignment: VerticalAlignment.Center
                        rightMargin: ui.du(1)
                        defaultImageSource: "asset:///images/paste.png"
                        pressedImageSource: "asset:///images/paste.png"
                        preferredWidth: ui.du(5)
                        preferredHeight: ui.du(5)
                        onClicked: browser.paste()
                    }

                    Separator {
                        visible: !address.focused
                    }
                    ImageButton {
                        visible: !address.focused
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(1.2)
                        rightMargin: ui.du(1.2)
                        defaultImageSource: "asset:///images/overflow.png"
                        pressedImageSource: "asset:///images/overflow.png"
                        preferredWidth: ui.du(5.5)
                        preferredHeight: ui.du(5.5)
                        onClicked: page.openMenu()
                    }
                }

                // Text selected in the page.
                Container {
                    visible: browser.selecting
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment: VerticalAlignment.Center
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    BarButton {
                        imageSource: "asset:///images/copy.png"
                        onClicked: browser.copySelection()
                    }
                    BarButton {
                        imageSource: "asset:///images/search.png"
                        onClicked: browser.searchSelection()
                    }
                    BarButton {
                        imageSource: "asset:///images/share.png"
                        onClicked: {
                            page.share(browser.selectionText)
                            browser.clearSelection()
                        }
                    }
                    BarButton {
                        imageSource: "asset:///images/selectall.png"
                        onClicked: browser.selectAll()
                    }
                    BarButton {
                        imageSource: "asset:///images/stop.png"
                        onClicked: browser.clearSelection()
                    }
                }

                // Find in page.
                Container {
                    visible: page.finding && !browser.selecting
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center
                    leftPadding: ui.du(1)
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    Container {
                        verticalAlignment: VerticalAlignment.Center
                        background: Color.create("#121212")
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }
                        TextField {
                            id: findField
                            hintText: qsTr("Find in page")
                            backgroundVisible: false
                            textStyle.color: Color.White
                            verticalAlignment: VerticalAlignment.Center
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: 1
                            }
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
                            rightMargin: ui.du(1)
                            verticalAlignment: VerticalAlignment.Center
                            textStyle.color: Color.create("#8a8a8a")
                        }
                    }
                    BarButton {
                        imageSource: "asset:///images/up.png"
                        onClicked: browser.find(findField.text, true)
                    }
                    BarButton {
                        imageSource: "asset:///images/down.png"
                        onClicked: browser.find(findField.text, false)
                    }
                    BarButton {
                        imageSource: "asset:///images/stop.png"
                        onClicked: page.endFind()
                    }
                }
            }
        }

        // ---- the ⋮ menu, sliding in from the right ------------------------
        Container {
            visible: page.menuOpen
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            background: Color.create("#99000000")
            gestureHandlers: [
                TapHandler {
                    onTapped: page.closeMenu()
                }
            ]
        }
        Container {
            id: menu
            visible: page.menuOpen
            horizontalAlignment: HorizontalAlignment.Right
            verticalAlignment: VerticalAlignment.Fill
            preferredWidth: Math.min(page.rootWidth * 0.66, ui.du(60))
            background: Color.create("#121212")
            animations: [
                TranslateTransition {
                    id: menuSlide
                    fromX: page.rootWidth * 0.66
                    toX: 0
                    duration: 160
                    easingCurve: StockCurve.CubicOut
                }
            ]
            ScrollView {
                Container {
                    topPadding: ui.du(1)
                    MenuRow {
                        title: qsTr("Forward")
                        imageSource: "asset:///images/forward.png"
                        enabled: browser.canGoForward
                        onTriggered: page.menuDo(function () { browser.forward() })
                    }
                    MenuRow {
                        title: qsTr("New tab")
                        imageSource: "asset:///images/newtab.png"
                        onTriggered: page.menuDo(function () {
                            browser.newTab("")
                            address.requestFocus()
                        })
                    }
                    MenuRow {
                        title: qsTr("Home")
                        imageSource: "asset:///images/home.png"
                        onTriggered: page.menuDo(function () { browser.home() })
                    }
                    MenuRow {
                        title: qsTr("Add to bookmarks")
                        imageSource: "asset:///images/bookmark_add.png"
                        enabled: !browser.bookmarked && !page.startPage
                        onTriggered: page.menuDo(function () { browser.addBookmark() })
                    }
                    MenuRow {
                        title: qsTr("Bookmarks")
                        imageSource: "asset:///images/bookmarks.png"
                        onTriggered: page.menuDo(function () { bookmarksSheet.open() })
                    }
                    MenuRow {
                        title: qsTr("History")
                        imageSource: "asset:///images/history.png"
                        onTriggered: page.menuDo(function () { historySheet.open() })
                    }
                    MenuRow {
                        title: qsTr("Find in page")
                        imageSource: "asset:///images/search.png"
                        onTriggered: page.menuDo(function () {
                            page.finding = true
                            findField.requestFocus()
                        })
                    }
                    MenuRow {
                        title: qsTr("Select text")
                        imageSource: "asset:///images/selectall.png"
                        onTriggered: page.menuDo(function () { browser.selectAll() })
                    }
                    MenuRow {
                        title: qsTr("Paste")
                        imageSource: "asset:///images/paste.png"
                        visible: browser.editing
                        onTriggered: page.menuDo(function () { browser.paste() })
                    }
                    MenuRow {
                        title: qsTr("Share")
                        imageSource: "asset:///images/share.png"
                        onTriggered: page.menuDo(function () { page.share(browser.url) })
                    }
                    MenuRow {
                        title: qsTr("Copy link")
                        imageSource: "asset:///images/copy.png"
                        onTriggered: page.menuDo(function () { browser.copyUrl() })
                    }
                    MenuRow {
                        title: qsTr("Downloads")
                        imageSource: "asset:///images/downloads.png"
                        onTriggered: page.menuDo(function () { downloadsSheet.open() })
                    }
                    MenuRow {
                        title: qsTr("Settings")
                        imageSource: "asset:///images/settings.png"
                        onTriggered: page.menuDo(function () { settingsSheet.open() })
                    }
                    MenuRow {
                        title: qsTr("Restart engine")
                        imageSource: "asset:///images/restart.png"
                        onTriggered: page.menuDo(function () { browser.restartEngine() })
                    }
                }
            }
        }
    }

    attachedObjects: [
        Sheet {
            id: bookmarksSheet
            onOpened: bookmarksPage.show()
            onClosed: bookmarksPage.active = false
            content: BookmarksPage {
                id: bookmarksPage
                onDone: bookmarksSheet.close()
            }
        },
        Sheet {
            id: historySheet
            onOpened: historyPage.show()
            onClosed: historyPage.active = false
            content: HistoryPage {
                id: historyPage
                onDone: historySheet.close()
            }
        },
        Sheet {
            id: tabsSheet
            onOpened: tabsPage.show()
            onClosed: tabsPage.active = false
            content: TabsPage {
                id: tabsPage
                onDone: tabsSheet.close()
            }
        },
        Sheet {
            id: downloadsSheet
            onOpened: downloadsPage.show()
            onClosed: downloadsPage.active = false
            content: DownloadsPage {
                id: downloadsPage
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

    // The address as the stock bar shows it: no scheme, no "www.".
    function displayUrl(url) {
        if (url.indexOf("file://") == 0)
            return ""
        return url.replace(/^[a-z]+:\/\//, "").replace(/^www\./, "")
    }

    // The menu and the long-press dialog open over a snapshot of the page,
    // taken first, since the engine window has to be hidden under them.
    function openMenu() {
        page.pending = "menu"
        browser.snapshot()
    }

    function closeMenu() {
        page.menuOpen = false
    }

    function menuDo(action) {
        page.menuOpen = false
        action()
    }

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
        page.pending = "dialog"
        browser.snapshot()
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
                address.text = page.displayUrl(browser.url)
        })
        browser.contextMenu.connect(page.showMenu)
        browser.findResult.connect(function (found, count) {
            findCount.text = found || count > 0 ? qsTr("%1 found").arg(count) : qsTr("Not found")
        })
        browser.snapshotReady.connect(function (url) {
            page.snapshotUrl = url
            if (page.pending == "menu") {
                page.menuOpen = true
                menuSlide.play()
            } else if (page.pending == "dialog") {
                page.dialogOpen = true
                menuDialog.show()
            }
            page.pending = ""
        })
    }
}
