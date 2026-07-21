#include "ObdMessageModel.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "PidEncoders.h"

ObdMessageModel::ObdMessageModel(QObject *parent) : QAbstractTableModel(parent) {}

int ObdMessageModel::rowCount(const QModelIndex &) const { return 0; }

int ObdMessageModel::columnCount(const QModelIndex &) const { return 0; }

QVariant ObdMessageModel::data(const QModelIndex &, int) const { return {}; }

void ObdMessageModel::setPidRaw(quint8 pid, const QByteArray &raw) { m_pidRaw.insert(pid, raw); }

QByteArray ObdMessageModel::pidRaw(quint8 pid) const { return m_pidRaw.value(pid); }

void ObdMessageModel::setStoredDtcs(const QStringList &codes) { m_stored = codes; }

void ObdMessageModel::setPendingDtcs(const QStringList &codes) { m_pending = codes; }

void ObdMessageModel::setPermanentDtcs(const QStringList &codes) { m_permanent = codes; }

void ObdMessageModel::clearDtcs()
{
    m_stored.clear();
    m_pending.clear();
}

void ObdMessageModel::setVin(const QString &vin) { m_vin = vin; }

void ObdMessageModel::setCalIds(const QStringList &ids) { m_calIds = ids; }

bool ObdMessageModel::loadBundled()
{
    QFile f(QStringLiteral(":/emulator/obd_message.json"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return loadScenariosJson(f.readAll());
}

bool ObdMessageModel::loadScenariosJson(const QByteArray &json)
{
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonArray arr = doc.object().value(QStringLiteral("scenarios")).toArray();
    if (arr.isEmpty())
        return false;

    const auto dtcList = [](const QJsonObject &o, const char *key) {
        QStringList out;
        for (const QJsonValue &v : o.value(QLatin1String(key)).toArray())
            out << v.toString();
        return out;
    };

    QVector<Scenario> loaded;
    for (const QJsonValue &sv : arr) {
        const QJsonObject o = sv.toObject();
        Scenario s;
        s.name = o.value(QStringLiteral("name")).toString();
        s.vin = o.value(QStringLiteral("vin")).toString();
        for (const QJsonValue &c : o.value(QStringLiteral("calIds")).toArray())
            s.calIds << c.toString();

        const QJsonObject pids = o.value(QStringLiteral("pids")).toObject();
        for (auto it = pids.begin(); it != pids.end(); ++it) {
            QString key = it.key();
            if (key.startsWith(QLatin1String("0x")) || key.startsWith(QLatin1String("0X")))
                key = key.mid(2);
            bool ok = false;
            const quint8 pid = quint8(key.toUInt(&ok, 16));
            if (ok)
                s.pids.insert(pid, it.value().toDouble());
        }

        const QJsonObject dtcs = o.value(QStringLiteral("dtcs")).toObject();
        s.storedDtcs = dtcList(dtcs, "stored");
        s.pendingDtcs = dtcList(dtcs, "pending");
        s.permanentDtcs = dtcList(dtcs, "permanent");
        s.silentEcus = o.value(QStringLiteral("silentEcus")).toInt(0);

        if (!s.name.isEmpty())
            loaded.append(s);
    }
    if (loaded.isEmpty())
        return false;

    m_scenarios = loaded;
    setActiveScenario(m_scenarios.first().name); // start on the first scenario
    return true;
}

QStringList ObdMessageModel::scenarioNames() const
{
    QStringList names;
    for (const Scenario &s : m_scenarios)
        names << s.name;
    return names;
}

int ObdMessageModel::indexOfScenario(const QString &name) const
{
    for (int i = 0; i < m_scenarios.size(); ++i)
        if (m_scenarios.at(i).name == name)
            return i;
    return -1;
}

void ObdMessageModel::setActiveScenario(const QString &name)
{
    const int idx = indexOfScenario(name);
    if (idx < 0)
        return;
    const Scenario &s = m_scenarios.at(idx);
    m_activeScenario = name;

    m_pidRaw.clear();
    for (auto it = s.pids.constBegin(); it != s.pids.constEnd(); ++it)
        m_pidRaw.insert(it.key(), PidEncoders::encode(it.key(), it.value()));

    m_stored = s.storedDtcs;
    m_pending = s.pendingDtcs;
    m_permanent = s.permanentDtcs;
    m_silentEcus = s.silentEcus;
    if (!s.vin.isEmpty())
        m_vin = s.vin;
    if (!s.calIds.isEmpty())
        m_calIds = s.calIds;
}
