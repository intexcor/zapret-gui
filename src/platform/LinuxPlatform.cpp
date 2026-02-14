#include "LinuxPlatform.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSysInfo>

LinuxPlatform::LinuxPlatform(QObject *parent)
    : PlatformHelper(parent)
{
}

QString LinuxPlatform::platformName() const { return "linux"; }

QString LinuxPlatform::binaryPath() const
{
    QString arch = QSysInfo::currentCpuArchitecture();
    QString subdir = (arch.contains("arm") || arch.contains("aarch64"))
                         ? "linux-aarch64" : "linux-x86_64";

    QString bundled = binDir() + "/" + subdir + "/nfqws";
    if (QFile::exists(bundled))
        return bundled;

    return writableBinDir() + "/nfqws";
}

QString LinuxPlatform::binaryDownloadUrl() const
{
    QString arch = QSysInfo::currentCpuArchitecture();
    if (arch.contains("arm") || arch.contains("aarch64"))
        return "https://github.com/Flowseal/zapret-discord-youtube/raw/main/bin/linux-aarch64/nfqws";
    return "https://github.com/Flowseal/zapret-discord-youtube/raw/main/bin/linux-x86_64/nfqws";
}

QString LinuxPlatform::resolveFilePath(const QString &filename) const
{
    if (QDir::isAbsolutePath(filename))
        return filename;

    QString fakePath = fakeDir() + "/" + filename;
    if (QFile::exists(fakePath))
        return fakePath;

    QString listsPath = listsDir() + "/" + filename;
    if (QFile::exists(listsPath))
        return listsPath;

    return filename;
}

QStringList LinuxPlatform::buildFilterArgs(const StrategyFilter &filter) const
{
    QStringList args;

    // nfqws uses same filter syntax as winws for most options
    if (filter.protocol == "udp")
        args << ("--filter-udp=" + filter.ports);
    else
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
    if (filter.ipIdZero)
        args << "--ip-id=zero";
    if (!filter.desyncMethod.isEmpty())
        args << ("--dpi-desync=" + filter.desyncMethod);
    if (filter.desyncRepeats > 0)
        args << ("--dpi-desync-repeats=" + QString::number(filter.desyncRepeats));
    if (filter.splitSeqovl > 0)
        args << ("--dpi-desync-split-seqovl=" + QString::number(filter.splitSeqovl));
    if (!filter.splitPosStr.isEmpty())
        args << ("--dpi-desync-split-pos=" + filter.splitPosStr);
    else if (filter.splitPos > 0)
        args << ("--dpi-desync-split-pos=" + QString::number(filter.splitPos));
    if (!filter.splitSeqovlPattern.isEmpty())
        args << ("--dpi-desync-split-seqovl-pattern=" + resolveFilePath(filter.splitSeqovlPattern));
    if (!filter.fakeQuic.isEmpty())
        args << ("--dpi-desync-fake-quic=" + resolveFilePath(filter.fakeQuic));
    if (!filter.fakeTls.isEmpty())
        args << ("--dpi-desync-fake-tls=" + resolveFilePath(filter.fakeTls));
    if (!filter.fakeTlsMod.isEmpty())
        args << ("--dpi-desync-fake-tls-mod=" + filter.fakeTlsMod);
    if (!filter.fakeUnknownUdp.isEmpty())
        args << ("--dpi-desync-fake-unknown-udp=" + resolveFilePath(filter.fakeUnknownUdp));
    if (!filter.fooling.isEmpty())
        args << ("--dpi-desync-fooling=" + filter.fooling);
    if (filter.badseqIncrement > 0)
        args << ("--dpi-desync-badseq-increment=" + QString::number(filter.badseqIncrement));
    if (!filter.desyncCutoff.isEmpty())
        args << ("--dpi-desync-cutoff=" + filter.desyncCutoff);
    if (filter.anyProtocol)
        args << "--dpi-desync-any-protocol=1";

    return args;
}

QStringList LinuxPlatform::buildArgs(const Strategy &strategy) const
{
    QStringList args;
    args << ("--qnum=" + QString::number(m_nfqueueNum));

    for (int i = 0; i < strategy.filters.size(); ++i) {
        if (i > 0)
            args << "--new";
        args << buildFilterArgs(strategy.filters[i]);
    }

    return args;
}

bool LinuxPlatform::setupFirewall(const Strategy &strategy)
{
    // Parse TCP and UDP ports from strategy
    QStringList tcpPorts = strategy.tcpPorts.split(',', Qt::SkipEmptyParts);
    QStringList udpPorts = strategy.udpPorts.split(',', Qt::SkipEmptyParts);

    // Add iptables NFQUEUE rules for TCP
    for (const QString &port : tcpPorts) {
        QProcess::execute("iptables",
                          {"-t", "mangle", "-A", "POSTROUTING",
                           "-p", "tcp", "--dport", port.trimmed(),
                           "-j", "NFQUEUE", "--queue-num", QString::number(m_nfqueueNum)});
    }

    // Add iptables NFQUEUE rules for UDP
    for (const QString &port : udpPorts) {
        QProcess::execute("iptables",
                          {"-t", "mangle", "-A", "POSTROUTING",
                           "-p", "udp", "--dport", port.trimmed(),
                           "-j", "NFQUEUE", "--queue-num", QString::number(m_nfqueueNum)});
    }

    m_firewallConfigured = true;
    return true;
}

bool LinuxPlatform::teardownFirewall()
{
    if (!m_firewallConfigured) return true;

    // Flush the POSTROUTING chain entries we added
    QProcess::execute("iptables",
                      {"-t", "mangle", "-D", "POSTROUTING",
                       "-j", "NFQUEUE", "--queue-num", QString::number(m_nfqueueNum)});

    m_firewallConfigured = false;
    return true;
}

bool LinuxPlatform::installService(const Strategy &strategy)
{
    // Create a systemd service unit
    QString binary = binaryPath();
    QStringList args = buildArgs(strategy);

    QString unit = QString(
        "[Unit]\n"
        "Description=Zapret DPI Bypass\n"
        "After=network.target\n"
        "\n"
        "[Service]\n"
        "Type=simple\n"
        "ExecStart=%1 %2\n"
        "Restart=on-failure\n"
        "RestartSec=5\n"
        "\n"
        "[Install]\n"
        "WantedBy=multi-user.target\n"
    ).arg(binary, args.join(' '));

    // Write unit file via pkexec tee
    QProcess tee;
    tee.start("pkexec", {"tee", "/etc/systemd/system/zapret.service"});
    if (tee.waitForStarted(5000)) {
        tee.write(unit.toUtf8());
        tee.closeWriteChannel();
        tee.waitForFinished(10000);
    }

    // Enable and start
    QProcess::execute("pkexec", {"systemctl", "daemon-reload"});
    QProcess::execute("pkexec", {"systemctl", "enable", "zapret"});
    QProcess::execute("pkexec", {"systemctl", "start", "zapret"});

    return true;
}

bool LinuxPlatform::removeService()
{
    QProcess::execute("pkexec", {"systemctl", "stop", "zapret"});
    QProcess::execute("pkexec", {"systemctl", "disable", "zapret"});
    QProcess::execute("pkexec", {"rm", "/etc/systemd/system/zapret.service"});
    QProcess::execute("pkexec", {"systemctl", "daemon-reload"});
    return true;
}

bool LinuxPlatform::elevatePrivileges()
{
    // On Linux, nfqws and iptables require root.
    // The process is launched via pkexec or the app is run as root.
    // We check if we're root; if not, we need to re-launch via pkexec.
    return (geteuid() == 0);
}
