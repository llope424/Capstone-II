#include "FrameTableModel.h"

namespace {
QString formatId(const CanFrame &f)
{
    return QStringLiteral("0x%1").arg(f.id, f.extended ? 8 : 3, 16, QChar('0')).toUpper();
}

QString formatData(const CanFrame &f)
{
    QStringList parts;
    for (int i = 0; i < f.length; ++i)
        parts << QStringLiteral("%1").arg(f.data[i], 2, 16, QChar('0')).toUpper();
    return parts.join(' ');
}
}

FrameTableModel::FrameTableModel(QObject *parent) : QAbstractTableModel(parent) {}

int FrameTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_frames.size();
}

int FrameTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant FrameTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_frames.size())
        return {};
    const CanFrame &f = m_frames.at(index.row());
    const int col = index.column();

    if (role == Qt::DisplayRole) {
        switch (col) {
        case Time: return QString::number(f.timestampUs / 1000000.0, 'f', 6);
        case Bus:  return QString::number(f.bus);
        case Id:   return formatId(f);
        case Ext:  return f.extended ? QStringLiteral("Yes") : QStringLiteral("No");
        case Dlc:  return QString::number(f.length);
        case Data: return formatData(f);
        }
    } else if (role == SortRole) {
        // Numeric keys so sorting orders by value, not by the displayed string.
        switch (col) {
        case Time: return static_cast<double>(f.timestampUs);
        case Bus:  return static_cast<uint>(f.bus);
        case Id:   return static_cast<uint>(f.id);
        case Ext:  return f.extended ? 1 : 0;
        case Dlc:  return static_cast<uint>(f.length);
        case Data: return formatData(f);
        }
    }
    return {};
}

QVariant FrameTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    static const char *titles[ColumnCount] = {"Time (s)", "Bus", "ID", "Ext", "DLC", "Data"};
    if (section >= 0 && section < ColumnCount)
        return QString::fromLatin1(titles[section]);
    return {};
}

void FrameTableModel::addFrames(const QVector<CanFrame> &frames)
{
    if (frames.isEmpty())
        return;

    beginInsertRows(QModelIndex(), m_frames.size(), m_frames.size() + frames.size() - 1);
    m_frames += frames;
    endInsertRows();

    // Trim the oldest arrivals (front of the vector) beyond the cap. This is
    // always by arrival order, regardless of the view's current sort column.
    if (m_frames.size() > m_maxRows) {
        const int remove = m_frames.size() - m_maxRows;
        beginRemoveRows(QModelIndex(), 0, remove - 1);
        m_frames.remove(0, remove);
        endRemoveRows();
    }
}

void FrameTableModel::clear()
{
    beginResetModel();
    m_frames.clear();
    endResetModel();
}
