#include "IOSPlatform.h"
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QSettings>

IOSPlatform::IOSPlatform(QObject *parent)
    : PlatformHelper(parent)
{
}

QString IOSPlatform::platformName() const { return "ios"; }

QString IOSPlatform::binaryPath() const
{
    // No external binary needed — DPI bypass is built into PacketProcessor
    return {};
}

QString IOSPlatform::binaryDownloadUrl() const
{
    // iOS binaries must be bundled — App Store doesn't allow downloading executables
    return {};
}

QStringList IOSPlatform::buildArgs(const Strategy & /*strategy*/) const
{
    // No-op: tpws is no longer used. Packet processing is done in Swift.
    return {};
}

bool IOSPlatform::setupFirewall(const Strategy &strategy)
{
    // Write strategy config to shared app group UserDefaults
    // The PacketTunnelProvider reads these settings on start/restart
    QSettings settings(QStringLiteral("group.com.zapretgui"),
                       QSettings::NativeFormat);

    int splitPos = 1;
    bool useDisorder = false;
    int fakeTtl = 3;
    int fakeRepeats = 6;
    QString fakeQuicFile;

    for (const auto &filter : strategy.filters) {
        if (filter.protocol == "udp") {
            if (!filter.fakeQuic.isEmpty())
                fakeQuicFile = filter.fakeQuic;
            if (filter.desyncRepeats > 0)
                fakeRepeats = filter.desyncRepeats;
        } else if (filter.protocol == "tcp") {
            if (filter.splitPos > 0)
                splitPos = filter.splitPos;
            if (filter.desyncMethod.contains("disorder"))
                useDisorder = true;
        }
    }

    settings.setValue(QStringLiteral("splitPos"), splitPos);
    settings.setValue(QStringLiteral("useDisorder"), useDisorder);
    settings.setValue(QStringLiteral("fakeTTL"), fakeTtl);
    settings.setValue(QStringLiteral("fakeRepeats"), fakeRepeats);
    settings.setValue(QStringLiteral("fakeQuicFile"), fakeQuicFile);
    settings.sync();

    // The tunnel is started via NetworkExtension framework from Swift code
    return true;
}

bool IOSPlatform::teardownFirewall()
{
    // Stop the VPN tunnel — handled by Swift side
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
