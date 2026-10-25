import bb.cascades 1.4

// The Active Frame: the browser's card while it is minimized, with the
// page's icon, title and address (main.cpp makes it the app's cover).
Container {
    background: Color.create("#121212")
    layout: DockLayout {}

    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Center
        leftPadding: ui.du(2)
        rightPadding: ui.du(2)
        ImageView {
            visible: browser.icon != ""
            horizontalAlignment: HorizontalAlignment.Center
            imageSource: browser.icon
            preferredWidth: ui.du(4)
            preferredHeight: ui.du(4)
            bottomMargin: ui.du(1)
        }
        Label {
            horizontalAlignment: HorizontalAlignment.Center
            text: browser.title != "" ? browser.title : "ChromeBerry"
            // Two lines at most, so the card holds the icon and address too.
            multiline: true
            autoSize.maxLineCount: 2
            textStyle.textAlign: TextAlign.Center
            textStyle.fontSize: FontSize.XSmall
            textStyle.color: Color.White
        }
        Label {
            horizontalAlignment: HorizontalAlignment.Center
            text: cover.address(browser.url)
            textStyle.fontSize: FontSize.XXSmall
            textStyle.color: Color.create("#9a9a9a")
        }
    }

    id: cover
    // As main.qml's displayUrl: the address without its scheme.
    function address(url) {
        if (url.indexOf("file://") == 0)
            return ""
        return url.replace(/^[a-z]+:\/\//, "").replace(/^www\./, "")
    }
}
