#include "downloads.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QUrl>

#include <bb/system/InvokeManager>
#include <bb/system/InvokeRequest>

namespace {

const char kSharedDownloads[] = "/accounts/1000/shared/downloads";

// A file name from the end of an address, without its query.
QString nameFromUrl(const QUrl &url)
{
    QString name = QFileInfo(url.path()).fileName();
    name.replace(QRegExp("[^A-Za-z0-9._ -]"), "_");
    return name.isEmpty() ? QString("download") : name;
}

}  // namespace

Downloads::Downloads(QObject *parent)
    : QObject(parent)
    , m_tools(new DevTools(this))
    , m_network(0)
    , m_nextId(1)
{
    QFileInfo shared(kSharedDownloads);
    m_folder = shared.isDir() && shared.isWritable()
        ? QString(kSharedDownloads) : QDir::homePath() + "/downloads";
    QDir().mkpath(m_folder);

    connect(m_tools, SIGNAL(versionFetched(QVariantMap)), this, SLOT(versionFetched(QVariantMap)));
    connect(m_tools, SIGNAL(attached()), this, SLOT(attached()));
    connect(m_tools, SIGNAL(event(QString, QVariantMap)),
            this, SLOT(protocolEvent(QString, QVariantMap)));
}

// ---- downloads the engine makes -----------------------------------------

void Downloads::connectEngine(int port)
{
    m_tools->close();
    m_tools->setPort(port);
    m_tools->fetchVersion();
}

void Downloads::disconnectEngine()
{
    m_tools->close();
    for (int i = 0; i < m_items.size(); ++i) {
        QVariantMap item = m_items.at(i).toMap();
        if (item.value("state") == "inProgress" && !item.value("id").toString().startsWith("q")) {
            item["state"] = "failed";
            m_items[i] = item;
        }
    }
    emit changed();
}

void Downloads::versionFetched(const QVariantMap &version)
{
    m_tools->attachBrowser(version.value("webSocketDebuggerUrl").toString());
}

void Downloads::attached()
{
    QVariantMap params;
    // Saved under the download's guid, then renamed when complete: the name
    // is then ours to pick, so the list knows where every file is.
    params["behavior"] = QString("allowAndName");
    params["downloadPath"] = m_folder;
    params["eventsEnabled"] = true;
    m_tools->call("Browser.setDownloadBehavior", params);
}

void Downloads::protocolEvent(const QString &method, const QVariantMap &params)
{
    const QString id = params.value("guid").toString();
    if (method == "Browser.downloadWillBegin") {
        QVariantMap item;
        item["id"] = id;
        item["url"] = params.value("url");
        item["name"] = params.value("suggestedFilename");
        item["path"] = m_folder + "/" + id;
        item["received"] = 0;
        item["total"] = 0;
        item["state"] = QString("inProgress");
        m_items.prepend(item);
        emit changed();
        emit message(tr("Downloading %1").arg(item.value("name").toString()));
    } else if (method == "Browser.downloadProgress") {
        QVariantMap fields;
        fields["received"] = params.value("receivedBytes");
        fields["total"] = params.value("totalBytes");
        fields["state"] = params.value("state");
        const int index = indexOf(id);
        if (index < 0)
            return;
        const QVariantMap item = m_items.at(index).toMap();
        const bool wasRunning = item.value("state") == "inProgress";
        const QString name = item.value("name").toString();
        if (wasRunning && fields.value("state") == "completed") {
            const QString path = uniquePath(name.isEmpty() ? QString("download") : name);
            if (QFile::rename(m_folder + "/" + id, path)) {
                fields["path"] = path;
                fields["name"] = QFileInfo(path).fileName();
            }
            emit message(tr("Saved %1 to Downloads").arg(fields.value("name", name).toString()));
        } else if (wasRunning && fields.value("state") == "canceled") {
            emit message(tr("Download of %1 canceled").arg(name));
        }
        update(id, fields);
    }
}

// ---- "Save image" ----------------------------------------------------------

