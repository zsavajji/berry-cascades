import bb.cascades 1.2

Page {
    id: bookmarksPage
    signal done()
    property int count: 0

    titleBar: TitleBar {
        title: qsTr("Bookmarks")
        dismissAction: ActionItem {
            title: qsTr("Close")
            onTriggered: bookmarksPage.done()
        }
    }

    function reload() {
        bookmarkModel.clear()
        bookmarkModel.append(browser.bookmarks)
        count = bookmarkModel.size()
    }

    Container {
        layout: DockLayout {}

        Label {
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            visible: bookmarksPage.count == 0
            text: qsTr("No bookmarks yet. Add one from the menu while on a page.")
            multiline: true
            textStyle.textAlign: TextAlign.Center
        }

        ListView {
            id: list
            dataModel: ArrayDataModel {
                id: bookmarkModel
            }

            function remove(url) {
                browser.removeBookmark(url)
            }

            listItemComponents: [
                ListItemComponent {
                    StandardListItem {
                        id: item
                        title: ListItemData.title
                        description: ListItemData.url
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
                var bookmark = dataModel.data(indexPath)
                browser.open(bookmark.url)
                bookmarksPage.done()
            }
        }
    }

    onCreationCompleted: {
        reload()
        browser.bookmarksChanged.connect(reload)
    }
}
