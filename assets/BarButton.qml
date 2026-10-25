import bb.cascades 1.4

// An icon button in the bottom bar, with a touch target wider than its icon.
Container {
    id: button
    property string imageSource
    signal clicked()

    verticalAlignment: VerticalAlignment.Center
    preferredWidth: ui.du(8)
    layout: DockLayout {}
    ImageButton {
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        defaultImageSource: button.imageSource
        pressedImageSource: button.imageSource
        preferredWidth: ui.du(5)
        preferredHeight: ui.du(5)
        onClicked: button.clicked()
    }
}
