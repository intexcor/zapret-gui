#include "StrategyManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>

// --- StrategyFilter ---

QJsonObject StrategyFilter::toJson() const
{
    QJsonObject obj;
    obj["protocol"] = protocol;
    if (!ports.isEmpty()) obj["ports"] = ports;
    if (!l7Protocol.isEmpty()) obj["l7Protocol"] = l7Protocol;
    if (!l3Filter.isEmpty()) obj["l3Filter"] = l3Filter;
    if (!hostlist.isEmpty()) obj["hostlist"] = hostlist;
    if (!hostlistExclude.isEmpty()) obj["hostlistExclude"] = hostlistExclude;
    if (!hostlistDomains.isEmpty()) obj["hostlistDomains"] = hostlistDomains;
    if (!ipset.isEmpty()) obj["ipset"] = ipset;
    if (!ipsetExclude.isEmpty()) obj["ipsetExclude"] = ipsetExclude;
    if (!desyncMethod.isEmpty()) obj["desyncMethod"] = desyncMethod;
    if (desyncRepeats > 0) obj["desyncRepeats"] = desyncRepeats;
    if (splitSeqovl > 0) obj["splitSeqovl"] = splitSeqovl;
    if (splitPos > 0) obj["splitPos"] = splitPos;
    if (!splitPosStr.isEmpty()) obj["splitPosStr"] = splitPosStr;
    if (!splitSeqovlPattern.isEmpty()) obj["splitSeqovlPattern"] = splitSeqovlPattern;
    if (!fakeQuic.isEmpty()) obj["fakeQuic"] = fakeQuic;
    if (!fakeTls.isEmpty()) obj["fakeTls"] = fakeTls;
    if (!fakeTlsMod.isEmpty()) obj["fakeTlsMod"] = fakeTlsMod;
    if (!fooling.isEmpty()) obj["fooling"] = fooling;
    if (badseqIncrement > 0) obj["badseqIncrement"] = badseqIncrement;
    if (!fakeUnknownUdp.isEmpty()) obj["fakeUnknownUdp"] = fakeUnknownUdp;
    if (!desyncCutoff.isEmpty()) obj["desyncCutoff"] = desyncCutoff;
    if (anyProtocol) obj["anyProtocol"] = true;
    if (ipIdZero) obj["ipIdZero"] = true;
    if (!tpwsOpts.isEmpty()) {
        QJsonArray arr;
        for (const auto &opt : tpwsOpts)
            arr.append(opt);
        obj["tpwsOpts"] = arr;
    }
    return obj;
}

StrategyFilter StrategyFilter::fromJson(const QJsonObject &obj)
{
    StrategyFilter f;
    f.protocol = obj["protocol"].toString();
    f.ports = obj["ports"].toString();
    f.l7Protocol = obj["l7Protocol"].toString();
    f.l3Filter = obj["l3Filter"].toString();
    f.hostlist = obj["hostlist"].toString();
    f.hostlistExclude = obj["hostlistExclude"].toString();
    f.hostlistDomains = obj["hostlistDomains"].toString();
    f.ipset = obj["ipset"].toString();
    f.ipsetExclude = obj["ipsetExclude"].toString();
    f.desyncMethod = obj["desyncMethod"].toString();
    f.desyncRepeats = obj["desyncRepeats"].toInt();
    f.splitSeqovl = obj["splitSeqovl"].toInt();
    f.splitPos = obj["splitPos"].toInt();
    f.splitPosStr = obj["splitPosStr"].toString();
    f.splitSeqovlPattern = obj["splitSeqovlPattern"].toString();
    f.fakeQuic = obj["fakeQuic"].toString();
    f.fakeTls = obj["fakeTls"].toString();
    f.fakeTlsMod = obj["fakeTlsMod"].toString();
    f.fooling = obj["fooling"].toString();
    f.badseqIncrement = obj["badseqIncrement"].toInt();
    f.fakeUnknownUdp = obj["fakeUnknownUdp"].toString();
    f.desyncCutoff = obj["desyncCutoff"].toString();
    f.anyProtocol = obj["anyProtocol"].toBool();
    f.ipIdZero = obj["ipIdZero"].toBool();
    for (const auto &v : obj["tpwsOpts"].toArray())
        f.tpwsOpts.append(v.toString());
    return f;
}

