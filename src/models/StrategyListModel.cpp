#include "StrategyListModel.h"
#include "core/StrategyManager.h"

StrategyListModel::StrategyListModel(StrategyManager *manager, QObject *parent)
    : QAbstractListModel(parent)
    , m_manager(manager)
{
    connect(m_manager, &StrategyManager::strategiesChanged,
            this, &StrategyListModel::onStrategiesChanged);
}

int StrategyListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_manager->count();
}

QVariant StrategyListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_manager->count())
        return {};

    const auto strategies = m_manager->strategies();
    const auto &s = strategies[index.row()];

    switch (role) {
    case IdRole: return s.id;
    case NameRole: return s.name;
    case DescriptionRole: return s.description;
    case AvailableRole: return m_manager->isStrategyAvailableOnPlatform(s.id);
    case SupportedPlatformsRole: return s.supportedPlatforms.join(", ");
    case TcpPortsRole: return s.tcpPorts;
    case UdpPortsRole: return s.udpPorts;
    case FilterCountRole: return s.filters.size();
    }

    return {};
}

QHash<int, QByteArray> StrategyListModel::roleNames() const
{
    return {
        {IdRole, "strategyId"},
        {NameRole, "name"},
        {DescriptionRole, "description"},
        {AvailableRole, "available"},
        {SupportedPlatformsRole, "supportedPlatforms"},
        {TcpPortsRole, "tcpPorts"},
        {UdpPortsRole, "udpPorts"},
        {FilterCountRole, "filterCount"},
    };
}

void StrategyListModel::onStrategiesChanged()
{
    beginResetModel();
    endResetModel();
}
