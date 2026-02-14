#pragma once

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>

class ProcessManager : public QObject
{
    Q_OBJECT

public:
    explicit ProcessManager(QObject *parent = nullptr);
    ~ProcessManager();

    void start(const QString &program, const QStringList &args,
               const QProcessEnvironment &env = QProcessEnvironment::systemEnvironment());
    void stop();
    bool isRunning() const;

    qint64 pid() const;

signals:
    void started();
    void stopped(int exitCode);
    void outputLine(const QString &line);
    void errorOccurred(const QString &error);

private slots:
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onErrorOccurred(QProcess::ProcessError error);

private:
    QProcess *m_process = nullptr;
    bool m_stopping = false;
};
