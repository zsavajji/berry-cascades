import bb.cascades 1.3

// An icon button in the bottom bar, with a touch target wider than its icon.
Container {
    id: button
    property string imageSource
    signal clicked()

    verticalAlignment: VerticalAlignment.Center
    preferredWidth: ui.du(10)
    layout: DockLayout {}
    ImageButton {
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        defaultImageSource: button.imageSource
        pressedImageSource: button.imageSource
        preferredWidth: ui.du(6)
        preferredHeight: ui.du(6)
        onClicked: button.clicked()
    }
}
