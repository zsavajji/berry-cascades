#ifndef BERRY_BROWSER_H
#define BERRY_BROWSER_H

#include <QElapsedTimer>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QVariant>

#include <bb/AbstractBpsEventHandler>

#include "devtools.h"

namespace bb {
class ImageData;
class UIToolkitSupport;
}

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
// assets/page.js). Also keeps the bookmarks, the settings and the last
// page, which the next start opens.
class Browser : public QObject, public bb::AbstractBpsEventHandler
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
    Q_PROPERTY(bool selecting READ selecting NOTIFY selectionChanged)
    Q_PROPERTY(QString selectionText READ selectionText NOTIFY selectionChanged)
    Q_PROPERTY(bool editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(QString searchName READ searchName NOTIFY searchChanged)
    // A file:// address of the page's icon, or empty.
    Q_PROPERTY(QString icon READ icon NOTIFY iconChanged)
    Q_PROPERTY(QVariant snapshotImage READ snapshotImage NOTIFY snapshotReady)
    // How far the full bar and the title strip are in, 0 (out) to 1 (in).
    Q_PROPERTY(double barsIn READ barsIn NOTIFY barsMoved)
    Q_PROPERTY(double titleIn READ titleIn NOTIFY barsMoved)
    // The on-screen keyboard's height in pixels while it shows, else 0.
    Q_PROPERTY(int keyboardHeight READ keyboardHeight NOTIFY keyboardHeightChanged)
    // The phone's scrolling input, for the settings that go with it: a
    // trackpad (Classic) by HardwareInfo; a touch keyboard (Passport) once
    // it has reported a touch, since no API tells before.
    Q_PROPERTY(bool hasTrackpad READ hasTrackpad CONSTANT)
    Q_PROPERTY(bool hasTouchKeyboard READ hasTouchKeyboard NOTIFY hasTouchKeyboardChanged)

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
    bool selecting() const { return m_selecting; }
    QString selectionText() const { return m_selectionText; }
    bool editing() const { return m_editing; }
    QString searchName() const;
    QString icon() const { return m_icon; }
    QVariant snapshotImage() const { return m_snapshotImage; }
    double barsIn() const { return m_barsIn; }
    double titleIn() const { return m_titleIn; }
    int keyboardHeight() const { return m_keyboardHeight; }
    bool hasTrackpad() const;
    bool hasTouchKeyboard() const { return m_settings.value("touchKeyboard", false).toBool(); }

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

    // A key the page should get: Cascades' KeyEvent.key, or the key code
    // for one the Q10 cut to its low byte.
    Q_INVOKABLE void sendKey(int key);
    // Text typed into the page's focused text box.
    Q_INVOKABLE void typeText(const QString &text);
    // Clipboard text into the focused field of the page.
    Q_INVOKABLE void paste();
    // A swipe on the trackpad (Q20), in its units, or on the touch keyboard
    // (Passport), in screen pixels. No default argument: moc would add a
    // two-argument clone, and QML1 mis-picked between such twins on Harmattan.
    Q_INVOKABLE void scrollPage(double dx, double dy, bool pixels);
    // The finger left the trackpad or the touch keyboard: the page glides on
    // and slows down, as after a flick on the screen.
    Q_INVOKABLE void endScroll();
    Q_INVOKABLE void noteTouchKeyboard();

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
    // hidden under a menu, a dialog or the address; answered by snapshotReady,
    // with snapshotImage set if ok.
    Q_INVOKABLE void snapshot();

    // The stock bars, drawn by Cascades under the page's window (view, its
    // ForeignWindowControl): heights in pixels of the title strip, the slim
    // bar (always there) and the full bar. showBars slides the full bar and
    // the title strip in or out (barsIn, titleIn) and clips the page's
    // window in step, so they show where it no longer is.
    Q_INVOKABLE void setBarHeights(int title, int slim, int full);
    Q_INVOKABLE void showBars(QObject *view, bool bars, bool title, bool animate);
    // The bars raised by this many pixels (above the on-screen keyboard),
    // the page's window clipped to match.
    Q_INVOKABLE void liftBars(int pixels);

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
    void snapshotReady(bool ok);
    void restartNeededChanged();
    void selectionChanged();
    void editingChanged();
    void searchChanged();
    void message(const QString &text);
    void pageTouched();
    // A long press on a link and/or an image; empty when not that kind.
    void contextMenu(const QString &link, const QString &linkText, const QString &image);
    void findResult(bool found, int count);
    // The page scrolled down (the bars give way) or back up.
    void pageScrolled(bool down);
    void iconChanged();
    void barsMoved();
    void swipedDown();
    void keyboardHeightChanged();
    void hasTouchKeyboardChanged();

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
    void swipeFromTop();
    void stepBars();
    void glide();
    void fetchForDownload(const QString &id, const QString &url);
    void downloadsLog(const QString &line) { log("downloads: " + line); }

private:
    void launch();
    void setEngineState(const QString &state);
    void setLoading(bool loading);
    void navigate(const QString &url);
    void pageEvent(const QVariantMap &event);
    int evaluate(const QString &expression, const QString &purpose);
    void keyEvent(const QString &type, const QVariantMap &fields);
    void wheel(double dx, double dy);
    void applySwipe();
    void applyMobileView();
    void setProgress(double progress);
    void rememberPage();
    void setPageClip(int top, int bottom);
    bool grabPage();
    bool setSnapshot(const bb::ImageData &image);
    void saveIcon(const QString &dataUrl);
    void setIcon(const QString &icon);
    void readSaved(const QString &id, const QString &stream);
    QString fixUp(const QString &input) const;
    QString engineDir() const;
    void log(const QString &line) const;
    void event(bps_event_t *event);
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

    bool m_selecting;
    QString m_selectionText;
    bool m_editing;
    QMap<int, QString> m_evaluations;
    QMap<int, QStringList> m_saves;   // "Save image" calls: {download id, stream}

    double m_progress;
    QTimer m_progressTimer;
    int m_snapshotId;
    QVariant m_snapshotImage;   // a bb::cascades::Image
    QTimer m_snapshotTimer;

    bb::UIToolkitSupport *m_toolkit;

    QPointer<QObject> m_pageView;
    int m_titleHeight, m_slimHeight, m_fullHeight, m_barsLift;
    double m_barsIn, m_titleIn, m_barsFrom, m_titleFrom, m_barsTo, m_titleTo;
    QElapsedTimer m_barsClock;
    QTimer m_barsTimer;
    bool m_keyboardShown;
    int m_keyboardHeight;

    // Trackpad and touch-keyboard swipes: their speed in wheel pixels per ms,
    // and the glide after the finger lifts.
    QElapsedTimer m_scrollClock;
    QTimer m_glideTimer;
    double m_scrollVX, m_scrollVY;

    // The content blocklist's domains (blocklist.txt, tools/fetch-blocklist.py).
    QSet<QByteArray> m_blocklist;
    QString m_icon;
    int m_iconCount;
};

#endif
