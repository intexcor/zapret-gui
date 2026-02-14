#pragma once

#include <QObject>
#include <QSettings>
#include <QVariant>

class ConfigManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY autoStartChanged)
    Q_PROPERTY(bool gameFilter READ gameFilter WRITE setGameFilter NOTIFY gameFilterChanged)
    Q_PROPERTY(bool ipsetMode READ ipsetMode WRITE setIpsetMode NOTIFY ipsetModeChanged)
    Q_PROPERTY(bool checkUpdates READ checkUpdates WRITE setCheckUpdates NOTIFY checkUpdatesChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QString lastStrategy READ lastStrategy WRITE setLastStrategy NOTIFY lastStrategyChanged)

public:
    explicit ConfigManager(QObject *parent = nullptr);

    bool autoStart() const;
    void setAutoStart(bool enabled);

    bool gameFilter() const;
    void setGameFilter(bool enabled);

    bool ipsetMode() const;
    void setIpsetMode(bool enabled);

    bool checkUpdates() const;
    void setCheckUpdates(bool enabled);

    QString theme() const;
    void setTheme(const QString &theme);

    QString lastStrategy() const;
    void setLastStrategy(const QString &id);

    Q_INVOKABLE QVariant value(const QString &key, const QVariant &defaultValue = {}) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);

signals:
    void autoStartChanged();
    void gameFilterChanged();
    void ipsetModeChanged();
    void checkUpdatesChanged();
    void themeChanged();
    void lastStrategyChanged();

private:
    QSettings m_settings;
};
