#include "HostlistManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

HostlistManager::HostlistManager(QObject *parent)
    : QObject(parent)
{
}

QString HostlistManager::listsDir() const
{
    QString appDir = QCoreApplication::applicationDirPath();

    // macOS bundle: Resources/lists
    QString resources = appDir + "/../Resources/lists";
    if (QDir(resources).exists())
        return resources;

    // Try next to binary
    QString local = appDir + "/lists";
    if (QDir(local).exists())
        return local;

    // Try share directory (Linux installed)
    QString share = appDir + "/../share/zapret-gui/lists";
    if (QDir(share).exists())
        return share;

    // Writable data location
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/lists";
    QDir().mkpath(dataDir);
    return dataDir;
}

QString HostlistManager::readFile(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QTextStream(&file).readAll();
}

void HostlistManager::writeFile(const QString &path, const QString &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("HostlistManager: Cannot write %s", qPrintable(path));
        return;
    }
    QTextStream(&file) << content;
}

void HostlistManager::loadLists()
{
    QString dir = listsDir();
    m_generalList = readFile(dir + "/list-general.txt");
    m_excludeList = readFile(dir + "/list-exclude.txt");
    m_googleList = readFile(dir + "/list-google.txt");
    m_ipsetAll = readFile(dir + "/ipset-all.txt");
    m_ipsetExclude = readFile(dir + "/ipset-exclude.txt");

    emit generalListChanged();
    emit excludeListChanged();
    emit googleListChanged();
    emit ipsetAllChanged();
    emit ipsetExcludeChanged();
}

QString HostlistManager::generalList() const { return m_generalList; }
QString HostlistManager::excludeList() const { return m_excludeList; }
QString HostlistManager::googleList() const { return m_googleList; }
QString HostlistManager::ipsetAll() const { return m_ipsetAll; }
QString HostlistManager::ipsetExclude() const { return m_ipsetExclude; }

void HostlistManager::setGeneralList(const QString &text)
{
    if (m_generalList != text) {
        m_generalList = text;
        emit generalListChanged();
    }
}

void HostlistManager::setExcludeList(const QString &text)
{
    if (m_excludeList != text) {
        m_excludeList = text;
        emit excludeListChanged();
    }
}

void HostlistManager::setGoogleList(const QString &text)
{
    if (m_googleList != text) {
        m_googleList = text;
        emit googleListChanged();
    }
}

void HostlistManager::setIpsetAll(const QString &text)
{
    if (m_ipsetAll != text) {
        m_ipsetAll = text;
        emit ipsetAllChanged();
    }
}

void HostlistManager::setIpsetExclude(const QString &text)
{
    if (m_ipsetExclude != text) {
        m_ipsetExclude = text;
        emit ipsetExcludeChanged();
    }
}

void HostlistManager::save()
{
    QString dir = listsDir();
    writeFile(dir + "/list-general.txt", m_generalList);
    writeFile(dir + "/list-exclude.txt", m_excludeList);
    writeFile(dir + "/list-google.txt", m_googleList);
    writeFile(dir + "/ipset-all.txt", m_ipsetAll);
    writeFile(dir + "/ipset-exclude.txt", m_ipsetExclude);
}

void HostlistManager::addDomain(const QString &listName, const QString &domain)
{
    QString trimmed = domain.trimmed();
    if (trimmed.isEmpty()) return;

    QString *target = nullptr;
    if (listName == "general") target = &m_generalList;
    else if (listName == "exclude") target = &m_excludeList;
    else if (listName == "google") target = &m_googleList;
    else return;

    // Avoid duplicates
    QStringList lines = target->split('\n', Qt::SkipEmptyParts);
    if (lines.contains(trimmed)) return;

    if (!target->isEmpty() && !target->endsWith('\n'))
        target->append('\n');
    target->append(trimmed + '\n');

    if (listName == "general") emit generalListChanged();
    else if (listName == "exclude") emit excludeListChanged();
    else if (listName == "google") emit googleListChanged();
}

void HostlistManager::removeDomain(const QString &listName, const QString &domain)
{
    QString trimmed = domain.trimmed();
    if (trimmed.isEmpty()) return;

    QString *target = nullptr;
    if (listName == "general") target = &m_generalList;
    else if (listName == "exclude") target = &m_excludeList;
    else if (listName == "google") target = &m_googleList;
    else return;

    QStringList lines = target->split('\n', Qt::SkipEmptyParts);
    lines.removeAll(trimmed);
    *target = lines.join('\n');
    if (!target->isEmpty()) target->append('\n');

    if (listName == "general") emit generalListChanged();
    else if (listName == "exclude") emit excludeListChanged();
    else if (listName == "google") emit googleListChanged();
}

QString HostlistManager::listFilePath(const QString &listName) const
{
    QString dir = listsDir();
    if (listName == "general") return dir + "/list-general.txt";
    if (listName == "exclude") return dir + "/list-exclude.txt";
    if (listName == "google") return dir + "/list-google.txt";
    if (listName == "ipset-all") return dir + "/ipset-all.txt";
    if (listName == "ipset-exclude") return dir + "/ipset-exclude.txt";
    return {};
}
