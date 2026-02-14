#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>

#include "core/ZapretEngine.h"
#include "core/StrategyManager.h"
#include "core/HostlistManager.h"
#include "core/ConfigManager.h"
#include "core/UpdateChecker.h"
#include "models/StrategyListModel.h"
#include "models/LogModel.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ZapretGui");
    app.setApplicationName("Zapret");
    app.setApplicationVersion(APP_VERSION);

    // Core singletons
    ConfigManager configManager;
    StrategyManager strategyManager;
    HostlistManager hostlistManager;
    LogModel logModel;

    strategyManager.loadStrategies();
    hostlistManager.loadLists();

    ZapretEngine engine(&strategyManager, &hostlistManager, &logModel);
    UpdateChecker updateChecker;

    // Models
    StrategyListModel strategyListModel(&strategyManager);

    // QML engine
    QQmlApplicationEngine qmlEngine;

    QQmlContext *ctx = qmlEngine.rootContext();
    ctx->setContextProperty("zapretEngine", &engine);
    ctx->setContextProperty("strategyManager", &strategyManager);
    ctx->setContextProperty("hostlistManager", &hostlistManager);
    ctx->setContextProperty("configManager", &configManager);
    ctx->setContextProperty("updateChecker", &updateChecker);
    ctx->setContextProperty("strategyListModel", &strategyListModel);
    ctx->setContextProperty("logModel", &logModel);

    qmlEngine.loadFromModule("ZapretGui", "Main");

    if (qmlEngine.rootObjects().isEmpty())
        return -1;

    // Restore last strategy on startup
    QString lastStrategy = configManager.value("lastStrategy").toString();
    if (!lastStrategy.isEmpty()) {
        engine.setCurrentStrategyId(lastStrategy);
    }

    return app.exec();
}
