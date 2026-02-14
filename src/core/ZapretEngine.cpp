#include "ZapretEngine.h"
#include "StrategyManager.h"
#include "HostlistManager.h"
#include "models/LogModel.h"
#include "platform/PlatformHelper.h"
#ifdef PLATFORM_MACOS
#include "platform/MacOSPlatform.h"
#endif
#include <QDir>
#include <QFile>
#include <QEventLoop>
#include <QRegularExpression>
#include <QTimer>

ZapretEngine::ZapretEngine(StrategyManager *strategyMgr,
                           HostlistManager *hostlistMgr,
                           LogModel *logModel,
                           QObject *parent)
    : QObject(parent)
    , m_strategyManager(strategyMgr)
    , m_hostlistManager(hostlistMgr)
    , m_logModel(logModel)
    , m_processManager(new ProcessManager(this))
    , m_udpProcessManager(new ProcessManager(this))
{
    connect(m_processManager, &ProcessManager::started, this, &ZapretEngine::onProcessStarted);
    connect(m_processManager, &ProcessManager::stopped, this, &ZapretEngine::onProcessStopped);
    connect(m_processManager, &ProcessManager::outputLine, this, &ZapretEngine::onProcessOutput);
    connect(m_processManager, &ProcessManager::errorOccurred, this, &ZapretEngine::onProcessError);

    connect(m_udpProcessManager, &ProcessManager::outputLine, this, &ZapretEngine::onUdpProcessOutput);
    connect(m_udpProcessManager, &ProcessManager::errorOccurred, this, [this](const QString &err) {
        m_logModel->appendLog("[udp-bypass] Error: " + err);
    });
    connect(m_udpProcessManager, &ProcessManager::stopped, this, [this](int exitCode) {
        m_logModel->appendLog(QString("[udp-bypass] Stopped (exit code: %1)").arg(exitCode));
    });
}

ZapretEngine::~ZapretEngine()
{
    if (m_running)
        stop();
}

bool ZapretEngine::isRunning() const { return m_running; }
QString ZapretEngine::status() const { return m_status; }
QString ZapretEngine::currentStrategyId() const { return m_currentStrategyId; }
QString ZapretEngine::errorString() const { return m_errorString; }

void ZapretEngine::setCurrentStrategyId(const QString &id)
{
    if (m_currentStrategyId != id) {
        m_currentStrategyId = id;
        emit currentStrategyIdChanged();
    }
}

