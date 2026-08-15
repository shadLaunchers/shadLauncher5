// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QObject>
#include "sfo_model.h"

SFOModel::SFOModel(QObject* parent) : QAbstractTableModel(parent) {}

void SFOModel::setEntries(const std::vector<SFOEntry>& entries) {
    beginResetModel();
    m_entries = entries;
    endResetModel();
}

const std::vector<SFOEntry>& SFOModel::entries() const {
    return m_entries;
}

int SFOModel::rowCount(const QModelIndex&) const {
    return int(m_entries.size());
}
int SFOModel::columnCount(const QModelIndex&) const {
    return 3;
}

QVariant SFOModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid())
        return {};

    const auto& entry = m_entries[index.row()];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0:
            return !entry.displayName.isEmpty() ? entry.displayName : entry.key;
        case 1:
            return entry.value;
        case 2:
            return entry.type;
        }
    }

    // Tooltip = ORIGINAL KEY for column 0
    if (role == Qt::ToolTipRole && index.column() == 0) {
        return entry.key;
    }

    return {};
}

QVariant SFOModel::headerData(int section, Qt::Orientation o, int role) const {
    if (role != Qt::DisplayRole || o != Qt::Horizontal)
        return {};
    switch (section) {
    case 0:
        return QObject::tr("Key");
    case 1:
        return QObject::tr("Value");
    case 2:
        return QObject::tr("Type");
    }
    return {};
}

Qt::ItemFlags SFOModel::flags(const QModelIndex& index) const {
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}
