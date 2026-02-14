#pragma once

#include <QAbstractListModel>
#include <QDateTime>

struct LogEntry {
    QDateTime timestamp;
    QString message;
};

class LogModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        TimestampRole = Qt::UserRole + 1,
        MessageRole,
        FormattedRole
    };

    explicit LogModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;

    Q_INVOKABLE void appendLog(const QString &message);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QString exportText() const;

signals:
    void countChanged();

private:
    static constexpr int MAX_ENTRIES = 10000;
    QList<LogEntry> m_entries;
};