void ZapretEngine::start()
{
    if (m_running) {
        stop();
        return;
    }

    setError({});

    if (m_currentStrategyId.isEmpty()) {
        setError("No strategy selected");
        return;
    }

    Strategy strategy = m_strategyManager->strategyById(m_currentStrategyId);
    if (strategy.id.isEmpty()) {
        setError("Strategy not found: " + m_currentStrategyId);
        return;
    }

    auto *platform = PlatformHelper::create(this);
    if (!platform) {
        setError("Unsupported platform");
        return;
    }

    // Check platform support
    if (!strategy.supportedPlatforms.contains(platform->platformName())) {
        setError(QString("Strategy '%1' is not supported on %2")
                     .arg(strategy.name, platform->platformName()));
        delete platform;
        return;
    }

    setStatus("Starting...");
    m_logModel->appendLog("[Engine] Starting with strategy: " + strategy.name);

    // Clean up stale state from a previous crash (PF rules left behind)
    if (QFile::exists("/tmp/zapret-pf-backup.conf")) {
        m_logModel->appendLog("[Engine] Cleaning up stale PF config from previous run");
        platform->teardownFirewall();
    }

    // Log download progress
    connect(platform, &PlatformHelper::downloadStatus, this, [this](const QString &msg) {
        m_logModel->appendLog("[Download] " + msg);
        setStatus(msg);
    });

    // Ensure binary exists (download if needed)
    if (!platform->ensureBinaryExists()) {
        setError("Binary not available. Check your internet connection.");
        m_logModel->appendLog("[Engine] Binary not found and download failed");
        setStatus("Stopped");
        delete platform;
        return;
    }

    // Elevate privileges if needed
    if (!platform->elevatePrivileges()) {
        setError("Failed to obtain required privileges");
        m_logModel->appendLog("[Engine] Privilege elevation failed");
        setStatus("Stopped");
        delete platform;
        return;
    }

#if defined(PLATFORM_MACOS) || defined(PLATFORM_LINUX)
    // Set up askpass helper BEFORE firewall setup — both pfctl and dvtws2 need sudo
    {
        QString askpassPath = QDir::tempPath() + "/zapret-askpass.sh";
        QFile askpass(askpassPath);
        if (askpass.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
#if defined(PLATFORM_MACOS)
            askpass.write("#!/bin/bash\n"
                "osascript -e 'Tell application \"System Events\" to display dialog "
                "\"Zapret needs administrator privileges.\" "
                "default answer \"\" with hidden answer "
                "buttons {\"Cancel\",\"OK\"} default button \"OK\" "
                "with title \"Zapret\"' "
                "-e 'text returned of result' 2>/dev/null\n");
#else
            // Linux: try zenity, then kdialog, then terminal prompt
            askpass.write("#!/bin/bash\n"
                "if command -v zenity &>/dev/null; then\n"
                "  zenity --password --title='Zapret' 2>/dev/null\n"
                "elif command -v kdialog &>/dev/null; then\n"
                "  kdialog --password 'Zapret needs administrator privileges.' 2>/dev/null\n"
                "fi\n");
#endif
            askpass.close();
            askpass.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        }
        // Set for current process so all child sudo calls inherit it
        qputenv("SUDO_ASKPASS", askpassPath.toUtf8());
    }
#endif

#if defined(PLATFORM_MACOS)
    {
        auto *macPlatform = qobject_cast<MacOSPlatform *>(platform);

        // One-time sudoers setup: installs NOPASSWD entries so future
        // sudo calls skip the password dialog entirely.
        if (macPlatform && !macPlatform->hasSudoersSetup()) {
            m_logModel->appendLog("[Engine] Setting up passwordless sudo (one-time)...");
            if (macPlatform->setupSudoers())
                m_logModel->appendLog("[Engine] Done — no more password prompts");
            else
                m_logModel->appendLog("[Engine] Sudoers setup failed — using password prompt");
        }

        bool hasUdp = macPlatform && macPlatform->strategyHasUdpFilters(strategy);

        QProcessEnvironment env = platform->environment();
        env.insert("SUDO_ASKPASS", qgetenv("SUDO_ASKPASS"));

        if (hasUdp) {
            // Start udp-bypass first to get the utun interface name
            QString udpBinary = macPlatform->udpBypassBinaryPath();
            if (!QFile::exists(udpBinary)) {
                setError("udp-bypass binary not found: " + udpBinary);
                m_logModel->appendLog("[Engine] udp-bypass binary not found");
                setStatus("Stopped");
                delete platform;
                return;
            }

            QStringList udpArgs = macPlatform->buildUdpBypassArgs(strategy);

            m_logModel->appendLog("[Engine] Starting udp-bypass: " + udpBinary);
            m_logModel->appendLog("[Engine] udp-bypass args: " + udpArgs.join(' '));

            // Start udp-bypass via sudo
            QStringList sudoUdpArgs;
            sudoUdpArgs << "-A" << udpBinary << udpArgs;

            m_utunInterface.clear();
            m_udpProcessManager->start("/usr/bin/sudo", sudoUdpArgs, env);

            // Wait for UTUN:<ifname> output (up to 5 seconds)
            QEventLoop loop;
            QTimer timeout;
            timeout.setSingleShot(true);
            bool processDied = false;

            auto conn = connect(this, &ZapretEngine::logMessage, &loop, [&](const QString &line) {
                if (line.startsWith("UTUN:")) {
                    QString iface = line.mid(5).trimmed();
                    // Validate interface name to prevent PF rule injection
                    static QRegularExpression utunRe("^utun\\d+$");
                    if (utunRe.match(iface).hasMatch())
                        m_utunInterface = iface;
                    loop.quit();
                }
            });
            // Quit early if udp-bypass crashes before producing UTUN line
            auto connDied = connect(m_udpProcessManager, &ProcessManager::stopped, &loop, [&](int) {
                processDied = true;
                loop.quit();
            });
            connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
            timeout.start(5000);
            loop.exec();
            disconnect(conn);
            disconnect(connDied);

            if (m_utunInterface.isEmpty()) {
                if (processDied)
                    setError("udp-bypass crashed before creating utun interface");
                else
                    setError("udp-bypass failed to create utun interface (timeout)");
                m_logModel->appendLog("[Engine] Failed to get utun interface from udp-bypass");
                m_udpProcessManager->stop();
                setStatus("Stopped");
                delete platform;
                return;
            }

            m_logModel->appendLog("[Engine] udp-bypass utun interface: " + m_utunInterface);
        }

        // Setup firewall rules (TCP rdr for tpws + optionally UDP route-to utun)
        bool fwOk;
        if (hasUdp && !m_utunInterface.isEmpty()) {
            fwOk = macPlatform->setupFirewallWithUtun(strategy, m_utunInterface);
        } else {
            fwOk = platform->setupFirewall(strategy);
        }

        if (!fwOk) {
            setError("Failed to configure firewall rules");
            m_logModel->appendLog("[Engine] Firewall setup failed");
            if (hasUdp)
                m_udpProcessManager->stop();
            setStatus("Stopped");
            delete platform;
            return;
        }

        // Build and start tpws
        QString binary = platform->binaryPath();
        QStringList args = platform->buildArgs(strategy);

        m_logModel->appendLog("[Engine] Binary: " + binary);
        m_logModel->appendLog("[Engine] Args: " + args.join(' '));

        QStringList sudoArgs;
        sudoArgs << "-A" << binary << args;
        m_logModel->appendLog("[Engine] Requesting admin privileges...");
        m_processManager->start("/usr/bin/sudo", sudoArgs, env);
    }
#elif defined(PLATFORM_LINUX)
    {
        // Setup firewall rules (iptables)
        if (!platform->setupFirewall(strategy)) {
            setError("Failed to configure firewall rules");
            m_logModel->appendLog("[Engine] Firewall setup failed");
            setStatus("Stopped");
            delete platform;
            return;
        }

        // Build command line
        QString binary = platform->binaryPath();
        QStringList args = platform->buildArgs(strategy);

        m_logModel->appendLog("[Engine] Binary: " + binary);
        m_logModel->appendLog("[Engine] Args: " + args.join(' '));

        setStatus("Starting...");

        QProcessEnvironment env = platform->environment();
        env.insert("SUDO_ASKPASS", qgetenv("SUDO_ASKPASS"));

        QStringList sudoArgs;
        sudoArgs << "-A" << binary << args;
        m_logModel->appendLog("[Engine] Requesting admin privileges...");
        m_processManager->start("/usr/bin/sudo", sudoArgs, env);
    }
#else
    {
        // Setup firewall rules (WinDivert, VPN)
        if (!platform->setupFirewall(strategy)) {
            setError("Failed to configure firewall rules");
            m_logModel->appendLog("[Engine] Firewall setup failed");
            setStatus("Stopped");
            delete platform;
            return;
        }

        // Build command line
        QString binary = platform->binaryPath();
        QStringList args = platform->buildArgs(strategy);

        m_logModel->appendLog("[Engine] Binary: " + binary);
        m_logModel->appendLog("[Engine] Args: " + args.join(' '));

        setStatus("Starting...");

        // Start the process directly (Windows uses UAC manifest, mobile uses VPN)
        m_processManager->start(binary, args, platform->environment());
    }
#endif

    delete platform;
}

