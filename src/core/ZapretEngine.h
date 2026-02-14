#pragma once

#include <QObject>
#include "ProcessManager.h"

class StrategyManager;
class HostlistManager;
class LogModel;

class ZapretEngine : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString currentStrategyId READ currentStrategyId WRITE setCurrentStrategyId NOTIFY currentStrategyIdChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    explicit ZapretEngine(StrategyManager *strategyMgr,
                          HostlistManager *hostlistMgr,
                          LogModel *logModel,
                          QObject *parent = nullptr);
    ~ZapretEngine();

    bool isRunning() const;
    QString status() const;
    QString currentStrategyId() const;
    void setCurrentStrategyId(const QString &id);
    QString errorString() const;

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void restart();

    Q_INVOKABLE bool installService();
    Q_INVOKABLE bool removeService();

signals:
    void runningChanged();
    void statusChanged();
    void currentStrategyIdChanged();
    void errorStringChanged();
    void logMessage(const QString &message);

private slots:
    void onProcessStarted();
    void onProcessStopped(int exitCode);
    void onProcessOutput(const QString &line);
    void onProcessError(const QString &error);

    void onUdpProcessOutput(const QString &line);

private:
    void setStatus(const QString &status);
    void setError(const QString &error);

    StrategyManager *m_strategyManager = nullptr;
    HostlistManager *m_hostlistManager = nullptr;
    LogModel *m_logModel = nullptr;
    ProcessManager *m_processManager = nullptr;
    ProcessManager *m_udpProcessManager = nullptr;

    bool m_running = false;
    QString m_status = "Stopped";
    QString m_currentStrategyId;
    QString m_errorString;
    QString m_utunInterface;
};
