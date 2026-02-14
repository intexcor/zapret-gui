#include "WindowsPlatform.h"
#include <QCoreApplication>
#include <QDir>
#include <QProcess>

WindowsPlatform::WindowsPlatform(QObject *parent)
    : PlatformHelper(parent)
{
}

QString WindowsPlatform::platformName() const { return "windows"; }

QString WindowsPlatform::binaryPath() const
{
    return binDir() + "/windows/winws.exe";
}

QString WindowsPlatform::binaryDownloadUrl() const
{
    // winws.exe is bundled — no download needed
    return {};
}

QString WindowsPlatform::resolveFilePath(const QString &filename) const
{
    // Check if it's an absolute path already
    if (QDir::isAbsolutePath(filename))
        return filename;

    // Check in bin directory (for fake packet files)
    QString binPath = binDir() + "/windows/" + filename;
    if (QFile::exists(binPath))
        return binPath;

    // Check in fake directory
    QString fakePath = fakeDir() + "/" + filename;
    if (QFile::exists(fakePath))
        return fakePath;

    // Check in lists directory
    QString listsPath = listsDir() + "/" + filename;
    if (QFile::exists(listsPath))
        return listsPath;

    return filename;
}

QStringList WindowsPlatform::buildFilterArgs(const StrategyFilter &filter) const
{
    QStringList args;

    // Protocol and port filter
    if (filter.protocol == "udp") {
        args << ("--filter-udp=" + filter.ports);
    } else {
        args << ("--filter-tcp=" + filter.ports);
    }

    // L3 and L7 filters
    if (!filter.l3Filter.isEmpty())
        args << ("--filter-l3=" + filter.l3Filter);
    if (!filter.l7Protocol.isEmpty())
        args << ("--filter-l7=" + filter.l7Protocol);

    // Host/IP lists
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

    // IP ID
    if (filter.ipIdZero)
        args << "--ip-id=zero";

    // Desync method
    if (!filter.desyncMethod.isEmpty())
        args << ("--dpi-desync=" + filter.desyncMethod);

    // Desync parameters
    if (filter.desyncRepeats > 0)
        args << ("--dpi-desync-repeats=" + QString::number(filter.desyncRepeats));

    if (filter.splitSeqovl > 0)
        args << ("--dpi-desync-split-seqovl=" + QString::number(filter.splitSeqovl));

    if (filter.splitPos > 0) {
        if (!filter.splitPosStr.isEmpty())
            args << ("--dpi-desync-split-pos=" + filter.splitPosStr);
        else
            args << ("--dpi-desync-split-pos=" + QString::number(filter.splitPos));
    } else if (!filter.splitPosStr.isEmpty()) {
        args << ("--dpi-desync-split-pos=" + filter.splitPosStr);
    }

    if (!filter.splitSeqovlPattern.isEmpty())
        args << ("--dpi-desync-split-seqovl-pattern=" + resolveFilePath(filter.splitSeqovlPattern));

    // Fake packets
    if (!filter.fakeQuic.isEmpty())
        args << ("--dpi-desync-fake-quic=" + resolveFilePath(filter.fakeQuic));
    if (!filter.fakeTls.isEmpty())
        args << ("--dpi-desync-fake-tls=" + resolveFilePath(filter.fakeTls));
    if (!filter.fakeTlsMod.isEmpty())
        args << ("--dpi-desync-fake-tls-mod=" + filter.fakeTlsMod);
    if (!filter.fakeUnknownUdp.isEmpty())
        args << ("--dpi-desync-fake-unknown-udp=" + resolveFilePath(filter.fakeUnknownUdp));

    // Fooling
    if (!filter.fooling.isEmpty())
        args << ("--dpi-desync-fooling=" + filter.fooling);
    if (filter.badseqIncrement > 0)
        args << ("--dpi-desync-badseq-increment=" + QString::number(filter.badseqIncrement));

    // Cutoff
    if (!filter.desyncCutoff.isEmpty())
        args << ("--dpi-desync-cutoff=" + filter.desyncCutoff);

    // Any protocol flag
    if (filter.anyProtocol)
        args << "--dpi-desync-any-protocol=1";

    return args;
}

QStringList WindowsPlatform::buildArgs(const Strategy &strategy) const
{
    QStringList args;

    // WinDivert capture filters
    if (!strategy.tcpPorts.isEmpty())
        args << ("--wf-tcp=" + strategy.tcpPorts);
    if (!strategy.udpPorts.isEmpty())
        args << ("--wf-udp=" + strategy.udpPorts);

    // Build each filter section separated by --new
    for (int i = 0; i < strategy.filters.size(); ++i) {
        if (i > 0)
            args << "--new";
        args << buildFilterArgs(strategy.filters[i]);
    }

    return args;
}

bool WindowsPlatform::setupFirewall(const Strategy & /*strategy*/)
{
    // WinDivert is loaded by winws.exe itself — no external firewall setup needed
    return true;
}

bool WindowsPlatform::teardownFirewall()
{
    // WinDivert is unloaded when winws.exe stops
    return true;
}

bool WindowsPlatform::installService(const Strategy &strategy)
{
    QString binary = binaryPath();
    QStringList args = buildArgs(strategy);
    QString cmdLine = "\"" + binary + "\" " + args.join(' ');

    // Use sc.exe to create a Windows service
    QProcess sc;
    sc.start("sc", {"create", "zapret",
                    "binPath=", cmdLine,
                    "DisplayName=", "Zapret DPI Bypass",
                    "start=", "auto"});
    sc.waitForFinished(10000);

    if (sc.exitCode() != 0) {
        qWarning("Failed to create service: %s", sc.readAllStandardError().constData());
        return false;
    }

    // Start the service
    QProcess scStart;
    scStart.start("sc", {"start", "zapret"});
    scStart.waitForFinished(10000);

    return scStart.exitCode() == 0;
}

bool WindowsPlatform::removeService()
{
    // Stop and delete the service
    QProcess scStop;
    scStop.start("sc", {"stop", "zapret"});
    scStop.waitForFinished(10000);

    QProcess scDelete;
    scDelete.start("sc", {"delete", "zapret"});
    scDelete.waitForFinished(10000);

    return scDelete.exitCode() == 0;
}

bool WindowsPlatform::elevatePrivileges()
{
    // On Windows, the app should be started with admin rights via UAC manifest
    // or the user runs it as administrator. WinDivert requires admin.
    // The manifest file requests elevation at launch.
    // If we need runtime elevation, we'd use ShellExecuteEx with "runas".
    return true;
}