void ZapretEngine::stop()
{
    if (!m_running) return;

    setStatus("Stopping...");
    m_logModel->appendLog("[Engine] Stopping...");

#if defined(PLATFORM_MACOS) || defined(PLATFORM_LINUX)
    // We launched: sudo -A tpws/nfqws2 ...
    // terminate() sends SIGTERM to sudo, which forwards it to the child.
    // If that fails, find the child PID and kill it directly.
    qint64 sudoPid = m_processManager->pid();
    m_processManager->stop();

    if (sudoPid > 0) {
        // Check if tpws child process is still alive (orphaned after sudo exit).
        // Note: pgrep -x matches by exact name, so this could match another user's
        // tpws in theory. We accept this risk since tpws is rarely run independently.
        QProcess pgrep;
        pgrep.start("pgrep", {"-x", "tpws"});
        if (pgrep.waitForFinished(2000)) {
            QString remaining = QString::fromUtf8(pgrep.readAllStandardOutput()).trimmed();
            if (!remaining.isEmpty()) {
                m_logModel->appendLog("[Engine] Cleaning up orphaned tpws process(es)");
                for (const QString &pid : remaining.split('\n', Qt::SkipEmptyParts))
                    QProcess::execute("/usr/bin/sudo", {"-A", "kill", pid.trimmed()});
            }
        }
    }
#if defined(PLATFORM_MACOS)
    // Stop udp-bypass if running
    if (m_udpProcessManager->isRunning()) {
        m_logModel->appendLog("[Engine] Stopping udp-bypass...");
        m_udpProcessManager->stop();

        // Clean up orphaned udp-bypass process(es)
        QProcess pgrep2;
        pgrep2.start("pgrep", {"-x", "udp-bypass"});
        if (pgrep2.waitForFinished(2000)) {
            QString remaining = QString::fromUtf8(pgrep2.readAllStandardOutput()).trimmed();
            if (!remaining.isEmpty()) {
                m_logModel->appendLog("[Engine] Cleaning up orphaned udp-bypass process(es)");
                for (const QString &pid : remaining.split('\n', Qt::SkipEmptyParts))
                    QProcess::execute("/usr/bin/sudo", {"-A", "kill", pid.trimmed()});
            }
        }
    }
    m_utunInterface.clear();
#endif
#else
    m_processManager->stop();
#endif

    // Teardown firewall rules
    auto *platform = PlatformHelper::create(this);
    if (platform) {
        platform->teardownFirewall();
        delete platform;
    }
}

