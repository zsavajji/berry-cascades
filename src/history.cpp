#include "history.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStringList>

#include <bb/data/JsonDataAccess>

namespace {

const int kMaxEntries = 1000;

}  // namespace

History::History(QObject *parent)
    : QObject(parent)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(2000);
    connect(&m_saveTimer, SIGNAL(timeout()), this, SLOT(save()));

    if (QFile::exists(path())) {
        bb::data::JsonDataAccess json;
        const QVariant data = json.load(path());
        if (!json.hasError())
            m_entries = data.toList();
    }
}

History::~History()
{
    if (m_saveTimer.isActive())
        save();
}

QString History::path() const
{
    return QDir::homePath() + "/history.json";
}

void History::save()
{
    m_saveTimer.stop();
    bb::data::JsonDataAccess json;
    json.save(QVariant(m_entries), path());
}

void History::visit(const QString &url, const QString &title)
{
    if (!url.startsWith("http://") && !url.startsWith("https://"))
        return;

    if (!m_entries.isEmpty()) {
        QVariantMap top = m_entries.first().toMap();
        if (top.value("url").toString() == url) {
            if (!title.isEmpty() && top.value("title").toString() != title) {
                top["title"] = title;
                m_entries[0] = top;
                emit changed();
                m_saveTimer.start();
            }
            return;
        }
    }

    int visits = 0;
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        const QVariantMap entry = m_entries.at(i).toMap();
        if (entry.value("url").toString() == url) {
            visits = entry.value("visits").toInt();
            m_entries.removeAt(i);
        }
    }
    QVariantMap entry;
    entry["url"] = url;
    entry["title"] = title;
    entry["visits"] = visits + 1;
    entry["time"] = QDateTime::currentMSecsSinceEpoch();
    m_entries.prepend(entry);
    while (m_entries.size() > kMaxEntries)
        m_entries.removeLast();
    emit changed();
    m_saveTimer.start();
}

QVariantList History::matches(const QString &text, int limit) const
{
    const QStringList words = text.toLower().split(' ', QString::SkipEmptyParts);
    QVariantList found;
    if (words.isEmpty())
        return found;
    foreach (const QVariant &e, m_entries) {
        const QVariantMap entry = e.toMap();
        const QString hay = entry.value("url").toString().toLower() + ' '
                          + entry.value("title").toString().toLower();
        bool all = true;
        foreach (const QString &w, words) {
            if (!hay.contains(w)) {
                all = false;
                break;
            }
        }
        if (all)
            found.append(entry);
    }
    // Most visited first; the list is newest first, so a stable sort keeps
    // recent pages ahead among equals.
    for (int i = 1; i < found.size(); ++i) {
        for (int j = i; j > 0 && found.at(j).toMap().value("visits").toInt()
                                  > found.at(j - 1).toMap().value("visits").toInt(); --j)
            found.swap(j, j - 1);
    }
    return found.mid(0, limit);
}

void History::remove(const QString &url)
{
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        if (m_entries.at(i).toMap().value("url").toString() == url)
            m_entries.removeAt(i);
    }
    emit changed();
    m_saveTimer.start();
}

void History::clear()
{
    m_entries.clear();
    emit changed();
    save();
}
