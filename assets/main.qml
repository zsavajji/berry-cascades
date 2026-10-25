import bb.cascades 1.4
import bb.system 1.2
import chromeberry 1.0

// Laid out like the stock BlackBerry 10 browser: the page fills the screen
// and one dark bar along the bottom carries Back, the address and the
// ⋮ menu, with a thin blue line above it while a page loads. Selecting text
// or finding in the page swaps the bar for its own.
//
// The page is the engine's own Screen window, bound to the
// ForeignWindowControl and drawn above this one, so it is hidden whenever
// Cascades has something to show there (the menu, a sheet, a dialog, the
// address suggestions, the text magnifier while the address is edited). A
// snapshot of the page stands in for it under the menu, dialogs and the
// address. A system toast would be covered too, so messages show in the
// title strip. The page's window never takes the keyboard (shim): keys come
// here, and while a text box in the page has the focus they go through an
// unseen field of ours (pageInput), so the phone's keyboard works as in any
// field of its own.
Page {
    id: page
    actionBarVisibility: ChromeVisibility.Hidden
    // The engine window keeps its size, so the keyboard covers the page
    // rather than squeezing it.
    resizeBehavior: PageResizeBehavior.None

    property real rootWidth: 720
    property bool menuOpen: false
    property bool dialogOpen: false
    // The menu's keys are on only while it is on screen: its actions stay on
    // the page after it closes, and their keys would take the typing.
    property bool menuShown: actionMenuVisualState != ActionMenuVisualState.Hidden
    property string pending          // "menu" or "dialog" while a snapshot is taken
    property bool snapshotShown: false   // browser.snapshotImage is the page
    property bool finding: false
    property bool typing: false
    property string focusText   // the address put in on focus, until the first key
    property real focusTime     // when the address got the focus
    property real movedTime     // when the user first put the cursor somewhere after that
    property bool focusAsked: false   // the address's cover was tapped
    property bool editCovered: false   // the snapshot stands in for the page while editing
    property real submitTime    // when Go was pressed: its Enter is not for the page
    property string notice      // shown in the title strip for a moment
    // Not tied to the address having focus: a tap on a suggestion takes the
    // focus away first. Picking one, Go or Back ends the typing.
    property bool showSuggestions: typing && address.text.length > 0
    onTypingChanged: {
        if (!typing && !address.focused)
            address.text = page.displayUrl(browser.url)
    }
    property bool sheetOpen: bookmarksSheet.opened || historySheet.opened
                             || downloadsSheet.opened || settingsSheet.opened
    property bool covered: menuOpen || dialogOpen || sheetOpen || showSuggestions || editCovered
    property bool startPage: browser.url.indexOf("file://") == 0
    // What the long-press menu is about.
    property string menuLink
    property string menuText
    property string menuImage
    property bool sharePending: false
    property bool backCancels: false   // the address was being edited when Back went down

    // Stock bars: scrolling down leaves only the slim address bar; scrolling
    // up, a tap on it, or a swipe down from the top edge (which also shows
    // the title strip) brings the full bar back. The page's window reaches
    // from the top to the slim bar; the strips over it are clipped out of it.
    property int titleHeight: ui.du(5)
    property int slimHeight: ui.du(3.3)
    property int fullHeight: ui.du(10.5)  // the bar and the progress line above it (as stock)
    property bool barsHidden: false
    // Editing, finding, selecting and the menus need the full bar.
    property bool barsShown: !barsHidden || address.focused || finding || browser.selecting || covered
    property bool titleShown: false
    property bool titleIn: (titleShown && barsShown) || notice != ""
    // Browser slides them (browser.barsIn, browser.titleIn) and clips the
    // page's window in step.
    onBarsShownChanged: browser.showBars(webView, barsShown, titleIn, true)
    onTitleInChanged: browser.showBars(webView, barsShown, titleIn, true)
    // The page comes back when the menu has gone, however it went.
    onActionMenuVisualStateChanged: {
        if (actionMenuVisualState == ActionMenuVisualState.Hidden)
            page.closeMenu()
    }

    keyListeners: [
        // Keys typed while no field of ours is focused go to the page.
        KeyListener {
            onKeyPressed: {
                // The Enter that pressed Go arrives once the address has let go of the focus.
                if (event.key == 13 && Date.now() - page.submitTime < 1000)
                    return
                if (!address.focused && !findField.focused && !pageInput.focused
                        && browser.engineState == "running")
                    page.pageKey(event, false)
            }
        }
    ]

    content: Container {
        id: root
        background: Color.create("#262626")
        layout: DockLayout {}
        // The trackpad (Q20) and the touch keyboard's swipes (Passport) scroll the page.
        eventHandlers: [
            TrackpadHandler {
                onTrackpad: {
                    if (event.trackpadEventType == TrackpadEventType.Move
                            && browser.engineState == "running" && !page.covered)
                        browser.scrollPage(event.deltaX, event.deltaY)
                }
            }
        ]
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

                // The keys for a text box in the page: in a field of ours the
                // phone does all its keyboard does (tapped and held Alt and Shift
                // with their indicator, the symbol key, a held key's accents).
                // What it types goes to the page at once and the field is
                // emptied (as term51's). Unseen under everything else here, the
                // page's window included, but not transparent: the phone opens
                // no symbol picker for a field it takes for invisible.
                TextField {
                    id: pageInput
                    backgroundVisible: false
                    // Only syncPageInput gives it the focus, never a touch or a key.
                    focusPolicy: FocusPolicy.None
                    navigation.focusPolicy: NavigationFocusPolicy.NotFocusable
                    // Kept unless another field takes it: the bars' buttons leave it.
                    focusRetentionPolicyFlags: FocusRetentionPolicy.LoseToFocusable
                    verticalAlignment: VerticalAlignment.Bottom
                    horizontalAlignment: HorizontalAlignment.Fill
                    inputMode: TextFieldInputMode.Text
                    input {
                        flags: TextInputFlag.AutoCapitalizationOff | TextInputFlag.AutoCorrectionOff
                               | TextInputFlag.PredictionOff | TextInputFlag.SpellCheckOff
                               | TextInputFlag.WordSubstitutionOff | TextInputFlag.AutoPeriodOff
                        submitKey: SubmitKey.EnterKey
                        submitKeyFocusBehavior: SubmitKeyFocusBehavior.Keep
                        onSubmitted: browser.sendKey(13)
                    }
                    onTextChanging: {
                        if (text.length > 0) {
                            browser.typeText(text)
                            pageInput.text = ""
                        }
                    }
                    keyListeners: [
                        KeyListener {
                            onKeyPressed: page.pageKey(event, true)
                        }
                    ]
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
                    visible: (page.menuOpen || page.dialogOpen || page.sheetOpen || page.editCovered)
                             && page.snapshotShown
                    image: browser.snapshotImage
                    // Straight in place of the page, not faded in.
                    loadEffect: ImageViewLoadEffect.None
                    gestureHandlers: [
                        TapHandler {
                            onTapped: {
                                page.typing = false
                                address.loseFocus()
                                page.closeMenu()
                            }
                        }
                    ]
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
                    // Not once the engine has stopped: its last frame would
                    // cover the Restart button.
                    visible: !page.covered && browser.engineState != "stopped"
                             && browser.engineState != "failed"
                    onBoundToWindowChanged: {
                        if (boundToWindow)
                            browser.showBars(webView, page.barsShown, page.titleIn, false)
                    }

                    attachedObjects: [
                        LayoutUpdateHandler {
                            onLayoutFrameChanged: {
                                browser.startEngine(layoutFrame.width, layoutFrame.height)
                            }
                        }
                    ]
                }

                // Address suggestions: a search, then bookmarks and history,
                // above the bars, which lie over the bottom of the page area.
                Container {
                    visible: page.showSuggestions
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill
                    bottomPadding: page.fullHeight - page.slimHeight
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
                            page.typing = false
                            address.loseFocus()
                        }
                    }
                }
            }

            // ---- the slim bar: the address, while the page is scrolled -----
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                preferredHeight: page.slimHeight
                background: Color.create("#282828")
                layout: DockLayout {}
                gestureHandlers: [
                    TapHandler {
                        onTapped: page.barsHidden = false
                    }
                ]
                Container {
                    visible: browser.loading
                    preferredWidth: page.rootWidth * browser.progress
                    preferredHeight: ui.du(0.3)
                    background: Color.create("#00a8df")
                }
                Container {
                    verticalAlignment: VerticalAlignment.Center
                    leftPadding: ui.du(1.5)
                    rightPadding: ui.du(1)
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    ImageView {
                        visible: browser.url.indexOf("https://") == 0
                        verticalAlignment: VerticalAlignment.Center
                        imageSource: "asset:///images/lock.png"
                        // Fixed: the address would squeeze it to nothing.
                        minWidth: ui.du(3)
                        maxWidth: ui.du(3)
                        preferredHeight: ui.du(3)
                        rightMargin: ui.du(1.5)
                    }
                    Label {
                        verticalAlignment: VerticalAlignment.Center
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        text: page.displayUrl(browser.url)
                        textStyle.fontSize: FontSize.XSmall
                        textStyle.color: Color.White
                    }
                }
            }
        }

        // ---- the title strip: swipe down from the top edge -----------------
        // Over the top of the page, whose window is clipped there (as under
        // the bars below), with the site's icon and title.
        Container {
            translationY: -(1 - browser.titleIn) * page.titleHeight
            attachedObjects: [
                // browser.titleIn already moves in steps.
                ImplicitAnimationController {
                    propertyName: "translationY"
                    enabled: false
                }
            ]
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Top
            preferredHeight: page.titleHeight
            leftPadding: ui.du(1)
            rightPadding: ui.du(1)
            background: Color.create("#262626")
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
            ImageView {
                visible: browser.icon != "" && page.notice == ""
                verticalAlignment: VerticalAlignment.Center
                imageSource: browser.icon
                preferredWidth: ui.du(3.5)
                preferredHeight: ui.du(3.5)
                rightMargin: ui.du(1)
            }
            Label {
                verticalAlignment: VerticalAlignment.Center
                text: page.notice != "" ? page.notice
                    : browser.title != "" ? browser.title : page.displayUrl(browser.url)
                textStyle.fontSize: FontSize.XSmall
                textStyle.color: Color.White
            }
        }

        // ---- the bars, over the bottom of the page -------------------------
        // The page's window reaches down to the slim bar; while these show,
        // its bottom is clipped (Browser::showBars) so they show through.
        Container {
            translationY: (1 - browser.barsIn) * page.fullHeight
            attachedObjects: [
                ImplicitAnimationController {
                    propertyName: "translationY"
                    enabled: false
                }
            ]
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Bottom
            layout: StackLayout {}

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
                preferredHeight: page.fullHeight - ui.du(0.5)
                background: Color.create("#282828")
                layout: DockLayout {}

                // Browsing: Back [address] | ⋮
                Container {
                    visible: !browser.selecting && !page.finding
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Center
                    // Margins only space siblings, never the bar's edges: Back
                    // is always first, and while editing the address is last.
                    leftPadding: ui.du(1.2)
                    rightPadding: address.focused ? ui.du(1) : 0
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }

                    // Back; while the address is being edited, it leaves editing.
                    ImageButton {
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(1.2)
                        rightMargin: ui.du(0.5)
                        defaultImageSource: "asset:///images/back.png"
                        pressedImageSource: "asset:///images/back.png"
                        disabledImageSource: "asset:///images/back.png"
                        enabled: address.focused || page.typing || browser.canGoBack
                        opacity: enabled ? 1.0 : 0.35
                        preferredWidth: ui.du(6)
                        preferredHeight: ui.du(6)
                        onTouch: {
                            if (event.isDown())
                                page.backCancels = address.focused || page.typing
                        }
                        onClicked: {
                            if (page.backCancels || address.focused || page.typing) {
                                page.typing = false
                                address.loseFocus()
                            } else
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
                        minHeight: ui.du(8)
                        background: Color.create("#191919")
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }
                        ImageView {
                            visible: !address.focused && browser.url.indexOf("https://") == 0
                            imageSource: "asset:///images/lock.png"
                            minWidth: ui.du(3.8)
                            maxWidth: ui.du(3.8)
                            preferredHeight: ui.du(3.8)
                            verticalAlignment: VerticalAlignment.Center
                        }
                        // A tap on the field puts the cursor where the finger was,
                        // which drops the selection made on focus (and selecting
                        // again during the tap stops the keyboard typing). So the
                        // unfocused field is covered, and the cover focuses it.
                        Container {
                            layout: DockLayout {}
                            verticalAlignment: VerticalAlignment.Center
                            // The selection handle at the field's end stays in the pill.
                            rightPadding: 20
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: 1
                            }
                            TextField {
                                id: address
                                text: page.displayUrl(browser.url)
                                hintText: qsTr("Search or enter an address")
                                inputMode: TextFieldInputMode.Url
                                backgroundVisible: false
                                textStyle.fontSize: FontSize.PercentageValue
                                textStyle.fontSizeValue: 108
                                clearButtonVisible: true
                                textStyle.color: Color.White
                                verticalAlignment: VerticalAlignment.Center
                                horizontalAlignment: HorizontalAlignment.Fill
                                input {
                                    submitKey: SubmitKey.Go
                                    onSubmitted: {
                                        page.submitTime = Date.now()
                                        browser.open(address.text)
                                        page.typing = false
                                        address.loseFocus()
                                    }
                                }
                                onFocusedChanged: {
                                    // Only the user's tap: Cascades also focuses the address when the
                                    // app comes back, and the keys meant for the page went there.
                                    if (focused && !page.focusAsked) {
                                        address.loseFocus()
                                        return
                                    }
                                    page.focusAsked = false
                                    if (focused) {
                                        page.typing = false
                                        page.focusTime = Date.now()
                                        page.movedTime = 0
                                        // First: setting the text comes back through onTextChanging.
                                        page.focusText = page.startPage ? "" : browser.url
                                        address.text = page.focusText
                                        address.editor.setSelection(0, address.text.length)
                                        // The magnifier is drawn under the page's window.
                                        page.pending = "edit"
                                        browser.snapshot()
                                    } else {
                                        page.editCovered = false
                                        if (!page.typing)
                                            address.text = page.displayUrl(browser.url)
                                        page.syncPageInput()
                                    }
                                }
                                onTextChanging: {
                                    // Setting the address on focus, or the cursor moving,
                                    // reports it unchanged.
                                    if (!focused || text == page.focusText)
                                        return
                                    // The first key replaces the address selected on focus,
                                    // even if the selection was lost (the keyboard puts the
                                    // cursor at the end), unless the user put the cursor
                                    // somewhere first: then it edits. Setting the text comes
                                    // back through this handler.
                                    var old = page.focusText
                                    if (old != "") {
                                        page.focusText = ""
                                        var edited = page.movedTime > 0 && Date.now() - page.movedTime > 150
                                        var kept = edited ? text : page.firstKey(old, text)
                                        if (kept != text) {
                                            address.text = kept
                                            return
                                        }
                                    }
                                    page.typing = true
                                    page.fillSuggestions(text)
                                }
                            }
                            Container {
                                visible: !address.focused
                                horizontalAlignment: HorizontalAlignment.Fill
                                verticalAlignment: VerticalAlignment.Fill
                                background: Color.Transparent
                                gestureHandlers: [
                                    TapHandler {
                                        onTapped: {
                                            page.focusAsked = true
                                            address.requestFocus()
                                        }
                                    }
                                ]
                            }
                        }
                        ImageButton {
                            visible: !address.focused
                            verticalAlignment: VerticalAlignment.Center
                            defaultImageSource: browser.loading ? "asset:///images/stop.png" : "asset:///images/reload.png"
                            pressedImageSource: defaultImageSource
                            preferredWidth: ui.du(6.5)
                            preferredHeight: ui.du(6.5)
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
                        preferredWidth: ui.du(6)
                        preferredHeight: ui.du(6)
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
                            onFocusedChanged: {
                                if (!focused)
                                    page.syncPageInput()
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
    }

    attachedObjects: [
        // The ⋮ menu's actions (openMenu) and the long-press menu's (showMenu).
        // Their keys work while their menu is shown (menuShown), where the menu
        // shows them: on the page a key the page's text box should have got
        // (one the page script did not report focused) would go Home instead.
        ActionItem {
            id: actForward
            shortcuts: Shortcut { key: "N"; enabled: page.menuShown }
            title: qsTr("Forward")
            imageSource: "asset:///images/forward.png"
            enabled: browser.canGoForward
            onTriggered: browser.forward()
        },
        ActionItem {
            id: actHome
            shortcuts: Shortcut { key: "H"; enabled: page.menuShown }
            title: qsTr("Home")
            imageSource: "asset:///images/home.png"
            onTriggered: browser.home()
        },
        ActionItem {
            id: actAddBookmark
            shortcuts: Shortcut { key: "A"; enabled: page.menuShown }
            title: qsTr("Add to bookmarks")
            imageSource: "asset:///images/bookmark_add.png"
            enabled: !browser.bookmarked && !page.startPage
            onTriggered: browser.addBookmark()
        },
        ActionItem {
            id: actBookmarks
            shortcuts: Shortcut { key: "K"; enabled: page.menuShown }
            title: qsTr("Bookmarks")
            imageSource: "asset:///images/bookmarks.png"
            onTriggered: bookmarksSheet.open()
        },
        ActionItem {
            id: actHistory
            shortcuts: Shortcut { key: "Y"; enabled: page.menuShown }
            title: qsTr("History")
            imageSource: "asset:///images/history.png"
            onTriggered: historySheet.open()
        },
        ActionItem {
            id: actFind
            shortcuts: Shortcut { key: "F"; enabled: page.menuShown }
            title: qsTr("Find in page")
            imageSource: "asset:///images/search.png"
            onTriggered: {
                page.finding = true
                findField.requestFocus()
            }
        },
        ActionItem {
            id: actSelect
            shortcuts: Shortcut { key: "L"; enabled: page.menuShown }
            title: qsTr("Select text")
            imageSource: "asset:///images/selectall.png"
            onTriggered: browser.selectAll()
        },
        ActionItem {
            id: actPaste
            shortcuts: Shortcut { key: "V"; enabled: page.menuShown }
            title: qsTr("Paste")
            imageSource: "asset:///images/paste.png"
            onTriggered: browser.paste()
        },
        ActionItem {
            id: actShare
            shortcuts: Shortcut { key: "S"; enabled: page.menuShown }
            title: qsTr("Share")
            imageSource: "asset:///images/share.png"
            onTriggered: page.share(browser.url)
        },
        ActionItem {
            id: actCopyUrl
            shortcuts: Shortcut { key: "C"; enabled: page.menuShown }
            title: qsTr("Copy link")
            imageSource: "asset:///images/copy.png"
            onTriggered: browser.copyUrl()
        },
        ActionItem {
            id: actDownloads
            shortcuts: Shortcut { key: "D"; enabled: page.menuShown }
            title: qsTr("Downloads")
            imageSource: "asset:///images/downloads.png"
            onTriggered: downloadsSheet.open()
        },
        ActionItem {
            id: actSettings
            shortcuts: Shortcut { key: "E"; enabled: page.menuShown }
            title: qsTr("Settings")
            imageSource: "asset:///images/settings.png"
            onTriggered: settingsSheet.open()
        },
        ActionItem {
            id: actRestart
            shortcuts: Shortcut { key: "R"; enabled: page.menuShown }
            title: qsTr("Restart engine")
            imageSource: "asset:///images/restart.png"
            onTriggered: browser.restartEngine()
        },
        ActionItem {
            id: actOpenLink
            shortcuts: Shortcut { key: "O"; enabled: page.menuShown }
            title: qsTr("Open")
            imageSource: "asset:///images/forward.png"
            onTriggered: browser.open(page.menuLink)
        },
        ActionItem {
            id: actCopyLink
            shortcuts: Shortcut { key: "C"; enabled: page.menuShown }
            title: qsTr("Copy link")
            imageSource: "asset:///images/copy.png"
            onTriggered: browser.copyText(page.menuLink)
        },
        ActionItem {
            id: actShareLink
            shortcuts: Shortcut { key: "S"; enabled: page.menuShown }
            title: qsTr("Share link")
            imageSource: "asset:///images/share.png"
            onTriggered: page.share(page.menuLink)
        },
        ActionItem {
            id: actCopyLinkText
            shortcuts: Shortcut { key: "T"; enabled: page.menuShown }
            title: qsTr("Copy link text")
            imageSource: "asset:///images/copy.png"
            onTriggered: browser.copyText(page.menuText)
        },
        ActionItem {
            id: actOpenImage
            shortcuts: Shortcut { key: "I"; enabled: page.menuShown }
            title: qsTr("Open image")
            imageSource: "asset:///images/forward.png"
            onTriggered: browser.open(page.menuImage)
        },
        ActionItem {
            id: actSaveImage
            shortcuts: Shortcut { key: "D"; enabled: page.menuShown }
            title: qsTr("Save image")
            imageSource: "asset:///images/downloads.png"
            onTriggered: downloads.save(page.menuImage)
        },
        ActionItem {
            id: actCopyImage
            shortcuts: Shortcut { key: "A"; enabled: page.menuShown }
            title: qsTr("Copy image address")
            imageSource: "asset:///images/copy.png"
            onTriggered: browser.copyText(page.menuImage)
        },
        ActionItem {
            id: actShareImage
            shortcuts: Shortcut { key: "S"; enabled: page.menuShown }
            title: qsTr("Share image address")
            imageSource: "asset:///images/share.png"
            onTriggered: page.share(page.menuImage)
        },
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
        Timer {
            id: menuTimer
            singleShot: true
            interval: 150  // a guess at how long the menu takes in new actions
            onTimeout: {
                if (page.menuOpen || page.dialogOpen)
                    page.openActionMenu()
            }
        },
        Timer {
            id: noticeTimer
            singleShot: true
            interval: 2500
            onTimeout: page.notice = ""
        },
        // Messages while the page's window is hidden; otherwise it would cover them.
        SystemToast {
            id: toast
            // A message while one is still showing replaces it.
            autoUpdateEnabled: true
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

    // The ⋮ menu and the long-press menu are the system's action menu, each
    // with its own actions. It is drawn under the engine's window, so it opens
    // over a snapshot of the page, taken first.
    function openMenu() {
        page.pending = "menu"
        browser.snapshot()
    }

    // Changing the actions as the menu opens slides its rows in from the top,
    // so a new set opens a moment after it is put in.
    function openActions(actions) {
        var same = page.actionCount() == actions.length
        for (var i = 0; same && i < actions.length; ++i)
            same = page.actionAt(i) == actions[i]
        if (same) {
            page.openActionMenu()
            return
        }
        while (page.actionCount() > 0)
            page.removeAction(page.actionAt(0))
        for (var j = 0; j < actions.length; ++j)
            page.addAction(actions[j], ActionBarPlacement.InOverflow)
        menuTimer.start()
    }

    function closeMenu() {
        page.menuOpen = false
        page.dialogOpen = false
    }

    function share(text) {
        if (!text)
            return
        page.sharePending = true
        shareQuery.data = text
        shareQuery.updateQuery()
    }

    // What the first key after focus should leave of the address it changed
    // from old to text: only what it inserted, or nothing if it deleted.
    function firstKey(old, text) {
        var p = 0
        while (p < old.length && p < text.length && old.charAt(p) == text.charAt(p))
            p++
        var s = 0
        while (s < old.length - p && s < text.length - p
               && old.charAt(old.length - 1 - s) == text.charAt(text.length - 1 - s))
            s++
        if (p + s == old.length)
            return text.substring(p, text.length - s)
        if (p + s == text.length)
            return ""
        return text
    }

    // A key for the page. The Q10 may cut a key's code to its low byte
    // (Shift 0xF0E1 as á, Alt 0xF0E9 as é, Delete 0xF0FF as ÿ): such a key
    // has a code for its cap, or is a modifier's byte with no cap of its own.
    // In pageInput (inField) the field types the characters itself and Enter
    // is its submitKey; the other keys (Backspace, Delete, Tab, Escape) go on.
    function pageKey(event, inField) {
        var k = event.key
        var c = event.keycap
        if (c >= 0xE000 && k >= 0x80 && k < 0x100
                || (c == k || c == 0) && (k == 0xE1 || k == 0xE2 || k == 0xE3 || k == 0xE4 || k == 0xE9 || k == 0xEA))
            k += 0xF000
        if (inField && (k == 13 || k == 10 || k == 0xF00D || k >= 0x20 && k != 0x7F && k < 0xE000))
            return
        browser.sendKey(k)
    }

    // pageInput has the keyboard while a text box in the page has the focus
    // and no field of ours is in use.
    function syncPageInput() {
        if (browser.editing && !address.focused && !findField.focused
                && browser.engineState == "running")
            pageInput.requestFocus()
        else if (pageInput.focused)
            pageInput.loseFocus()
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
        page.menuLink = link
        page.menuText = linkText
        page.menuImage = image
        page.pending = "dialog"
        browser.snapshot()
    }

    onCreationCompleted: {
        var notify = function (text) {
            if (page.covered) {
                toast.body = text
                toast.show()
                return
            }
            page.notice = text
            noticeTimer.start()
        }
        browser.message.connect(notify)
        downloads.message.connect(notify)
        browser.editingChanged.connect(page.syncPageInput)
        browser.pageTouched.connect(function () {
            page.typing = false
            address.loseFocus()
            findField.loseFocus()
        })
        browser.pageChanged.connect(function () {
            if (!address.focused)
                address.text = page.displayUrl(browser.url)
        })
        browser.contextMenu.connect(page.showMenu)
        // Focusing may still move the cursor right away (never select the address
        // again from here: it leaves the keyboard unable to type at all). A cursor
        // put somewhere later is the user's, to edit.
        address.editor.cursorPositionChanged.connect(function () {
            if (page.focusText == "" || !address.focused)
                return
            var since = Date.now() - page.focusTime
            if (since > 500 && page.movedTime == 0)
                page.movedTime = Date.now()
        })
        browser.setBarHeights(page.titleHeight, page.slimHeight, page.fullHeight)
        // The stock bars.
        browser.pageScrolled.connect(function (down) {
            page.barsHidden = down
            if (down)
                page.titleShown = false
        })
        browser.loadingChanged.connect(function () {
            if (browser.loading) {
                page.barsHidden = false
                page.titleShown = false
            }
        })
        browser.swipedDown.connect(function () {
            page.titleShown = true
            page.barsHidden = false
        })
        browser.findResult.connect(function (found, count) {
            findCount.text = found || count > 0 ? qsTr("%1 found").arg(count) : qsTr("Not found")
        })
        browser.snapshotReady.connect(function (ok) {
            page.snapshotShown = ok
            if (page.pending == "menu") {
                page.menuOpen = true
                page.openActions([actForward, actHome, actAddBookmark, actBookmarks, actHistory,
                                  actFind, actSelect].concat(browser.editing ? [actPaste] : [],
                                  [actShare, actCopyUrl, actDownloads, actSettings, actRestart]))
            } else if (page.pending == "dialog") {
                page.dialogOpen = true
                var actions = []
                if (page.menuLink != "")
                    actions = actions.concat([actOpenLink, actCopyLink, actShareLink],
                                             page.menuText != "" ? [actCopyLinkText] : [])
                if (page.menuImage != "")
                    actions = actions.concat([actOpenImage, actSaveImage, actCopyImage],
                                             page.menuLink == "" ? [actShareImage] : [])
                page.openActions(actions)
            } else if (page.pending == "edit") {
                page.editCovered = address.focused && ok
            }
            page.pending = ""
        })
    }
}
