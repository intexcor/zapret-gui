#include "MacOSPlatform.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

MacOSPlatform::MacOSPlatform(QObject *parent)
    : PlatformHelper(parent)
{
}

QString MacOSPlatform::platformName() const { return "macos"; }

QString MacOSPlatform::binaryPath() const
{
    // tpws — transparent TCP proxy for DPI bypass on macOS
    QString bundled = binDir() + "/macos/tpws";
    if (QFile::exists(bundled))
        return bundled;

    return writableBinDir() + "/tpws";
}

QString MacOSPlatform::binaryDownloadUrl() const
{
    return "https://github.com/Flowseal/zapret-discord-youtube/raw/main/bin/macos/tpws";
}

QString MacOSPlatform::resolveFilePath(const QString &filename) const
{
    if (QDir::isAbsolutePath(filename))
        return filename;

    // Use world-readable copies in /tmp/zapret so tpws can access them
    // after dropping privileges
    QString tmpPath = "/tmp/zapret/" + filename;
    if (QFile::exists(tmpPath))
        return tmpPath;

    QString listsPath = listsDir() + "/" + filename;
    if (QFile::exists(listsPath))
        return listsPath;

    return filename;
}

static bool copyToTempDir(const QString &srcDir, const QString &destDir)
{
    QDir src(srcDir);
    if (!src.exists())
        return false;

    QDir().mkpath(destDir);

    for (const QString &entry : src.entryList(QDir::Files)) {
        QString srcFile = srcDir + "/" + entry;
        QString destFile = destDir + "/" + entry;
        QFile::remove(destFile); // overwrite if exists
        if (!QFile::copy(srcFile, destFile))
            return false;
        // Make world-readable so tpws can access after dropping privileges
        QFile::setPermissions(destFile,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner |
            QFileDevice::ReadGroup | QFileDevice::ReadOther);
    }
    return true;
}

// Validate port specification: only digits, commas, hyphens allowed.
// Prevents PF rule injection via malicious strategies.json.
static bool isValidPortSpec(const QString &ports)
{
    static QRegularExpression re("^[0-9,\\-]+$");
    return !ports.isEmpty() && re.match(ports).hasMatch();
}

// Validate utun interface name: must be "utunN" where N is digits.
static bool isValidUtunName(const QString &name)
{
    static QRegularExpression re("^utun\\d+$");
    return re.match(name).hasMatch();
}

// Validate unix username for PF rules.
static bool isValidUsername(const QString &name)
{
    static QRegularExpression re("^[a-zA-Z_][a-zA-Z0-9_.\\-]*$");
    return !name.isEmpty() && re.match(name).hasMatch();
}

// XML-escape a string for safe embedding in Apple plist XML.
static QString xmlEscape(const QString &s)
{
    QString r = s;
    r.replace('&', "&amp;");
    r.replace('<', "&lt;");
    r.replace('>', "&gt;");
    r.replace('"', "&quot;");
    return r;
}

