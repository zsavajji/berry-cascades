import bb.cascades 1.2

Page {
    id: downloadsPage
    signal done()
    property int count: 0

    titleBar: TitleBar {
        title: qsTr("Downloads")
        dismissAction: ActionItem {
            title: qsTr("Close")
            onTriggered: downloadsPage.done()
        }
        acceptAction: ActionItem {
            title: qsTr("Clear")
            onTriggered: downloads.clearFinished()
        }
    }

    function size(bytes) {
        if (bytes >= 1048576)
            return (bytes / 1048576).toFixed(1) + " MB"
        return Math.round(bytes / 1024) + " KB"
    }

    function reload() {
        var rows = []
        var items = downloads.items
        for (var i = 0; i < items.length; ++i) {
            var d = items[i]
            var status = d.state == "completed" ? size(d.total)
                : d.state == "inProgress" ? (d.total > 0 ? Math.floor(100 * d.received / d.total) + "%" : size(d.received))
                : d.state == "canceled" ? qsTr("Canceled") : qsTr("Failed")
            rows.push({id: d.id, name: d.name, url: d.url, status: status, running: d.state == "inProgress"})
        }
        downloadModel.clear()
        downloadModel.append(rows)
        count = rows.length
    }

    Container {
        Label {
            leftMargin: 12
            topMargin: 10
            text: qsTr("Saved to %1").arg(downloads.folder)
            textStyle.base: SystemDefaults.TextStyles.SubtitleText
            textStyle.color: Color.create("#8d89a6")
        }

        Container {
            layout: DockLayout {}
            Label {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                visible: downloadsPage.count == 0
                text: qsTr("Nothing downloaded yet. Long-press an image to save it.")
                multiline: true
                textStyle.textAlign: TextAlign.Center
            }

            ListView {
                dataModel: ArrayDataModel {
                    id: downloadModel
                }

                function cancel(id) {
                    downloads.cancel(id)
                }

                listItemComponents: [
                    ListItemComponent {
                        StandardListItem {
                            id: item
                            title: ListItemData.name
                            description: ListItemData.url
                            status: ListItemData.status
                            imageSource: "asset:///images/downloads.png"
                            contextActions: [
                                ActionSet {
                                    title: ListItemData.name
                                    ActionItem {
                                        title: qsTr("Cancel download")
                                        enabled: ListItemData.running
                                        imageSource: "asset:///images/stop.png"
                                        onTriggered: item.ListItem.view.cancel(ListItemData.id)
                                    }
                                }
                            ]
                        }
                    }
                ]

                onTriggered: downloads.open(dataModel.data(indexPath).id)
            }
        }
    }

    // Lists are filled while the sheet is open, not on every change behind it.
    property bool active: false
    function show() {
        downloadsPage.active = true
        downloadsPage.reload()
    }

    onCreationCompleted: {
        downloads.changed.connect(function () {
            if (downloadsPage.active)
                downloadsPage.reload()
        })
    }
}
