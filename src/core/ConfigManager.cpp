#include "ConfigManager.h"

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
    , m_settings("ZapretGui", "Zapret")
{
}

bool ConfigManager::autoStart() const { return m_settings.value("autoStart", false).toBool(); }
void ConfigManager::setAutoStart(bool enabled)
{
    if (autoStart() != enabled) {
        m_settings.setValue("autoStart", enabled);
        emit autoStartChanged();
    }
}

bool ConfigManager::gameFilter() const { return m_settings.value("gameFilter", false).toBool(); }
void ConfigManager::setGameFilter(bool enabled)
{
    if (gameFilter() != enabled) {
        m_settings.setValue("gameFilter", enabled);
        emit gameFilterChanged();
    }
}

bool ConfigManager::ipsetMode() const { return m_settings.value("ipsetMode", false).toBool(); }
void ConfigManager::setIpsetMode(bool enabled)
{
    if (ipsetMode() != enabled) {
        m_settings.setValue("ipsetMode", enabled);
        emit ipsetModeChanged();
    }
}

bool ConfigManager::checkUpdates() const { return m_settings.value("checkUpdates", true).toBool(); }
void ConfigManager::setCheckUpdates(bool enabled)
{
    if (checkUpdates() != enabled) {
        m_settings.setValue("checkUpdates", enabled);
        emit checkUpdatesChanged();
    }
}

QString ConfigManager::theme() const { return m_settings.value("theme", "system").toString(); }
void ConfigManager::setTheme(const QString &theme)
{
    if (this->theme() != theme) {
        m_settings.setValue("theme", theme);
        emit themeChanged();
    }
}

QString ConfigManager::lastStrategy() const { return m_settings.value("lastStrategy").toString(); }
void ConfigManager::setLastStrategy(const QString &id)
{
    if (lastStrategy() != id) {
        m_settings.setValue("lastStrategy", id);
        emit lastStrategyChanged();
    }
}

QVariant ConfigManager::value(const QString &key, const QVariant &defaultValue) const
{
    return m_settings.value(key, defaultValue);
}

void ConfigManager::setValue(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
}