QStringList MacOSPlatform::buildFilterArgs(const StrategyFilter &filter) const
{
    QStringList args;

    // tpws is TCP-only, skip UDP filters
    if (filter.protocol == "udp")
        return {};

    // TCP port filter
    if (!filter.ports.isEmpty())
        args << ("--filter-tcp=" + filter.ports);

    if (!filter.l3Filter.isEmpty())
        args << ("--filter-l3=" + filter.l3Filter);
    if (!filter.l7Protocol.isEmpty())
        args << ("--filter-l7=" + filter.l7Protocol);
    if (!filter.hostlist.isEmpty())
        args << ("--hostlist=" + resolveFilePath(filter.hostlist));
    if (!filter.hostlistExclude.isEmpty())
        args << ("--hostlist-exclude=" + resolveFilePath(filter.hostlistExclude));
    if (!filter.hostlistDomains.isEmpty())
        args << ("--hostlist-domains=" + filter.hostlistDomains);
    if (!filter.ipset.isEmpty())
        args << ("--ipset=" + resolveFilePath(filter.ipset));
    if (!filter.ipsetExclude.isEmpty())
        args << ("--ipset-exclude=" + resolveFilePath(filter.ipsetExclude));

    // Translate desync method to tpws options
    QString method = filter.desyncMethod;
    if (method.isEmpty())
        return args;

    // Split position
    if (method.contains("split") || method.contains("disorder")) {
        if (!filter.splitPosStr.isEmpty())
            args << ("--split-pos=" + filter.splitPosStr);
        else if (filter.splitPos > 0)
            args << ("--split-pos=" + QString::number(filter.splitPos));
    }

    // Disorder mode — send second fragment first
    if (method.contains("disorder"))
        args << "--disorder";

    // OOB (out of band) — alternative to fake on tpws
    if (method.contains("oob"))
        args << "--oob";

    // Additional tpws-specific options (--hostcase, --domcase, --tlsrec, etc.)
    for (const QString &opt : filter.tpwsOpts)
        args << opt;

    return args;
}

QStringList MacOSPlatform::buildArgs(const Strategy &strategy) const
{
    QStringList args;

    // tpws listens as a transparent proxy
    args << "--bind-addr=127.0.0.1";
    args << ("--port=" + QString::number(m_proxyPort));

    // Keep tpws running as root so it can use DIOCNATLOOK on /dev/pf
    // for transparent proxy NAT lookups. tpws only binds to 127.0.0.1
    // so this is safe — not exposed to the network.
    args << "--uid" << "0:0";

    // Copy lists to /tmp/zapret/ so they're accessible.
    copyToTempDir(listsDir(), "/tmp/zapret");

    // Build each filter section separated by --new
    bool firstFilter = true;
    for (int i = 0; i < strategy.filters.size(); ++i) {
        QStringList filterArgs = buildFilterArgs(strategy.filters[i]);
        if (filterArgs.isEmpty())
            continue; // Skip UDP filters

        if (!firstFilter)
            args << "--new";
        args << filterArgs;
        firstFilter = false;
    }

    return args;
}

QString MacOSPlatform::udpBypassBinaryPath() const
{
    QString bundled = binDir() + "/macos/udp-bypass";
    if (QFile::exists(bundled))
        return bundled;

    return writableBinDir() + "/udp-bypass";
}

bool MacOSPlatform::strategyHasUdpFilters(const Strategy &strategy) const
{
    for (const auto &filter : strategy.filters) {
        if (filter.protocol == "udp")
            return true;
    }
    return false;
}

QString MacOSPlatform::resolveFakeFilePath(const QString &filename) const
{
    if (QDir::isAbsolutePath(filename))
        return filename;

    // Fake files are copied to /tmp/zapret/ alongside lists
    QString tmpPath = "/tmp/zapret/" + filename;
    if (QFile::exists(tmpPath))
        return tmpPath;

    QString fakePath = fakeDir() + "/" + filename;
    if (QFile::exists(fakePath))
        return fakePath;

    return filename;
}

QStringList MacOSPlatform::buildUdpBypassArgs(const Strategy &strategy) const
{
    QStringList args;

    // Copy fake files to /tmp/zapret/ so udp-bypass can access them
    copyToTempDir(fakeDir(), "/tmp/zapret");

    // Find the first UDP filter with a QUIC fake payload
    for (const auto &filter : strategy.filters) {
        if (filter.protocol != "udp")
            continue;

        if (!filter.fakeQuic.isEmpty()) {
            args << "--fake-quic" << resolveFakeFilePath(filter.fakeQuic);

            if (filter.desyncRepeats > 0)
                args << "--repeats" << QString::number(filter.desyncRepeats);

            break; // Use the first matching UDP filter
        }
    }

    // Always enable verbose for now — helps debugging
    args << "--verbose";

    return args;
}

