#ifndef BERRY_DOWNLOADS_H
#define BERRY_DOWNLOADS_H

#include <QMap>
#include <QObject>
#include <QVariant>

#include "devtools.h"

class QNetworkAccessManager;
class QNetworkReply;

// Files saved this session, newest first: {id, name, path, url, received,
// total, state ("inProgress", "completed", "canceled", "failed")}.
//
// Pages' downloads go through the engine: the browser target's
// Browser.setDownloadBehavior points them at the shared Downloads folder and
// reports progress. "Save image" fetches the address itself with Qt, trusting
// the engine's root store, since a page cannot be made to download a
// cross-origin image.
class Downloads : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(QString folder READ folder CONSTANT)

public:
    explicit Downloads(QObject *parent = 0);

    QVariantList items() const { return m_items; }
    QString folder() const { return m_folder; }

    // The engine is up on this DevTools port.
    void connectEngine(int port);
    void disconnectEngine();

    Q_INVOKABLE void save(const QString &url);
    Q_INVOKABLE void cancel(const QString &id);
    Q_INVOKABLE void open(const QString &id);
    Q_INVOKABLE void clearFinished();

signals:
    void changed();
    void message(const QString &text);

private slots:
    void versionFetched(const QVariantMap &version);
    void attached();
    void protocolEvent(const QString &method, const QVariantMap &params);
    void replyProgress(qint64 received, qint64 total);
    void replyFinished();

private:
    int indexOf(const QString &id) const;
    void update(const QString &id, const QVariantMap &fields);
    QString uniquePath(const QString &name) const;

    QString m_folder;
    DevTools *m_tools;
    QNetworkAccessManager *m_network;
    QMap<QNetworkReply *, QString> m_replies;
    QVariantList m_items;
    int m_nextId;
};

#endif
