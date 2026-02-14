#pragma once

#include <QObject>
#include <QNetworkAccessManager>

class UpdateChecker : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool checking READ isChecking NOTIFY checkingChanged)
    Q_PROPERTY(bool updateAvailable READ isUpdateAvailable NOTIFY updateAvailableChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY latestVersionChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)

public:
    explicit UpdateChecker(QObject *parent = nullptr);

    bool isChecking() const;
    bool isUpdateAvailable() const;
    QString latestVersion() const;
    QString currentVersion() const;

    Q_INVOKABLE void check();

signals:
    void checkingChanged();
    void updateAvailableChanged();
    void latestVersionChanged();
    void checkFinished(bool hasUpdate);
    void checkFailed(const QString &error);

private:
    QNetworkAccessManager m_nam;
    bool m_checking = false;
    bool m_updateAvailable = false;
    QString m_latestVersion;
};
