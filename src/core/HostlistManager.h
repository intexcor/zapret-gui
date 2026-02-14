#pragma once

#include <QObject>
#include <QStringList>

class HostlistManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString generalList READ generalList WRITE setGeneralList NOTIFY generalListChanged)
    Q_PROPERTY(QString excludeList READ excludeList WRITE setExcludeList NOTIFY excludeListChanged)
    Q_PROPERTY(QString googleList READ googleList WRITE setGoogleList NOTIFY googleListChanged)
    Q_PROPERTY(QString ipsetAll READ ipsetAll WRITE setIpsetAll NOTIFY ipsetAllChanged)
    Q_PROPERTY(QString ipsetExclude READ ipsetExclude WRITE setIpsetExclude NOTIFY ipsetExcludeChanged)

public:
    explicit HostlistManager(QObject *parent = nullptr);

    void loadLists();

    QString generalList() const;
    void setGeneralList(const QString &text);

    QString excludeList() const;
    void setExcludeList(const QString &text);

    QString googleList() const;
    void setGoogleList(const QString &text);

    QString ipsetAll() const;
    void setIpsetAll(const QString &text);

    QString ipsetExclude() const;
    void setIpsetExclude(const QString &text);

    Q_INVOKABLE void save();
    Q_INVOKABLE void addDomain(const QString &listName, const QString &domain);
    Q_INVOKABLE void removeDomain(const QString &listName, const QString &domain);

    // Returns the absolute path to a list file (for command line args)
    QString listFilePath(const QString &listName) const;

signals:
    void generalListChanged();
    void excludeListChanged();
    void googleListChanged();
    void ipsetAllChanged();
    void ipsetExcludeChanged();

private:
    QString listsDir() const;
    QString readFile(const QString &path) const;
    void writeFile(const QString &path, const QString &content);

    QString m_generalList;
    QString m_excludeList;
    QString m_googleList;
    QString m_ipsetAll;
    QString m_ipsetExclude;
};
