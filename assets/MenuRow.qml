import bb.cascades 1.3

// One row of the ⋮ menu, drawn like the stock action menu: icon and title on
// black, a lighter press state, dimmed when disabled.
Container {
    id: row
    property alias title: label.text
    property alias imageSource: icon.imageSource
    signal triggered()
    property real verticalPadding: ui.du(1.6)

    horizontalAlignment: HorizontalAlignment.Fill
    background: pressedState ? Color.create("#2b2b2b") : Color.Transparent
    opacity: enabled ? 1.0 : 0.35
    property bool pressedState: false
    topPadding: verticalPadding
    bottomPadding: verticalPadding
    leftPadding: ui.du(2.2)
    rightPadding: ui.du(2.2)
    layout: StackLayout {
        orientation: LayoutOrientation.LeftToRight
    }

    ImageView {
        id: icon
        preferredWidth: ui.du(6)
        preferredHeight: ui.du(6)
        verticalAlignment: VerticalAlignment.Center
    }
    Label {
        id: label
        leftMargin: ui.du(2)
        verticalAlignment: VerticalAlignment.Center
        textStyle.color: Color.White
        textStyle.base: SystemDefaults.TextStyles.PrimaryText
    }

    onTouch: {
        if (!row.enabled)
            return
        if (event.isDown())
            row.pressedState = true
        else if (event.isUp() || event.isCancel())
            row.pressedState = false
    }
    gestureHandlers: [
        TapHandler {
            onTapped: {
                if (row.enabled)
                    row.triggered()
            }
        }
    ]
}