// --- Strategy ---

QJsonObject Strategy::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    if (!description.isEmpty()) obj["description"] = description;
    if (!tcpPorts.isEmpty()) obj["tcpPorts"] = tcpPorts;
    if (!udpPorts.isEmpty()) obj["udpPorts"] = udpPorts;
    obj["gameFilterEnabled"] = gameFilterEnabled;

    QJsonArray filtersArr;
    for (const auto &f : filters)
        filtersArr.append(f.toJson());
    obj["filters"] = filtersArr;

    QJsonArray platforms;
    for (const auto &p : supportedPlatforms)
        platforms.append(p);
    obj["supportedPlatforms"] = platforms;

    return obj;
}

Strategy Strategy::fromJson(const QJsonObject &obj)
{
    Strategy s;
    s.id = obj["id"].toString();
    s.name = obj["name"].toString();
    s.description = obj["description"].toString();
    s.tcpPorts = obj["tcpPorts"].toString();
    s.udpPorts = obj["udpPorts"].toString();
    s.gameFilterEnabled = obj["gameFilterEnabled"].toBool();

    for (const auto &v : obj["filters"].toArray())
        s.filters.append(StrategyFilter::fromJson(v.toObject()));

    for (const auto &v : obj["supportedPlatforms"].toArray())
        s.supportedPlatforms.append(v.toString());

    return s;
}

// --- StrategyManager ---

StrategyManager::StrategyManager(QObject *parent)
    : QObject(parent)
{
}

QString StrategyManager::strategiesFilePath() const
{
    QString appDir = QCoreApplication::applicationDirPath();

    // macOS bundle: look in Resources
    QString resources = appDir + "/../Resources/strategies.json";
    if (QFile::exists(resources))
        return resources;

    // Look next to the binary
    QString local = appDir + "/strategies.json";
    if (QFile::exists(local))
        return local;

    // Try share directory (Linux installed)
    QString share = appDir + "/../share/zapret-gui/strategies.json";
    if (QFile::exists(share))
        return share;

    // Writable location for user modifications
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    return dataDir + "/strategies.json";
}

void StrategyManager::loadStrategies()
{
    QString path = strategiesFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("StrategyManager: Cannot open %s", qPrintable(path));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    m_strategies.clear();

    for (const auto &v : doc.array())
        m_strategies.append(Strategy::fromJson(v.toObject()));

    emit strategiesChanged();
}

void StrategyManager::saveStrategies()
{
    QJsonArray arr;
    for (const auto &s : m_strategies)
        arr.append(s.toJson());

    QString path = strategiesFilePath();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("StrategyManager: Cannot write %s", qPrintable(path));
        return;
    }

    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

int StrategyManager::count() const { return m_strategies.size(); }
QList<Strategy> StrategyManager::strategies() const { return m_strategies; }

Strategy StrategyManager::strategyById(const QString &id) const
{
    for (const auto &s : m_strategies)
        if (s.id == id) return s;
    return {};
}

int StrategyManager::indexOfStrategy(const QString &id) const
{
    for (int i = 0; i < m_strategies.size(); ++i)
        if (m_strategies[i].id == id) return i;
    return -1;
}

QStringList StrategyManager::strategyNames() const
{
    QStringList names;
    for (const auto &s : m_strategies)
        names << s.name;
    return names;
}

QString StrategyManager::strategyIdAt(int index) const
{
    if (index < 0 || index >= m_strategies.size()) return {};
    return m_strategies[index].id;
}

QString StrategyManager::strategyNameById(const QString &id) const
{
    return strategyById(id).name;
}

QString StrategyManager::strategyDescriptionById(const QString &id) const
{
    return strategyById(id).description;
}

bool StrategyManager::isStrategyAvailableOnPlatform(const QString &id) const
{
    Strategy s = strategyById(id);
    QString currentPlatform;
#if defined(PLATFORM_WINDOWS)
    currentPlatform = "windows";
#elif defined(PLATFORM_LINUX)
    currentPlatform = "linux";
#elif defined(PLATFORM_MACOS)
    currentPlatform = "macos";
#elif defined(PLATFORM_ANDROID)
    currentPlatform = "android";
#elif defined(PLATFORM_IOS)
    currentPlatform = "ios";
#endif
    return s.supportedPlatforms.contains(currentPlatform);
}
