#ifndef BERRY_BROWSER_H
#define BERRY_BROWSER_H

#include <QMap>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QVariant>

#include "devtools.h"

namespace bb {
namespace system {
class InvokeRequest;
}
}

class Downloads;
class History;

// The browser as QML sees it: starts the Chromium engine as a child process
// whose window lands in the page's ForeignWindowControl, and drives it over
// the DevTools protocol (navigation, history, keys, the page script in
// assets/page.js). Also keeps the tabs, bookmarks and settings.
//
// Tabs are addresses, not running pages, as in webmee: the engine shows one
// page, and switching tabs loads the other tab's address and scroll position.
class Browser : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString url READ url NOTIFY pageChanged)
    Q_PROPERTY(QString title READ title NOTIFY pageChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
    Q_PROPERTY(QString engineState READ engineState NOTIFY engineStateChanged)
    Q_PROPERTY(QString windowId READ windowId CONSTANT)
    Q_PROPERTY(QVariantList bookmarks READ bookmarks NOTIFY bookmarksChanged)
    Q_PROPERTY(bool bookmarked READ bookmarked NOTIFY bookmarkedChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY restartNeededChanged)
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(int currentTab READ currentTab NOTIFY tabsChanged)
    Q_PROPERTY(bool selecting READ selecting NOTIFY selectionChanged)
    Q_PROPERTY(QString selectionText READ selectionText NOTIFY selectionChanged)
    Q_PROPERTY(bool editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(QString searchName READ searchName NOTIFY searchChanged)

public:
    Browser(History *history, Downloads *downloads, QObject *parent = 0);
    ~Browser();

    QString url() const { return m_url; }
    QString title() const { return m_title; }
    bool loading() const { return m_loading; }
    bool canGoBack() const { return m_historyIndex > 0; }
    bool canGoForward() const { return m_historyIndex >= 0 && m_historyIndex < m_historyIds.size() - 1; }
    QString engineState() const { return m_engineState; }
    QString windowId() const;
    QVariantList bookmarks() const;
    bool bookmarked() const;
    double progress() const { return m_progress; }
    bool restartNeeded() const { return m_restartNeeded; }
    QVariantList tabs() const { return m_tabs; }
    int currentTab() const { return m_currentTab; }
    bool selecting() const { return m_selecting; }
    QString selectionText() const { return m_selectionText; }
    bool editing() const { return m_editing; }
    QString searchName() const;

    // Starts the engine once the web view's size in pixels is known.
    Q_INVOKABLE void startEngine(int width, int height);
    Q_INVOKABLE void restartEngine();

    Q_INVOKABLE void open(const QString &input);
    Q_INVOKABLE void back();
    Q_INVOKABLE void forward();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void home();
    Q_INVOKABLE QString homeUrl() const;
    Q_INVOKABLE QString searchUrl(const QString &query) const;
    Q_INVOKABLE bool looksLikeAddress(const QString &input) const;

    // Bookmarks, then history, matching what is typed in the address bar.
    Q_INVOKABLE QVariantList suggest(const QString &text) const;

    Q_INVOKABLE void newTab(const QString &input = QString());
    Q_INVOKABLE void switchTab(int index);
    Q_INVOKABLE void closeTab(int index);

    // A key the page should get: Cascades' KeyEvent.key.
    Q_INVOKABLE void sendKey(int key);
    // Clipboard text into the focused field of the page.
    Q_INVOKABLE void paste();

    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void copySelection();
    Q_INVOKABLE void searchSelection();

    Q_INVOKABLE void find(const QString &text, bool backwards);
    Q_INVOKABLE void stopFind();

    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void copyUrl();
    Q_INVOKABLE void addBookmark();
    Q_INVOKABLE void removeBookmark(const QString &url);
    Q_INVOKABLE void clearCache();
    Q_INVOKABLE void clearCookies();

    // A picture of the page for Cascades to show while the engine window is
    // hidden under a menu or dialog; answered by snapshotReady.
    Q_INVOKABLE void snapshot();

    // The engine's output and the app's own lines about it.
    Q_INVOKABLE QString logPath() const;
    Q_INVOKABLE QString logTail() const;

    Q_INVOKABLE QVariant setting(const QString &key) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);

signals:
    void pageChanged();
    void loadingChanged();
    void historyChanged();
    void engineStateChanged();
    void bookmarksChanged();
    void bookmarkedChanged();
    void progressChanged();
    // A file:// address of the page's picture, or empty when there is none.
    void snapshotReady(const QString &url);
    void restartNeededChanged();
    void tabsChanged();
    void selectionChanged();
    void editingChanged();
    void searchChanged();
    void message(const QString &text);
    void pageTouched();
    // A long press on a link and/or an image; empty when not that kind.
    void contextMenu(const QString &link, const QString &linkText, const QString &image);
    void findResult(bool found, int count);

private slots:
    void launchIfIdle();
    void engineFinished(int exitCode, QProcess::ExitStatus status);
    void engineError(QProcess::ProcessError error);
    void probeEngine();
    void targetsListed(const QVariantList &targets);
    void attached();
    void detached();
    void replied(int id, const QString &method, const QVariantMap &result);
    void failed(int id, const QString &method, const QString &message);
    void protocolEvent(const QString &method, const QVariantMap &params);
    void refreshHistory();
    void invoked(const bb::system::InvokeRequest &request);
    void creepProgress();
    void snapshotTimedOut();
    void appVisible();
    void appHidden();

private:
    void launch();
    void setEngineState(const QString &state);
    void setLoading(bool loading);
    void navigate(const QString &url);
    void pageEvent(const QVariantMap &event);
    int evaluate(const QString &expression, const QString &purpose);
    void keyEvent(const QString &type, const QVariantMap &fields);
    void applySwipe();
    void applyMobileView();
    void setProgress(double progress);
    void setPageActive(bool active);
    void updateCurrentTab();
    void saveTabs();
    QString fixUp(const QString &input) const;
    QString engineDir() const;
    void log(const QString &line) const;
    QStringList engineArguments(const QString &startUrl) const;

    History *m_history;
    Downloads *m_downloads;
    QSettings m_settings;
    QProcess *m_engine;
    DevTools *m_devtools;
    QString m_pageScript;
    QTimer m_startTimer;
    QTimer m_probe;
    QTimer m_historyTimer;
    int m_probeAttempts;
    int m_port;
    int m_width;
    int m_height;
    bool m_stopping;
    bool m_restartNeeded;

    QString m_engineState;
    QString m_url;
    QString m_title;
    QString m_pendingUrl;
    QString m_mainFrameId;
    bool m_loading;
    int m_historyIndex;
    QVariantList m_historyIds;

    QVariantList m_tabs;
    int m_currentTab;
    int m_pendingScroll;

    bool m_selecting;
    QString m_selectionText;
    bool m_editing;
    QMap<int, QString> m_evaluations;

    double m_progress;
    QTimer m_progressTimer;
    int m_snapshotId;
    int m_snapshotCount;
    QTimer m_snapshotTimer;
    bool m_pageActive;
};

#endif
