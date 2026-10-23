import bb.cascades 1.2
import bb.system 1.2

Page {
    id: historyPage
    signal done()
    property int count: 0

    titleBar: TitleBar {
        title: qsTr("History")
        dismissAction: ActionItem {
            title: qsTr("Close")
            onTriggered: historyPage.done()
        }
        acceptAction: ActionItem {
            title: qsTr("Clear")
            enabled: history.count > 0
            onTriggered: clearDialog.show()
        }
    }

    function reload() {
        var words = filter.text.toLowerCase().split(" ")
        var rows = []
        var entries = history.entries
        for (var i = 0; i < entries.length && rows.length < 300; ++i) {
            var e = entries[i]
            var hay = (e.url + " " + e.title).toLowerCase()
            var all = true
            for (var w = 0; w < words.length; ++w) {
                if (words[w] && hay.indexOf(words[w]) < 0) {
                    all = false
                    break
                }
            }
            if (all)
                rows.push({
                    title: e.title ? e.title : e.url,
                    url: e.url,
                    when: Qt.formatDateTime(new Date(e.time), "d MMM hh:mm")
                })
        }
        historyModel.clear()
        historyModel.append(rows)
        count = rows.length
    }

    Container {
        TextField {
            id: filter
            hintText: qsTr("Search history")
            topMargin: 10
            leftPadding: 12
            rightPadding: 12
            onTextChanging: historyPage.reload()
        }

        Container {
            layout: DockLayout {}
            Label {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                visible: historyPage.count == 0
                text: filter.text ? qsTr("Nothing matches.") : qsTr("No pages visited yet.")
            }

            ListView {
                dataModel: ArrayDataModel {
                    id: historyModel
                }

                function remove(url) {
                    history.remove(url)
                }

                listItemComponents: [
                    ListItemComponent {
                        StandardListItem {
                            id: item
                            title: ListItemData.title
                            description: ListItemData.url
                            status: ListItemData.when
                            contextActions: [
                                ActionSet {
                                    title: ListItemData.title
                                    DeleteActionItem {
                                        onTriggered: item.ListItem.view.remove(ListItemData.url)
                                    }
                                }
                            ]
                        }
                    }
                ]

                onTriggered: {
                    browser.open(dataModel.data(indexPath).url)
                    historyPage.done()
                }
            }
        }
    }

    attachedObjects: [
        SystemDialog {
            id: clearDialog
            title: qsTr("Clear history")
            body: qsTr("Forget all %1 visited pages?").arg(history.count)
            onFinished: {
                if (result == SystemUiResult.ConfirmButtonSelection)
                    history.clear()
            }
        }
    ]

    // Lists are filled while the sheet is open, not on every change behind it.
    property bool active: false
    function show() {
        historyPage.active = true
        historyPage.reload()
    }

    onCreationCompleted: {
        history.changed.connect(function () {
            if (historyPage.active)
                historyPage.reload()
        })
    }
}
