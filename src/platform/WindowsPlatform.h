#pragma once

#include "PlatformHelper.h"

class WindowsPlatform : public PlatformHelper
{
    Q_OBJECT

public:
    explicit WindowsPlatform(QObject *parent = nullptr);

    QString platformName() const override;
    QString binaryPath() const override;
    QString binaryDownloadUrl() const override;
    QStringList buildArgs(const Strategy &strategy) const override;
    bool setupFirewall(const Strategy &strategy) override;
    bool teardownFirewall() override;
    bool installService(const Strategy &strategy) override;
    bool removeService() override;
    bool elevatePrivileges() override;

private:
    QStringList buildFilterArgs(const StrategyFilter &filter) const;
    QString resolveFilePath(const QString &filename) const;
};
