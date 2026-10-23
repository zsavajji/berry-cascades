#ifndef BERRY_DEVTOOLS_H
#define BERRY_DEVTOOLS_H

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVariant>

class QTcpSocket;

// A Chrome DevTools Protocol client for the engine's --remote-debugging-port.
// Qt 4 has no WebSocket or JSON classes, so this carries a minimal RFC 6455
// client (text frames, ping/pong, close) and uses bb::data::JsonDataAccess.
//
// listTargets() asks the HTTP endpoint for /json/list and attach() opens a
// page's WebSocket; fetchVersion() and attachBrowser() do the same for the
// browser target. Replies to call() arrive as replied(id, method, result),
// protocol events as event(method, params).
class DevTools : public QObject
{
    Q_OBJECT
public:
    explicit DevTools(QObject *parent = 0);

    void setPort(int port);
    void listTargets();
    void fetchVersion();
    void attach(const QString &targetId);
    void attachBrowser(const QString &webSocketUrl);
    void close();
    bool isAttached() const;
    bool isConnecting() const;
    QString targetId() const { return m_targetId; }

    int call(const QString &method, const QVariantMap &params = QVariantMap());

signals:
    void targetsListed(const QVariantList &targets);
    void versionFetched(const QVariantMap &version);
    void targetsFailed();
    void attached();
    void detached();
    void replied(int id, const QString &method, const QVariantMap &result);
    void failed(int id, const QString &method, const QString &message);
    void event(const QString &method, const QVariantMap &params);

private slots:
    void httpConnected();
    void httpReadyRead();
    void httpFinished();
    void wsConnected();
    void wsReadyRead();
    void wsClosed();

private:
    void fetch(const QByteArray &path);
    void open(const QByteArray &path);
    void sendFrame(int opcode, const QByteArray &payload);
    void handleMessage(const QByteArray &text);
    bool parseFrames();

    int m_port;
    QTcpSocket *m_http;
    QByteArray m_httpPath;
    QByteArray m_httpBuffer;
    bool m_httpDone;

    QTcpSocket *m_ws;
    QString m_targetId;
    QByteArray m_wsPath;
    QByteArray m_wsKey;
    QByteArray m_wsBuffer;
    QByteArray m_fragments;
    bool m_handshaken;

    int m_nextId;
    QMap<int, QString> m_pending;
};

#endif
