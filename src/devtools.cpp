#include "devtools.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QTcpSocket>
#include <QtGlobal>

#include <bb/data/JsonDataAccess>

namespace {

const char kHost[] = "127.0.0.1";
const char kWsGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

QVariant parseJson(const QByteArray &bytes)
{
    bb::data::JsonDataAccess json;
    QVariant value = json.loadFromBuffer(bytes);
    return json.hasError() ? QVariant() : value;
}

QByteArray toJson(const QVariant &value)
{
    bb::data::JsonDataAccess json;
    QByteArray bytes;
    json.saveToBuffer(value, &bytes);
    return bytes;
}

// Offset of the body in an HTTP response, or -1 while the headers are
// incomplete. Sets *length to Content-Length, or -1 when there is none.
int bodyOffset(const QByteArray &response, int *length)
{
    int end = response.indexOf("\r\n\r\n");
    if (end < 0)
        return -1;
    *length = -1;
    foreach (const QByteArray &line, response.left(end).split('\n')) {
        int colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().toLower() == "content-length")
            *length = line.mid(colon + 1).trimmed().toInt();
    }
    return end + 4;
}

}  // namespace

DevTools::DevTools(QObject *parent)
    : QObject(parent)
    , m_port(0)
    , m_http(0)
    , m_httpDone(false)
    , m_ws(0)
    , m_handshaken(false)
    , m_nextId(1)
{
    qsrand(QDateTime::currentDateTime().toTime_t());
}

void DevTools::setPort(int port)
{
    m_port = port;
}

void DevTools::listTargets()
{
    if (m_http)
        return;
    m_httpBuffer.clear();
    m_httpDone = false;
    m_http = new QTcpSocket(this);
    connect(m_http, SIGNAL(connected()), this, SLOT(httpConnected()));
    connect(m_http, SIGNAL(readyRead()), this, SLOT(httpReadyRead()));
    connect(m_http, SIGNAL(disconnected()), this, SLOT(httpFinished()));
    connect(m_http, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(httpFinished()));
    m_http->connectToHost(kHost, m_port);
}

void DevTools::httpConnected()
{
    QByteArray request = "GET /json/list HTTP/1.1\r\nHost: ";
    request += kHost;
    request += ':' + QByteArray::number(m_port) + "\r\nConnection: close\r\n\r\n";
    m_http->write(request);
}

void DevTools::httpReadyRead()
{
    m_httpBuffer += m_http->readAll();
    int length = -1;
    int offset = bodyOffset(m_httpBuffer, &length);
    if (offset >= 0 && length >= 0 && m_httpBuffer.size() - offset >= length)
        httpFinished();
}

void DevTools::httpFinished()
{
    if (!m_http || m_httpDone)
        return;
    m_httpDone = true;
    m_http->disconnect(this);
    m_http->abort();
    m_http->deleteLater();
    m_http = 0;

    int length = -1;
    int offset = bodyOffset(m_httpBuffer, &length);
    QVariant targets;
    if (offset >= 0 && m_httpBuffer.startsWith("HTTP/1.1 200"))
        targets = parseJson(m_httpBuffer.mid(offset, length));
    if (targets.type() == QVariant::List)
        emit targetsListed(targets.toList());
    else
        emit targetsFailed();
}

void DevTools::attach(const QString &targetId)
{
    close();
    m_targetId = targetId;
    m_wsBuffer.clear();
    m_fragments.clear();
    m_handshaken = false;
    QByteArray nonce;
    for (int i = 0; i < 16; ++i)
        nonce.append(char(qrand() & 0xff));
    m_wsKey = nonce.toBase64();

    m_ws = new QTcpSocket(this);
    connect(m_ws, SIGNAL(connected()), this, SLOT(wsConnected()));
    connect(m_ws, SIGNAL(readyRead()), this, SLOT(wsReadyRead()));
    connect(m_ws, SIGNAL(disconnected()), this, SLOT(wsClosed()));
    connect(m_ws, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(wsClosed()));
    m_ws->connectToHost(kHost, m_port);
}

void DevTools::close()
{
    if (!m_ws)
        return;
    QTcpSocket *ws = m_ws;
    m_ws = 0;
    ws->disconnect(this);
    ws->abort();
    ws->deleteLater();
    m_pending.clear();
    if (m_handshaken) {
        m_handshaken = false;
        emit detached();
    }
}

bool DevTools::isAttached() const
{
    return m_ws && m_handshaken;
}

bool DevTools::isConnecting() const
{
    return m_ws && !m_handshaken;
}

