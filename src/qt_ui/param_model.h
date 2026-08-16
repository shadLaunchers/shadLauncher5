// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <vector>
#include <QAbstractTableModel>
#include <QString>

struct ParamEntry {
    QString key;         // full JSON path, e.g. "disc[0].contents[0].contentId"
    QString lookupKey;   // path with array indices collapsed, for label lookup
    QString displayName; // friendly field name
    QString rawValue;    // value exactly as it appears in the document
    QString value;       // decoded/annotated value
    QString type;        // JSON type name
    int depth = 0;       // nesting level, 0 for top-level keys
};

class ParamModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        FieldColumn = 0,
        KeyColumn,
        ValueColumn,
        TypeColumn,
        ColumnCount,
    };

    explicit ParamModel(QObject* parent = nullptr);
    void setEntries(const std::vector<ParamEntry>& entries);
    const std::vector<ParamEntry>& entries() const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    std::vector<ParamEntry> m_entries;
};
