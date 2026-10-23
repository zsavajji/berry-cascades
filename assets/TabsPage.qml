import bb.cascades 1.2

// Tabs are addresses: opening one loads its page in the one engine window,
// where it was left.
Page {
    id: tabsPage
    signal done()

    titleBar: TitleBar {
        title: qsTr("Tabs")
        dismissAction: ActionItem {
            title: qsTr("Close")
            onTriggered: tabsPage.done()
        }
        acceptAction: ActionItem {
            title: qsTr("New")
            onTriggered: {
                browser.newTab("")
                tabsPage.done()
            }
        }
    }

    function reload() {
        tabModel.clear()
        var tabs = browser.tabs
        for (var i = 0; i < tabs.length; ++i) {
            tabModel.append({
                tab: i,
                title: tabs[i].title ? tabs[i].title : (tabs[i].url ? tabs[i].url : qsTr("New tab")),
                url: tabs[i].url ? tabs[i].url : "",
                current: i == browser.currentTab
            })
        }
    }

    ListView {
        dataModel: ArrayDataModel {
            id: tabModel
        }

        function closeTab(index) {
            browser.closeTab(index)
        }

        listItemComponents: [
            ListItemComponent {
                StandardListItem {
                    id: item
                    title: ListItemData.title
                    description: ListItemData.url
                    status: ListItemData.current ? qsTr("Open") : ""
                    imageSource: "asset:///images/tabs.png"
                    contextActions: [
                        ActionSet {
                            title: ListItemData.title
                            DeleteActionItem {
                                title: qsTr("Close tab")
                                onTriggered: item.ListItem.view.closeTab(ListItemData.tab)
                            }
                        }
                    ]
                }
            }
        ]

        onTriggered: {
            browser.switchTab(dataModel.data(indexPath).tab)
            tabsPage.done()
        }
    }

    onCreationCompleted: {
        reload()
        browser.tabsChanged.connect(reload)
    }
}
