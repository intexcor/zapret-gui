#include "UpdateChecker.h"
#include <QNetworkReply>
#include <QVersionNumber>

static const char *VERSION_URL =
    "https://raw.githubusercontent.com/Flowseal/zapret-discord-youtube/main/.service/version.txt";

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
{
}

bool UpdateChecker::isChecking() const { return m_checking; }
bool UpdateChecker::isUpdateAvailable() const { return m_updateAvailable; }
QString UpdateChecker::latestVersion() const { return m_latestVersion; }

QString UpdateChecker::currentVersion() const
{
    return QStringLiteral(APP_VERSION);
}

void UpdateChecker::check()
{
    if (m_checking) return;

    m_checking = true;
    emit checkingChanged();

    QNetworkRequest req(QUrl(QString::fromLatin1(VERSION_URL)));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_checking = false;
        emit checkingChanged();

        if (reply->error() != QNetworkReply::NoError) {
            emit checkFailed(reply->errorString());
            return;
        }

        QString remote = QString::fromUtf8(reply->readAll()).trimmed();
        m_latestVersion = remote;
        emit latestVersionChanged();

        QVersionNumber current = QVersionNumber::fromString(currentVersion());
        QVersionNumber latest = QVersionNumber::fromString(remote);

        bool hasUpdate = latest > current;
        if (m_updateAvailable != hasUpdate) {
            m_updateAvailable = hasUpdate;
            emit updateAvailableChanged();
        }

        emit checkFinished(hasUpdate);
    });
}
