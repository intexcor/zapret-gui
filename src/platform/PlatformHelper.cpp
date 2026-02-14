#include "PlatformHelper.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>

#if defined(PLATFORM_WINDOWS)
#include "WindowsPlatform.h"
#elif defined(PLATFORM_MACOS)
#include "MacOSPlatform.h"
#elif defined(PLATFORM_ANDROID)
#include "AndroidPlatform.h"
#elif defined(PLATFORM_IOS)
#include "IOSPlatform.h"
#else
#include "LinuxPlatform.h"
#endif

PlatformHelper *PlatformHelper::create(QObject *parent)
{
#if defined(PLATFORM_WINDOWS)
    return new WindowsPlatform(parent);
#elif defined(PLATFORM_MACOS)
    return new MacOSPlatform(parent);
#elif defined(PLATFORM_ANDROID)
    return new AndroidPlatform(parent);
#elif defined(PLATFORM_IOS)
    return new IOSPlatform(parent);
#else
    return new LinuxPlatform(parent);
#endif
}

bool PlatformHelper::ensureBinaryExists()
{
    QString path = binaryPath();
    QFileInfo fi(path);

    if (fi.exists() && fi.isExecutable())
        return true;

    // Binary not found — try to download
    QString url = binaryDownloadUrl();
    if (url.isEmpty()) {
        qWarning("No download URL for binary on this platform");
        return false;
    }

    emit downloadStatus("Downloading " + fi.fileName() + "...");

    // Make sure the directory exists
    QDir().mkpath(fi.absolutePath());

    if (!downloadFile(url, path)) {
        return false;
    }

    // Set executable permission
    QFile file(path);
    file.setPermissions(file.permissions()
                        | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeUser);

    emit downloadStatus("Download complete");
    return fi.exists();
}

bool PlatformHelper::downloadFile(const QString &url, const QString &destPath)
{
    QNetworkAccessManager nam;
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = nam.get(request);

    // Synchronous wait with event loop
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    // Timeout after 60 seconds
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(60000);

    // Progress reporting
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                emit downloadProgress(received, total);
                if (total > 0) {
                    int pct = static_cast<int>(received * 100 / total);
                    emit downloadStatus(QString("Downloading... %1%").arg(pct));
                }
            });

    loop.exec();

    if (!timeout.isActive()) {
        // Timed out
        reply->abort();
        reply->deleteLater();
        emit downloadStatus("Download timed out");
        return false;
    }

    if (reply->error() != QNetworkReply::NoError) {
        qWarning("Download failed: %s", qPrintable(reply->errorString()));
        emit downloadStatus("Download failed: " + reply->errorString());
        reply->deleteLater();
        return false;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    if (data.isEmpty()) {
        emit downloadStatus("Downloaded empty file");
        return false;
    }

    QFile file(destPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("Cannot write to %s", qPrintable(destPath));
        emit downloadStatus("Cannot write file: " + destPath);
        return false;
    }

    file.write(data);
    file.close();

    qInfo("Downloaded %s (%lld bytes)", qPrintable(destPath), data.size());
    return true;
}

QString PlatformHelper::resourcePath(const QString &relativePath) const
{
    QString appDir = QCoreApplication::applicationDirPath();

    // macOS bundle: Resources/
    QString resources = appDir + "/../Resources/" + relativePath;
    if (QFile::exists(resources) || QDir(resources).exists())
        return resources;

    // Try local (next to binary)
    QString local = appDir + "/" + relativePath;
    if (QFile::exists(local) || QDir(local).exists())
        return local;

    // Try installed share path (Linux)
    QString share = appDir + "/../share/zapret-gui/" + relativePath;
    if (QFile::exists(share) || QDir(share).exists())
        return share;

    // Writable data location
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                       + "/" + relativePath;
    if (QFile::exists(dataPath) || QDir(dataPath).exists())
        return dataPath;

    return local;
}

QString PlatformHelper::binDir() const
{
    return resourcePath("bin");
}

QString PlatformHelper::listsDir() const
{
    return resourcePath("lists");
}

QString PlatformHelper::fakeDir() const
{
    return resourcePath("fake");
}

QString PlatformHelper::writableBinDir() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/bin";
    QDir().mkpath(dir);
    return dir;
}
