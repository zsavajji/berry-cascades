#ifndef BERRY_DOWNLOADS_H
#define BERRY_DOWNLOADS_H

#include <QMap>
#include <QObject>
#include <QStringList>
#include <QVariant>

#include "devtools.h"

// Files saved this session, newest first: {id, name, path, url, received,
// total, state ("inProgress", "completed", "canceled", "failed")}.
//
// Pages' downloads go through the engine: the browser target's
// Browser.setDownloadBehavior points them at the shared Downloads folder and
// reports progress. "Save image" asks for the address with fetch(), which the
// browser has the engine load (Qt here speaks only TLS 1.0), and the data
// comes back through append() and finish(); a page cannot be made to download
// a cross-origin image itself.
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

    // The next part of a saved address; false once it is no longer wanted.
    bool append(const QString &id, const QByteArray &data);
    // All of it is in, or it failed for this reason.
    void finish(const QString &id, const QString &error);

signals:
    void changed();
    void message(const QString &text);
    void fetch(const QString &id, const QString &url);
    void log(const QString &line);

private slots:
    void versionFetched(const QVariantMap &version);
    void attached();
    void protocolEvent(const QString &method, const QVariantMap &params);
    void refused(int id, const QString &method, const QString &message);
    void unreachable();

private:
    int indexOf(const QString &id) const;
    void update(const QString &id, const QVariantMap &fields);
    QString uniquePath(const QString &name) const;

    QString m_folder;
    DevTools *m_tools;
    QVariantList m_items;
    int m_nextId;
};

#endif
