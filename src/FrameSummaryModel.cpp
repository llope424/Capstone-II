#include "FrameSummaryModel.h"

namespace {
QString formatId(quint32 id, bool extended)
{
    return QStringLiteral("0x%1").arg(id, extended ? 8 : 3, 16, QChar('0')).toUpper();
}

QString formatData(const quint8 *data, quint8 length)
{
    QStringList parts;
    for (int i = 0; i < length; ++i)
        parts << QStringLiteral("%1").arg(data[i], 2, 16, QChar('0')).toUpper();
    return parts.join(' ');
}

quint64 keyOf(const CanFrame &f) { return (quint64(f.bus) << 32) | f.id; }
}

FrameSummaryModel::FrameSummaryModel(QObject *parent) : QAbstractTableModel(parent) {}

int FrameSummaryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

int FrameSummaryModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant FrameSummaryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries.at(index.row());
    const int col = index.column();

    if (role == Qt::DisplayRole) {
        switch (col) {
        case Id:     return formatId(e.id, e.extended);
        case Bus:    return QString::number(e.bus);
        case Count:  return QString::number(e.count);
        case Dlc:    return QString::number(e.length);
        case Period: return e.periodMs < 0 ? QStringLiteral("-") : QString::number(e.periodMs, 'f', 1);
        case Data:   return formatData(e.data, e.length);
        }
    } else if (role == SortRole) {
        switch (col) {
        case Id:     return static_cast<uint>(e.id);
        case Bus:    return static_cast<uint>(e.bus);
        case Count:  return static_cast<qulonglong>(e.count);
        case Dlc:    return static_cast<uint>(e.length);
        case Period: return e.periodMs;
        case Data:   return formatData(e.data, e.length);
        }
    }
    return {};
}

QVariant FrameSummaryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    static const char *titles[ColumnCount] = {"ID", "Bus", "Count", "DLC", "Period (ms)", "Last Data"};
    if (section >= 0 && section < ColumnCount)
        return QString::fromLatin1(titles[section]);
    return {};
}

void FrameSummaryModel::addFrames(const QVector<CanFrame> &frames)
{
    if (frames.isEmpty())
        return;

    int minChanged = m_entries.size();
    int maxChanged = -1;

    for (const CanFrame &f : frames) {
        const quint64 key = keyOf(f);
        auto it = m_index.constFind(key);
        if (it != m_index.constEnd()) {
            Entry &e = m_entries[it.value()];
            if (f.timestampUs >= e.lastTsUs)
                e.periodMs = (f.timestampUs - e.lastTsUs) / 1000.0;
            e.lastTsUs = f.timestampUs;
            e.count++;
            e.length = f.length;
            for (int i = 0; i < 8; ++i)
                e.data[i] = f.data[i];
            minChanged = qMin(minChanged, it.value());
            maxChanged = qMax(maxChanged, it.value());
        } else {
            const int row = m_entries.size();
            beginInsertRows(QModelIndex(), row, row);
            Entry e;
            e.id = f.id;
            e.extended = f.extended;
            e.bus = f.bus;
            e.count = 1;
            e.length = f.length;
            for (int i = 0; i < 8; ++i)
                e.data[i] = f.data[i];
            e.lastTsUs = f.timestampUs;
            m_entries.append(e);
            m_index.insert(key, row);
            endInsertRows();
        }
    }

    if (maxChanged >= 0)
        emit dataChanged(index(minChanged, 0), index(maxChanged, ColumnCount - 1));
}

void FrameSummaryModel::clear()
{
    beginResetModel();
    m_entries.clear();
    m_index.clear();
    endResetModel();
}
