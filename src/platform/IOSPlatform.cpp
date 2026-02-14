#include "IOSPlatform.h"
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

IOSPlatform::IOSPlatform(QObject *parent)
    : PlatformHelper(parent)
{
}

QString IOSPlatform::platformName() const { return "ios"; }

QString IOSPlatform::binaryPath() const
{
    return QCoreApplication::applicationDirPath() + "/tpws";
}

QString IOSPlatform::binaryDownloadUrl() const
{
    // iOS binaries must be bundled — App Store doesn't allow downloading executables
    return {};
}

QStringList IOSPlatform::buildArgs(const Strategy &strategy) const
{
    QStringList args;

    args << "--port" << QString::number(m_socksPort);
    args << "--bind-addr=127.0.0.1";

    // Only TCP strategies, same as macOS
    for (const auto &filter : strategy.filters) {
        if (filter.protocol == "udp")
            continue;

        if (!filter.hostlist.isEmpty()) {
            QString docsDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
            args << ("--hostlist=" + docsDir + "/" + filter.hostlist);
        }

        QString method = filter.desyncMethod;
        if (method.contains("split") || method.contains("disorder")) {
            args << "--split-pos=1";
            if (method.contains("disorder"))
                args << "--disorder";
        }
    }

    return args;
}

bool IOSPlatform::setupFirewall(const Strategy & /*strategy*/)
{
    // iOS uses PacketTunnelProvider — traffic is captured through the VPN tunnel
    // The tunnel is started via NetworkExtension framework from Swift code
    return true;
}

bool IOSPlatform::teardownFirewall()
{
    // Stop the VPN tunnel
    return true;
}

bool IOSPlatform::installService(const Strategy & /*strategy*/)
{
    // On iOS, "service" = VPN on-demand configuration
    // This is configured via NEVPNManager in Swift
    return true;
}

bool IOSPlatform::removeService()
{
    return true;
}

bool IOSPlatform::elevatePrivileges()
{
    // iOS doesn't need root — VPN permission from user
    return true;
}
