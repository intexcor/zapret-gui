#include "AndroidPlatform.h"
#include <QCoreApplication>
#include <QDir>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#endif

AndroidPlatform::AndroidPlatform(QObject *parent)
    : PlatformHelper(parent)
{
}

QString AndroidPlatform::platformName() const { return "android"; }

QString AndroidPlatform::binaryPath() const
{
    // On Android, tpws is bundled as a native library in the APK
    return QCoreApplication::applicationDirPath() + "/libtpws.so";
}

QString AndroidPlatform::binaryDownloadUrl() const
{
    return "https://github.com/Flowseal/zapret-discord-youtube/raw/main/bin/android-arm64/tpws";
}

QStringList AndroidPlatform::buildArgs(const Strategy &strategy) const
{
    QStringList args;

    // tpws in VPN mode — binds to localhost as SOCKS proxy
    args << "--port" << QString::number(m_socksPort);
    args << "--bind-addr=127.0.0.1";

    // Only TCP strategies work with tpws
    for (const auto &filter : strategy.filters) {
        if (filter.protocol == "udp")
            continue;

        if (!filter.hostlist.isEmpty()) {
            // On Android, lists are in app's files directory
            QString dataDir = QCoreApplication::applicationDirPath() + "/../files";
            args << ("--hostlist=" + dataDir + "/" + filter.hostlist);
        }

        // Translate to tpws-compatible options
        QString method = filter.desyncMethod;
        if (method.contains("split") || method.contains("disorder")) {
            args << "--split-pos=1";
            if (method.contains("disorder"))
                args << "--disorder";
        }
    }

    return args;
}

bool AndroidPlatform::setupFirewall(const Strategy & /*strategy*/)
{
    // On Android, traffic routing is handled by VpnService
    // No iptables needed — the VPN TUN interface captures all traffic
#ifdef Q_OS_ANDROID
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");

    if (activity.isValid()) {
        // Call Java method to prepare VPN
        QJniObject::callStaticMethod<void>(
            "com/zapretgui/ZapretVpnService",
            "prepare",
            "(Landroid/content/Context;)V",
            activity.object());
    }
#endif
    return true;
}

bool AndroidPlatform::teardownFirewall()
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(
        "com/zapretgui/ZapretVpnService",
        "stop",
        "()V");
#endif
    return true;
}

bool AndroidPlatform::installService(const Strategy & /*strategy*/)
{
    // On Android, "service" means the VPN stays connected
    // This is handled by the VpnService foreground notification
    return true;
}

bool AndroidPlatform::removeService()
{
    return teardownFirewall();
}

bool AndroidPlatform::elevatePrivileges()
{
    // Android VPN doesn't need root — just VPN permission from user
    return true;
}