void Downloads::save(const QString &address)
{
    const QUrl url(address);
    const QString id = "q" + QString::number(m_nextId++);
    QVariantMap item;
    item["id"] = id;
    item["url"] = address;
    item["received"] = 0;
    item["total"] = 0;
    item["state"] = QString("inProgress");

    if (url.scheme() == "data") {
        // data:[<type>][;base64],<data>
        const QByteArray raw = address.toUtf8();
        const int comma = raw.indexOf(',');
        const QByteArray header = raw.mid(5, comma - 5);
        QByteArray body = QByteArray::fromPercentEncoding(raw.mid(comma + 1));
        if (header.endsWith(";base64"))
            body = QByteArray::fromBase64(body);
        QString ext = QString::fromUtf8(header.split(';').first()).section('/', 1);
        if (ext.isEmpty() || ext.contains('+'))
            ext = "bin";
        const QString path = uniquePath("image." + ext);
        QFile file(path);
        const bool ok = comma > 0 && file.open(QIODevice::WriteOnly) && file.write(body) == body.size();
        item["name"] = QFileInfo(path).fileName();
        item["path"] = path;
        item["received"] = body.size();
        item["total"] = body.size();
        item["state"] = QString(ok ? "completed" : "failed");
        m_items.prepend(item);
        emit changed();
        emit message(ok ? tr("Saved %1 to Downloads").arg(item.value("name").toString())
                        : tr("Could not save the image"));
        return;
    }

    if (url.scheme() != "http" && url.scheme() != "https") {
        emit message(tr("Cannot save this address"));
        return;
    }

    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
        // The phone's own root store predates many of today's certificates;
        // the engine ships a current one.
        const QList<QSslCertificate> roots = QSslCertificate::fromPath(
            QCoreApplication::applicationDirPath() + "/engine/root_store.certs");
        if (!roots.isEmpty()) {
            QSslConfiguration config = QSslConfiguration::defaultConfiguration();
            config.setCaCertificates(roots);
            QSslConfiguration::setDefaultConfiguration(config);
        }
    }
    const QString path = uniquePath(nameFromUrl(url));
    item["name"] = QFileInfo(path).fileName();
    item["path"] = path;
    m_items.prepend(item);
    emit changed();
    emit message(tr("Downloading %1").arg(item.value("name").toString()));

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Linux; Android 10; BlackBerry) Mobile");
    QNetworkReply *reply = m_network->get(request);
    m_replies.insert(reply, id);
    connect(reply, SIGNAL(downloadProgress(qint64, qint64)), this, SLOT(replyProgress(qint64, qint64)));
    connect(reply, SIGNAL(finished()), this, SLOT(replyFinished()));
}

void Downloads::replyProgress(qint64 received, qint64 total)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || !m_replies.contains(reply))
        return;
    QVariantMap fields;
    fields["received"] = received;
    fields["total"] = qMax(total, qint64(0));
    update(m_replies.value(reply), fields);
}

void Downloads::replyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || !m_replies.contains(reply))
        return;
    const QString id = m_replies.take(reply);
    reply->deleteLater();
    const int index = indexOf(id);
    if (index < 0)
        return;
    const QVariantMap item = m_items.at(index).toMap();
    QVariantMap fields;
    if (reply->error() == QNetworkReply::OperationCanceledError) {
        fields["state"] = QString("canceled");
    } else if (reply->error() != QNetworkReply::NoError) {
        fields["state"] = QString("failed");
        emit message(tr("Download failed: %1").arg(reply->errorString()));
    } else {
        const QByteArray body = reply->readAll();
        QFile file(item.value("path").toString());
        const bool ok = file.open(QIODevice::WriteOnly) && file.write(body) == body.size();
        fields["state"] = QString(ok ? "completed" : "failed");
        fields["received"] = body.size();
        fields["total"] = body.size();
        emit message(ok ? tr("Saved %1 to Downloads").arg(item.value("name").toString())
                        : tr("Could not write %1").arg(item.value("path").toString()));
    }
    update(id, fields);
}

// ---- the list --------------------------------------------------------------

void Downloads::cancel(const QString &id)
{
    for (QMap<QNetworkReply *, QString>::iterator it = m_replies.begin(); it != m_replies.end(); ++it) {
        if (it.value() == id) {
            it.key()->abort();
            return;
        }
    }
    QVariantMap params;
    params["guid"] = id;
    m_tools->call("Browser.cancelDownload", params);
}

void Downloads::open(const QString &id)
{
    const int index = indexOf(id);
    if (index < 0)
        return;
    const QVariantMap item = m_items.at(index).toMap();
    if (item.value("state") != "completed")
        return;
    bb::system::InvokeRequest request;
    request.setAction("bb.action.OPEN");
    request.setUri(QUrl::fromLocalFile(item.value("path").toString()));
    bb::system::InvokeManager manager;
    manager.invoke(request);
}

void Downloads::clearFinished()
{
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items.at(i).toMap().value("state") != "inProgress")
            m_items.removeAt(i);
    }
    emit changed();
}

int Downloads::indexOf(const QString &id) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).toMap().value("id").toString() == id)
            return i;
    }
    return -1;
}

void Downloads::update(const QString &id, const QVariantMap &fields)
{
    const int index = indexOf(id);
    if (index < 0)
        return;
    QVariantMap item = m_items.at(index).toMap();
    for (QVariantMap::const_iterator it = fields.begin(); it != fields.end(); ++it)
        item[it.key()] = it.value();
    m_items[index] = item;
    emit changed();
}

QString Downloads::uniquePath(const QString &name) const
{
    const QFileInfo info(name);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix().isEmpty() ? QString() : "." + info.suffix();
    QString path = m_folder + "/" + name;
    for (int n = 1; QFile::exists(path); ++n)
        path = QString("%1/%2-%3%4").arg(m_folder, base).arg(n).arg(suffix);
    return path;
}
