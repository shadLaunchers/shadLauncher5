// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QFont>
#include <QFontDatabase>
#include <QObject>
#include "param_model.h"

ParamModel::ParamModel(QObject* parent) : QAbstractTableModel(parent) {}

void ParamModel::setEntries(const std::vector<ParamEntry>& entries) {
    beginResetModel();
    m_entries = entries;
    endResetModel();
}

const std::vector<ParamEntry>& ParamModel::entries() const {
    return m_entries;
}

int ParamModel::rowCount(const QModelIndex&) const {
    return int(m_entries.size());
}

int ParamModel::columnCount(const QModelIndex&) const {
    return ColumnCount;
}

QVariant ParamModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_entries.size())) {
        return {};
    }

    const auto& entry = m_entries[index.row()];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case FieldColumn:
            return !entry.displayName.isEmpty() ? entry.displayName : entry.key;
        case KeyColumn:
            return entry.key;
        case ValueColumn:
            return entry.value;
        case TypeColumn:
            return entry.type;
        default:
            return {};
        }
    }

    // The full path disambiguates fields that share a friendly name (a disc
    // entry's "Content ID" versus the top-level one, say), and the raw value
    // is worth keeping reachable wherever a decoder rewrote it.
    if (role == Qt::ToolTipRole) {
        switch (index.column()) {
        case FieldColumn:
            return entry.key;
        case ValueColumn:
            return entry.rawValue != entry.value ? entry.rawValue : QVariant{};
        default:
            return {};
        }
    }

    if (role == Qt::FontRole && index.column() == KeyColumn) {
        return QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }

    return {};
}

QVariant ParamModel::headerData(int section, Qt::Orientation o, int role) const {
    if (role != Qt::DisplayRole || o != Qt::Horizontal) {
        return {};
    }
    switch (section) {
    case FieldColumn:
        return QObject::tr("Field");
    case KeyColumn:
        return QObject::tr("Key");
    case ValueColumn:
        return QObject::tr("Value");
    case TypeColumn:
        return QObject::tr("Type");
    default:
        return {};
    }
}

Qt::ItemFlags ParamModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}