void DevTools::wsConnected()
{
    // No Origin header: Chromium only checks --remote-allow-origins when one
    // is sent.
    QByteArray request = "GET /devtools/page/" + m_targetId.toUtf8() + " HTTP/1.1\r\n";
    request += "Host: " + QByteArray(kHost) + ':' + QByteArray::number(m_port) + "\r\n";
    request += "Upgrade: websocket\r\nConnection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: " + m_wsKey + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n\r\n";
    m_ws->write(request);
}

void DevTools::wsReadyRead()
{
    m_wsBuffer += m_ws->readAll();
    if (!m_handshaken) {
        int end = m_wsBuffer.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        QByteArray headers = m_wsBuffer.left(end);
        m_wsBuffer.remove(0, end + 4);
        QByteArray accept = QCryptographicHash::hash(m_wsKey + kWsGuid,
                                                     QCryptographicHash::Sha1).toBase64();
        if (!headers.startsWith("HTTP/1.1 101") || !headers.contains(accept)) {
            wsClosed();
            return;
        }
        m_handshaken = true;
        emit attached();
    }
    while (m_ws && parseFrames()) {
    }
}

// Takes one complete frame off the buffer; false when more bytes are needed.
bool DevTools::parseFrames()
{
    const QByteArray &b = m_wsBuffer;
    if (b.size() < 2)
        return false;
    const quint8 b0 = quint8(b[0]);
    const quint8 b1 = quint8(b[1]);
    const bool fin = b0 & 0x80;
    const int opcode = b0 & 0x0f;
    const bool masked = b1 & 0x80;
    quint64 length = b1 & 0x7f;
    int header = 2;
    if (length == 126) {
        if (b.size() < 4)
            return false;
        length = (quint64(quint8(b[2])) << 8) | quint8(b[3]);
        header = 4;
    } else if (length == 127) {
        if (b.size() < 10)
            return false;
        length = 0;
        for (int i = 2; i < 10; ++i)
            length = (length << 8) | quint8(b[i]);
        header = 10;
    }
    if (masked)
        header += 4;
    if (quint64(b.size() - header) < length)
        return false;

    QByteArray payload = b.mid(header, int(length));
    if (masked) {
        for (int i = 0; i < payload.size(); ++i)
            payload[i] = payload[i] ^ b[header - 4 + (i % 4)];
    }
    m_wsBuffer.remove(0, header + int(length));

    switch (opcode) {
    case 0x0:  // continuation
    case 0x1:  // text
        m_fragments += payload;
        if (fin) {
            QByteArray message = m_fragments;
            m_fragments.clear();
            handleMessage(message);
        }
        break;
    case 0x8:  // close
        wsClosed();
        return false;
    case 0x9:  // ping
        sendFrame(0xA, payload);
        break;
    default:
        break;
    }
    return true;
}

void DevTools::wsClosed()
{
    close();
}

void DevTools::sendFrame(int opcode, const QByteArray &payload)
{
    if (!m_ws)
        return;
    QByteArray frame;
    frame.append(char(0x80 | opcode));
    const int n = payload.size();
    if (n < 126) {
        frame.append(char(0x80 | n));
    } else if (n < 65536) {
        frame.append(char(0x80 | 126));
        frame.append(char((n >> 8) & 0xff));
        frame.append(char(n & 0xff));
    } else {
        frame.append(char(0x80 | 127));
        for (int shift = 56; shift >= 0; shift -= 8)
            frame.append(char((quint64(n) >> shift) & 0xff));
    }
    char mask[4];
    for (int i = 0; i < 4; ++i)
        mask[i] = char(qrand() & 0xff);
    frame.append(mask, 4);
    for (int i = 0; i < n; ++i)
        frame.append(payload[i] ^ mask[i % 4]);
    m_ws->write(frame);
}

int DevTools::call(const QString &method, const QVariantMap &params)
{
    if (!isAttached())
        return -1;
    const int id = m_nextId++;
    QVariantMap message;
    message["id"] = id;
    message["method"] = method;
    message["params"] = params;
    m_pending.insert(id, method);
    sendFrame(0x1, toJson(message));
    return id;
}

void DevTools::handleMessage(const QByteArray &text)
{
    const QVariantMap message = parseJson(text).toMap();
    if (message.contains("id")) {
        const int id = message.value("id").toInt();
        const QString method = m_pending.take(id);
        if (message.contains("error"))
            emit failed(id, method, message.value("error").toMap().value("message").toString());
        else
            emit replied(id, method, message.value("result").toMap());
    } else if (message.contains("method")) {
        emit event(message.value("method").toString(), message.value("params").toMap());
    }
}