void ZapretEngine::restart()
{
    stop();
    start();
}

bool ZapretEngine::installService()
{
    auto *platform = PlatformHelper::create(this);
    if (!platform) return false;

    if (m_currentStrategyId.isEmpty()) {
        setError("No strategy selected for service installation");
        delete platform;
        return false;
    }

    Strategy strategy = m_strategyManager->strategyById(m_currentStrategyId);
    bool ok = platform->installService(strategy);
    if (!ok) {
        setError("Failed to install service");
    } else {
        m_logModel->appendLog("[Engine] Service installed successfully");
    }

    delete platform;
    return ok;
}

bool ZapretEngine::removeService()
{
    auto *platform = PlatformHelper::create(this);
    if (!platform) return false;

    bool ok = platform->removeService();
    if (!ok) {
        setError("Failed to remove service");
    } else {
        m_logModel->appendLog("[Engine] Service removed successfully");
    }

    delete platform;
    return ok;
}

void ZapretEngine::onProcessStarted()
{
    m_running = true;
    emit runningChanged();
    setStatus("Running");
    setError({});
    m_logModel->appendLog("[Engine] Process started");
}

void ZapretEngine::onProcessStopped(int exitCode)
{
    m_running = false;
    emit runningChanged();
    setStatus("Stopped");
    m_logModel->appendLog(QString("[Engine] Process stopped (exit code: %1)").arg(exitCode));
}

void ZapretEngine::onProcessOutput(const QString &line)
{
    m_logModel->appendLog(line);
    emit logMessage(line);
}

void ZapretEngine::onProcessError(const QString &error)
{
    setError(error);
    m_logModel->appendLog("[Engine] Error: " + error);
    setStatus("Stopped");
}

void ZapretEngine::setStatus(const QString &status)
{
    if (m_status != status) {
        m_status = status;
        emit statusChanged();
    }
}

void ZapretEngine::setError(const QString &error)
{
    if (m_errorString != error) {
        m_errorString = error;
        emit errorStringChanged();
    }
}

void ZapretEngine::onUdpProcessOutput(const QString &line)
{
    m_logModel->appendLog("[udp-bypass] " + line);
    emit logMessage(line);
}
