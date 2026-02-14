#include "ProcessManager.h"

ProcessManager::ProcessManager(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &ProcessManager::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &ProcessManager::onReadyReadStderr);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ProcessManager::onFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &ProcessManager::onErrorOccurred);
}

ProcessManager::~ProcessManager()
{
    stop();
}

void ProcessManager::start(const QString &program, const QStringList &args,
                           const QProcessEnvironment &env)
{
    if (m_process->state() != QProcess::NotRunning) {
        stop();
    }

    m_stopping = false;
    m_process->setProcessEnvironment(env);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->start(program, args);

    if (m_process->waitForStarted(5000)) {
        emit started();
    }
}

void ProcessManager::stop()
{
    if (m_process->state() == QProcess::NotRunning)
        return;

    m_stopping = true;
    m_process->terminate();
    if (!m_process->waitForFinished(3000)) {
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

bool ProcessManager::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

qint64 ProcessManager::pid() const
{
    return m_process->processId();
}

void ProcessManager::onReadyReadStdout()
{
    while (m_process->canReadLine()) {
        QString line = QString::fromUtf8(m_process->readLine()).trimmed();
        if (!line.isEmpty())
            emit outputLine(line);
    }
}

void ProcessManager::onReadyReadStderr()
{
    while (m_process->canReadLine()) {
        QString line = QString::fromUtf8(m_process->readLine()).trimmed();
        if (!line.isEmpty())
            emit outputLine("[stderr] " + line);
    }
}

void ProcessManager::onFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    // Read any remaining output
    onReadyReadStdout();

    // SIGTERM (15) and SIGKILL (9) are expected when we stop the process — not crashes
    if (exitStatus == QProcess::CrashExit && exitCode != 15 && exitCode != 9) {
        emit errorOccurred("Process crashed unexpectedly.");
    }
    emit stopped(exitCode);
}

void ProcessManager::onErrorOccurred(QProcess::ProcessError error)
{
    // When we intentionally stop the process, QProcess reports Crashed —
    // suppress this since onFinished already handles SIGTERM/SIGKILL exit codes.
    if (m_stopping && error == QProcess::Crashed)
        return;

    QString msg;
    switch (error) {
    case QProcess::FailedToStart:
        msg = "Failed to start process. Check that the binary exists and has execute permissions.";
        break;
    case QProcess::Crashed:
        msg = "Process crashed unexpectedly.";
        break;
    case QProcess::Timedout:
        msg = "Process timed out.";
        break;
    case QProcess::WriteError:
        msg = "Write error communicating with process.";
        break;
    case QProcess::ReadError:
        msg = "Read error communicating with process.";
        break;
    default:
        msg = "Unknown process error.";
        break;
    }
    emit errorOccurred(msg);
}
