#ifndef BERRY_HISTORY_H
#define BERRY_HISTORY_H

#include <QObject>
#include <QTimer>
#include <QVariant>

// Pages visited, newest first: {url, title, visits, time (ms since epoch)}.
// Kept in history.json in the app's data folder, at most kMaxEntries, one
// row per address. Feeds the History sheet and the address bar's
// suggestions.
class History : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)

public:
    explicit History(QObject *parent = 0);
    ~History();

    int count() const { return m_entries.size(); }
    QVariantList entries() const { return m_entries; }

    // A page finished loading. The same address again only updates its title.
    void visit(const QString &url, const QString &title);

    // Entries whose address or title contains every word of text, most
    // visited first.
    QVariantList matches(const QString &text, int limit) const;

    Q_INVOKABLE void remove(const QString &url);
    Q_INVOKABLE void clear();

signals:
    void changed();

private slots:
    void save();

private:
    QString path() const;

    QVariantList m_entries;
    QTimer m_saveTimer;
};

#endif
