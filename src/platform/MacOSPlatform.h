#pragma once

#include "PlatformHelper.h"

class MacOSPlatform : public PlatformHelper
{
    Q_OBJECT

public:
    explicit MacOSPlatform(QObject *parent = nullptr);

    QString platformName() const override;
    QString binaryPath() const override;
    QString binaryDownloadUrl() const override;
    QStringList buildArgs(const Strategy &strategy) const override;
    bool setupFirewall(const Strategy &strategy) override;
    bool teardownFirewall() override;
    bool installService(const Strategy &strategy) override;
    bool removeService() override;
    bool elevatePrivileges() override;

    // udp-bypass support
    QString udpBypassBinaryPath() const;
    QStringList buildUdpBypassArgs(const Strategy &strategy) const;
    bool strategyHasUdpFilters(const Strategy &strategy) const;
    bool setupFirewallWithUtun(const Strategy &strategy, const QString &utunIface);

    // Sudoers setup for passwordless operation
    bool hasSudoersSetup() const;
    bool setupSudoers();
    bool removeSudoers();

private:
    QString resolveFilePath(const QString &filename) const;
    QString resolveFakeFilePath(const QString &filename) const;
    QStringList buildFilterArgs(const StrategyFilter &filter) const;

    int m_proxyPort = 988;
    bool m_pfConfigured = false;
};
