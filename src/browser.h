#ifndef BERRY_BROWSER_H
#define BERRY_BROWSER_H

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

// The browser as QML sees it: starts the Chromium engine as a child process
// whose window lands in the page's ForeignWindowControl, and drives it over
// the DevTools protocol (navigation, history, keys). Also keeps the
// bookmarks and settings.
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
    Q_PROPERTY(bool bookmarked READ bookmarked NOTIFY bookmarksChanged)
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY restartNeededChanged)

public:
    explicit Browser(QObject *parent = 0);
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
    bool restartNeeded() const { return m_restartNeeded; }

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

    // A key the page should get: Cascades' KeyEvent.key.
    Q_INVOKABLE void sendKey(int key);

    Q_INVOKABLE void addBookmark();
    Q_INVOKABLE void removeBookmark(const QString &url);
    Q_INVOKABLE void copyUrl();

    Q_INVOKABLE QVariant setting(const QString &key) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);

signals:
    void pageChanged();
    void loadingChanged();
    void historyChanged();
    void engineStateChanged();
    void bookmarksChanged();
    void restartNeededChanged();
    void message(const QString &text);
    void pageTouched();

private slots:
    void launchIfIdle();
    void engineFinished(int exitCode, QProcess::ExitStatus status);
    void engineError(QProcess::ProcessError error);
    void probeEngine();
    void targetsListed(const QVariantList &targets);
    void targetsFailed();
    void attached();
    void detached();
    void replied(int id, const QString &method, const QVariantMap &result);
    void failed(int id, const QString &method, const QString &message);
    void protocolEvent(const QString &method, const QVariantMap &params);
    void refreshHistory();
    void invoked(const bb::system::InvokeRequest &request);

private:
    void launch();
    void setEngineState(const QString &state);
    void setLoading(bool loading);
    void navigate(const QString &url);
    void keyEvent(const QString &type, const QVariantMap &fields);
    QString fixUp(const QString &input) const;
    QString searchUrl(const QString &query) const;
    QString engineDir() const;
    QString logPath() const;
    QStringList engineArguments(const QString &startUrl) const;

    QSettings m_settings;
    QProcess *m_engine;
    DevTools *m_devtools;
    QTimer m_startTimer;
    QTimer m_probe;
    QTimer m_historyTimer;
    int m_probeAttempts;
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
};

#endif
