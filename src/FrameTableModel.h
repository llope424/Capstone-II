#pragma once

#include <QAbstractTableModel>
#include <QVector>

#include "CanFrame.h"

// Table model backing the Raw Traffic view. Holds received CAN frames in arrival
// order and exposes them as 6 columns (Time, Bus, ID, Ext, DLC, Data). Pairing
// this with a QSortFilterProxyModel gives click-to-sort headers with correct
// *numeric* ordering (via SortRole) while the "keep newest N" trimming always
// operates on true arrival order, independent of how the view is sorted.
class FrameTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    // Custom role the sort proxy reads, so columns sort by numeric value
    // (timestamp, id, dlc, ...) rather than by the displayed text.
    static constexpr int SortRole = Qt::UserRole + 1;

    enum Column { Time = 0, Bus, Id, Ext, Dlc, Data, ColumnCount };

    explicit FrameTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void addFrames(const QVector<CanFrame> &frames);
    void clear();
    int maxRows() const { return m_maxRows; }

private:
    QVector<CanFrame> m_frames;
    int m_maxRows = 5000;
};
