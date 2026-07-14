#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QVector>

#include "CanFrame.h"

// "Overview" model for the Raw Traffic tab: instead of one row per received
// frame, it keeps one row per unique CAN ID and updates that row in place -
// count, last data, and the period between the last two sightings. Mirrors
// SavvyCAN's overview mode. Pair with a QSortFilterProxyModel for sort/filter.
class FrameSummaryModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    static constexpr int SortRole = Qt::UserRole + 1;
    enum Column { Id = 0, Bus, Count, Dlc, Period, Data, ColumnCount };

    explicit FrameSummaryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void addFrames(const QVector<CanFrame> &frames);
    void clear();

private:
    struct Entry
    {
        quint32 id = 0;
        bool extended = false;
        quint8 bus = 0;
        quint64 count = 0;
        quint8 length = 0;
        quint8 data[8] = {0};
        quint32 lastTsUs = 0;
        double periodMs = -1.0; // -1 until at least two sightings
    };

    QVector<Entry> m_entries;
    QHash<quint64, int> m_index; // key: (bus<<32)|id -> row
};
