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
    // No external binary needed — DPI bypass is built into the VPN processor JNI lib
    return {};
}

QString AndroidPlatform::binaryDownloadUrl() const
{
    // No binary to download — everything is bundled in libvpn-processor.so
    return {};
}

QStringList AndroidPlatform::buildArgs(const Strategy & /*strategy*/) const
{
    // No-op: tpws is no longer used. Packet processing is done in JNI.
    return {};
}

bool AndroidPlatform::setupFirewall(const Strategy &strategy)
{
#ifdef Q_OS_ANDROID
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative",
        "activity",
        "()Landroid/app/Activity;");

    if (!activity.isValid())
        return false;

    // Extract strategy parameters for the VPN processor
    int fakeTtl = 3;
    int fakeRepeats = 6;
    QString fakeQuicPath;
    int splitPos = 1;
    bool useDisorder = false;

    for (const auto &filter : strategy.filters) {
        if (filter.protocol == "udp") {
            // UDP filter → extract QUIC fake injection params
            if (!filter.fakeQuic.isEmpty()) {
                QString dataDir = QCoreApplication::applicationDirPath() + "/../files/fake";
                fakeQuicPath = dataDir + "/" + filter.fakeQuic;
            }
            if (filter.desyncRepeats > 0)
                fakeRepeats = filter.desyncRepeats;
        } else if (filter.protocol == "tcp") {
            // TCP filter → extract split/disorder params
            if (filter.splitPos > 0)
                splitPos = filter.splitPos;
            if (filter.desyncMethod.contains("disorder"))
                useDisorder = true;
        }
    }

    // Prepare VPN permission
    QJniObject::callStaticMethod<void>(
        "com/zapretgui/ZapretVpnService",
        "prepare",
        "(Landroid/content/Context;)V",
        activity.object());

    // Start VPN service with strategy config
    QJniObject fakePathJni = QJniObject::fromString(fakeQuicPath);

    QJniObject::callStaticMethod<void>(
        "com/zapretgui/ZapretVpnService",
        "start",
        "(Landroid/content/Context;IILjava/lang/String;IZ)V",
        activity.object(),
        (jint)fakeTtl,
        (jint)fakeRepeats,
        fakePathJni.object<jstring>(),
        (jint)splitPos,
        (jboolean)useDisorder);
#else
    Q_UNUSED(strategy);
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
