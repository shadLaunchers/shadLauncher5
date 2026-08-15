// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <vector>
#include <QAbstractTableModel>
#include <QString>

struct SFOEntry {
    QString key;         // raw key
    QString displayName; // friendly key name
    QString value;
    QString type;
};

class SFOModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit SFOModel(QObject* parent = nullptr);
    void setEntries(const std::vector<SFOEntry>& entries);
    const std::vector<SFOEntry>& entries() const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    std::vector<SFOEntry> m_entries;
};
