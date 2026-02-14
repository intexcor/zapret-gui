#pragma once

#include <QObject>
#include <QList>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

struct StrategyFilter {
    QString protocol;            // "tcp" or "udp"
    QString ports;               // e.g. "80,443" or "19294-19344,50000-50100"
    QString l7Protocol;          // e.g. "discord,stun" (--filter-l7)
    QString l3Filter;            // e.g. "ipv4" (--filter-l3)
    QString hostlist;            // hostlist file path
    QString hostlistExclude;
    QString hostlistDomains;     // inline domain filter (--hostlist-domains)
    QString ipset;
    QString ipsetExclude;
    QString desyncMethod;        // e.g. "fake", "multisplit", "fake,fakedsplit"
    int desyncRepeats = 0;
    int splitSeqovl = 0;
    int splitPos = 0;
    QString splitPosStr;         // e.g. "1,midsld" for complex positions
    QString splitSeqovlPattern;  // pattern file path
    QString fakeQuic;            // fake QUIC packet file
    QString fakeTls;             // fake TLS packet file
    QString fakeTlsMod;          // --dpi-desync-fake-tls-mod
    QString fooling;             // e.g. "ts", "badseq"
    int badseqIncrement = 0;
    QString fakeUnknownUdp;
    QString desyncCutoff;        // e.g. "n2"
    bool anyProtocol = false;
    bool ipIdZero = false;       // --ip-id=zero
    QStringList tpwsOpts;        // tpws-specific flags: --hostcase, --domcase, --tlsrec, etc.

    QJsonObject toJson() const;
    static StrategyFilter fromJson(const QJsonObject &obj);
};

struct Strategy {
    QString id;
    QString name;
    QString description;
    QString tcpPorts;            // --wf-tcp ports
    QString udpPorts;            // --wf-udp ports
    bool gameFilterEnabled = false;
    QList<StrategyFilter> filters;
    QStringList supportedPlatforms; // "windows", "linux", "macos", "android", "ios"

    QJsonObject toJson() const;
    static Strategy fromJson(const QJsonObject &obj);
};

class StrategyManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY strategiesChanged)

public:
    explicit StrategyManager(QObject *parent = nullptr);

    void loadStrategies();
    void saveStrategies();

    int count() const;
    QList<Strategy> strategies() const;
    Strategy strategyById(const QString &id) const;
    Q_INVOKABLE int indexOfStrategy(const QString &id) const;

    Q_INVOKABLE QStringList strategyNames() const;
    Q_INVOKABLE QString strategyIdAt(int index) const;
    Q_INVOKABLE QString strategyNameById(const QString &id) const;
    Q_INVOKABLE QString strategyDescriptionById(const QString &id) const;
    Q_INVOKABLE bool isStrategyAvailableOnPlatform(const QString &id) const;

signals:
    void strategiesChanged();

private:
    QString strategiesFilePath() const;
    QList<Strategy> m_strategies;
};