bool MacOSPlatform::setupFirewall(const Strategy &strategy)
{
    // TCP-only setup (no utun interface needed)
    return setupFirewallWithUtun(strategy, QString());
}

// Convert port list to PF syntax: "19294-19344" → "19294:19344"
static QString toPfPortList(const QString &ports)
{
    QStringList parts = ports.split(',', Qt::SkipEmptyParts);
    for (QString &p : parts)
        p.replace('-', ':');
    return "{ " + parts.join(", ") + " }";
}

bool MacOSPlatform::setupFirewallWithUtun(const Strategy &strategy, const QString &utunIface)
{
    QString currentUser = qEnvironmentVariable("USER");

    // Validate USER to prevent PF rule injection via environment
    if (!isValidUsername(currentUser)) {
        qWarning() << "[PF] Invalid username for PF rules:" << currentUser;
        return false;
    }

    // Validate port specs to prevent PF rule injection via strategies.json
    if (!strategy.tcpPorts.isEmpty() && !isValidPortSpec(strategy.tcpPorts)) {
        qWarning() << "[PF] Invalid TCP port spec:" << strategy.tcpPorts;
        return false;
    }
    if (!strategy.udpPorts.isEmpty() && !isValidPortSpec(strategy.udpPorts)) {
        qWarning() << "[PF] Invalid UDP port spec:" << strategy.udpPorts;
        return false;
    }

    // Validate utun interface name
    if (!utunIface.isEmpty() && !isValidUtunName(utunIface)) {
        qWarning() << "[PF] Invalid utun interface name:" << utunIface;
        return false;
    }

    // Build the complete PF config:
    // 1. Preserve default macOS PF anchors
    // 2. Append our redirect/route rules
    QString pfConf;

    // Default macOS pf.conf structure — must keep Apple anchors working
    pfConf += "scrub-anchor \"com.apple/*\"\n";
    pfConf += "nat-anchor \"com.apple/*\"\n";
    pfConf += "rdr-anchor \"com.apple/*\"\n";

    // TCP rules: tpws transparent proxy via PF rdr + route-to
    if (!strategy.tcpPorts.isEmpty()) {
        QString tcpPortList = toPfPortList(strategy.tcpPorts);

        pfConf += QString("rdr pass on lo0 proto tcp from any to any port %1 -> 127.0.0.1 port %2\n")
                      .arg(tcpPortList)
                      .arg(m_proxyPort);
    }

    pfConf += "anchor \"com.apple/*\"\n";
    pfConf += "load anchor \"com.apple\" from \"/etc/pf.anchors/com.apple\"\n";

    // TCP route-to for tpws
    if (!strategy.tcpPorts.isEmpty()) {
        QString tcpPortList = toPfPortList(strategy.tcpPorts);
        pfConf += QString("pass out route-to lo0 inet proto tcp from any to any port %1 user %2\n")
                      .arg(tcpPortList, currentUser);
    }

    // UDP route-to for udp-bypass
    if (!utunIface.isEmpty() && !strategy.udpPorts.isEmpty()) {
        QString udpPortList = toPfPortList(strategy.udpPorts);

        // Loop prevention: udp-bypass marks its raw socket packets with TOS 0x04.
        // This rule passes them through without route-to redirection.
        // Restricted to root user (udp-bypass) and specific UDP ports for defense-in-depth.
        pfConf += QString("pass out quick inet proto udp from any to any port %1 tos 0x04 user root\n")
                      .arg(udpPortList);

        pfConf += QString("pass out route-to (%1 10.66.0.2) inet proto udp from any to any port %2 user %3 no state\n")
                      .arg(utunIface, udpPortList, currentUser);
    }

    qDebug().noquote() << "[PF] Config:" << pfConf;

    // Save original pf.conf for teardown restoration
    int backupRet = QProcess::execute("/usr/bin/sudo", {"-A", "cp", "/etc/pf.conf", "/tmp/zapret-pf-backup.conf"});
    if (backupRet != 0) {
        qWarning() << "[PF] Failed to backup /etc/pf.conf (exit code:" << backupRet << ")";
        return false;
    }

    // Write our config to a temp file and load it as the main PF config
    {
        QFile tmp("/tmp/zapret-pf.conf");
        if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        tmp.write(pfConf.toUtf8());
        tmp.close();
    }

    // Load the complete PF configuration (not an anchor — so rules are actually evaluated)
    QProcess pfctl;
    pfctl.start("/usr/bin/sudo", {"-A", "pfctl", "-f", "/tmp/zapret-pf.conf"});
    if (pfctl.waitForStarted(15000)) {
        if (!pfctl.waitForFinished(15000) || pfctl.exitCode() != 0) {
            qWarning().noquote() << "[PF] pfctl failed:" << pfctl.readAllStandardError();
            // Restore original on failure
            QProcess::execute("/usr/bin/sudo", {"-A", "pfctl", "-f", "/tmp/zapret-pf-backup.conf"});
            return false;
        }
    } else {
        QProcess::execute("/usr/bin/sudo", {"-A", "pfctl", "-f", "/tmp/zapret-pf-backup.conf"});
        return false;
    }

    // Enable PF
    QProcess::execute("/usr/bin/sudo", {"-A", "pfctl", "-e"});

    // Make /dev/pf world-readable so tpws can use DIOCNATLOOK after dropping
    // privileges to nobody. Restored in teardownFirewall().
    QProcess::execute("/usr/bin/sudo", {"-A", "chmod", "644", "/dev/pf"});

    m_pfConfigured = true;
    return true;
}

