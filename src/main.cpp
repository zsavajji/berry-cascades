#include <bb/cascades/AbstractPane>
#include <bb/cascades/Application>
#include <bb/cascades/QmlDocument>

#include "browser.h"
#include "downloads.h"
#include "history.h"

using namespace bb::cascades;

Q_DECL_EXPORT int main(int argc, char **argv)
{
    Application app(argc, argv);
    History history(&app);
    Downloads downloads(&app);
    Browser browser(&history, &downloads, &app);

    QmlDocument *document = QmlDocument::create("asset:///main.qml").parent(&app);
    if (document->hasErrors())
        return 1;
    document->setContextProperty("browser", &browser);
    document->setContextProperty("history", &history);
    document->setContextProperty("downloads", &downloads);

    AbstractPane *root = document->createRootObject<AbstractPane>();
    if (!root)
        return 1;

    app.setScene(root);
    // A swipe down from the top edge shows the page's title strip (Browser
    // gets it from UIToolkitSupport), not Cascades' application menu, which
    // this app does not have and which would otherwise slide down empty.
    app.setMenuEnabled(false);
    return Application::exec();
}
