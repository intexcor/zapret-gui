#include "LogModel.h"

LogModel::LogModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LogModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_entries.size();
}

QVariant LogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};

    const auto &entry = m_entries[index.row()];

    switch (role) {
    case TimestampRole:
        return entry.timestamp;
    case MessageRole:
        return entry.message;
    case FormattedRole:
        return entry.timestamp.toString("[hh:mm:ss] ") + entry.message;
    }

    return {};
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    return {
        {TimestampRole, "timestamp"},
        {MessageRole, "message"},
        {FormattedRole, "formatted"},
    };
}

int LogModel::count() const
{
    return m_entries.size();
}

void LogModel::appendLog(const QString &message)
{
    // Trim old entries if we exceed max
    if (m_entries.size() >= MAX_ENTRIES) {
        beginRemoveRows({}, 0, 0);
        m_entries.removeFirst();
        endRemoveRows();
    }

    int row = m_entries.size();
    beginInsertRows({}, row, row);
    m_entries.append({QDateTime::currentDateTime(), message});
    endInsertRows();

    emit countChanged();
}

void LogModel::clear()
{
    if (m_entries.isEmpty()) return;
    beginResetModel();
    m_entries.clear();
    endResetModel();
    emit countChanged();
}

QString LogModel::exportText() const
{
    QString text;
    for (const auto &entry : m_entries) {
        text += entry.timestamp.toString("[yyyy-MM-dd hh:mm:ss] ") + entry.message + '\n';
    }
    return text;
}