bool MacOSPlatform::teardownFirewall()
{
    // Use backup file existence as the authoritative indicator that PF was
    // configured — this works even when called from a fresh MacOSPlatform
    // instance (m_pfConfigured defaults to false on new instances).
    bool hasBackup = QFile::exists("/tmp/zapret-pf-backup.conf");
    if (!hasBackup && !m_pfConfigured)
        return true;

    // Restore original pf.conf
    if (hasBackup) {
        QProcess::execute("/usr/bin/sudo", {"-A", "pfctl", "-f", "/tmp/zapret-pf-backup.conf"});
    } else {
        // Fallback: reload default macOS pf.conf
        QProcess::execute("/usr/bin/sudo", {"-A", "pfctl", "-f", "/etc/pf.conf"});
    }

    // Restore /dev/pf permissions (was set to 644 for tpws DIOCNATLOOK)
    QProcess::execute("/usr/bin/sudo", {"-A", "chmod", "600", "/dev/pf"});

    QFile::remove("/tmp/zapret-pf.conf");
    QFile::remove("/tmp/zapret-pf-backup.conf");

    m_pfConfigured = false;
    return true;
}

bool MacOSPlatform::installService(const Strategy &strategy)
{
    QString binary = binaryPath();
    QStringList args = buildArgs(strategy);

    // Create a launchd plist for tpws
    QString plist = QString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "    <key>Label</key>\n"
        "    <string>com.zapretgui.tpws</string>\n"
        "    <key>ProgramArguments</key>\n"
        "    <array>\n"
        "        <string>%1</string>\n"
    ).arg(xmlEscape(binary));

    for (const QString &arg : args)
        plist += QString("        <string>%1</string>\n").arg(xmlEscape(arg));

    plist += QString(
        "    </array>\n"
        "    <key>RunAtLoad</key>\n"
        "    <true/>\n"
        "    <key>KeepAlive</key>\n"
        "    <true/>\n"
        "</dict>\n"
        "</plist>\n"
    );

    // Write to /Library/LaunchDaemons (needs root)
    QString plistPath = "/Library/LaunchDaemons/com.zapretgui.tpws.plist";
    QFile file(plistPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(plist.toUtf8());
        file.close();
    }

    QProcess::execute("launchctl", {"load", plistPath});
    return true;
}

