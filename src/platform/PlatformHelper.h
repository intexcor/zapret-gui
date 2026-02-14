#pragma once

#include <QObject>
#include <QProcessEnvironment>
#include <QStringList>
#include "core/StrategyManager.h"

class PlatformHelper : public QObject
{
    Q_OBJECT

public:
    explicit PlatformHelper(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~PlatformHelper() = default;

    // Platform identification
    virtual QString platformName() const = 0;

    // Binary path for the DPI bypass tool (winws/nfqws/tpws)
    virtual QString binaryPath() const = 0;

    // URL to download the binary if it's missing
    virtual QString binaryDownloadUrl() const = 0;

    // Check if binary exists, download if not. Returns true on success.
    bool ensureBinaryExists();

    // Build command-line arguments for the given strategy
    virtual QStringList buildArgs(const Strategy &strategy) const = 0;

    // Firewall / packet redirect setup and teardown
    virtual bool setupFirewall(const Strategy &strategy) = 0;
    virtual bool teardownFirewall() = 0;

    // Service installation (auto-start on boot)
    virtual bool installService(const Strategy &strategy) = 0;
    virtual bool removeService() = 0;

    // Privilege elevation (UAC / pkexec / SMJobBless / VPN permission)
    virtual bool elevatePrivileges() = 0;

    // Environment variables for the child process
    virtual QProcessEnvironment environment() const
    {
        return QProcessEnvironment::systemEnvironment();
    }

    // Factory: creates the right platform helper for the current OS
    static PlatformHelper *create(QObject *parent = nullptr);

signals:
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadStatus(const QString &message);

protected:
    // Utility: resolve file paths relative to the app's resource directory
    QString resourcePath(const QString &relativePath) const;
    QString binDir() const;
    QString listsDir() const;
    QString fakeDir() const;

    // Writable bin directory for downloaded binaries
    QString writableBinDir() const;

private:
    bool downloadFile(const QString &url, const QString &destPath);
};
