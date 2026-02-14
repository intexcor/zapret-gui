#pragma once

#include <QAbstractListModel>

class StrategyManager;

class StrategyListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        AvailableRole,
        SupportedPlatformsRole,
        TcpPortsRole,
        UdpPortsRole,
        FilterCountRole
    };

    explicit StrategyListModel(StrategyManager *manager, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private slots:
    void onStrategiesChanged();

private:
    StrategyManager *m_manager;
};