bool MacOSPlatform::removeService()
{
    QString plistPath = "/Library/LaunchDaemons/com.zapretgui.tpws.plist";
    QProcess::execute("launchctl", {"unload", plistPath});
    QFile::remove(plistPath);
    return true;
}

bool MacOSPlatform::elevatePrivileges()
{
    // tpws needs root for binding to privileged ports and transparent proxying.
    // Elevation handled via sudo -A with askpass helper set up by ZapretEngine.
    return true;
}

bool MacOSPlatform::hasSudoersSetup() const
{
    return QFile::exists("/etc/sudoers.d/zapret");
}

bool MacOSPlatform::setupSudoers()
{
    QString currentUser = qEnvironmentVariable("USER");
    if (!isValidUsername(currentUser)) {
        qWarning() << "[Sudoers] Invalid username:" << currentUser;
        return false;
    }

    QString tpws = binaryPath();
    QString udpBypass = udpBypassBinaryPath();

    // Build sudoers content — one NOPASSWD entry per command used by the app
    QString content;
    content += "# Zapret GUI — passwordless DPI bypass tools\n";
    content += "# Remove: sudo rm /etc/sudoers.d/zapret\n";
    content += QString("%1 ALL=(root) NOPASSWD: %2 *\n").arg(currentUser, tpws);
    content += QString("%1 ALL=(root) NOPASSWD: %2 *\n").arg(currentUser, udpBypass);
    content += QString("%1 ALL=(root) NOPASSWD: /sbin/pfctl *\n").arg(currentUser);
    content += QString("%1 ALL=(root) NOPASSWD: /bin/cp /etc/pf.conf /tmp/zapret-pf-backup.conf\n").arg(currentUser);
    content += QString("%1 ALL=(root) NOPASSWD: /bin/chmod 644 /dev/pf\n").arg(currentUser);
    content += QString("%1 ALL=(root) NOPASSWD: /bin/chmod 600 /dev/pf\n").arg(currentUser);
    content += QString("%1 ALL=(root) NOPASSWD: /bin/kill *\n").arg(currentUser);
    content += QString("%1 ALL=(root) NOPASSWD: /bin/rm /etc/sudoers.d/zapret\n").arg(currentUser);

    // Write to temp file for validation
    QString tmpPath = "/tmp/zapret-sudoers";
    {
        QFile tmp(tmpPath);
        if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning() << "[Sudoers] Cannot write temp file";
            return false;
        }
        tmp.write(content.toUtf8());
        tmp.close();
    }

    // Validate syntax before installing (prevents broken sudoers)
    if (QProcess::execute("/usr/sbin/visudo", {"-c", "-f", tmpPath}) != 0) {
        qWarning() << "[Sudoers] visudo validation failed";
        QFile::remove(tmpPath);
        return false;
    }

    // Install to /etc/sudoers.d/ (this is the ONE password prompt)
    if (QProcess::execute("/usr/bin/sudo", {"-A", "cp", tmpPath, "/etc/sudoers.d/zapret"}) != 0) {
        qWarning() << "[Sudoers] Failed to install sudoers file";
        QFile::remove(tmpPath);
        return false;
    }

    // sudoers files must be 0440
    if (QProcess::execute("/usr/bin/sudo", {"-A", "chmod", "0440", "/etc/sudoers.d/zapret"}) != 0) {
        qWarning() << "[Sudoers] Failed to set permissions";
        QFile::remove(tmpPath);
        return false;
    }

    QFile::remove(tmpPath);
    return true;
}

bool MacOSPlatform::removeSudoers()
{
    if (!hasSudoersSetup())
        return true;
    return QProcess::execute("/usr/bin/sudo", {"-A", "rm", "/etc/sudoers.d/zapret"}) == 0;
}
