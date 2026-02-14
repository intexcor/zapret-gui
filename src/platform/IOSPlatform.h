#pragma once

#include "PlatformHelper.h"

class IOSPlatform : public PlatformHelper
{
    Q_OBJECT

public:
    explicit IOSPlatform(QObject *parent = nullptr);

    QString platformName() const override;
    QString binaryPath() const override;
    QString binaryDownloadUrl() const override;
    QStringList buildArgs(const Strategy &strategy) const override;
    bool setupFirewall(const Strategy &strategy) override;
    bool teardownFirewall() override;
    bool installService(const Strategy &strategy) override;
    bool removeService() override;
    bool elevatePrivileges() override;

};
