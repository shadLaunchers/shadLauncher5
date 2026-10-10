// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>
#include <QAbstractTableModel>
#include <QDialog>
#include <QSortFilterProxyModel>
#include <QString>
#include "common/types.h"

class QLabel;
class QLineEdit;
class QSpinBox;
class QTableView;
class QTimer;

class StringsModel : public QAbstractTableModel {
    Q_OBJECT
public:
    struct Entry {
        u64 offset = 0;
        QString text;
    };

    using QAbstractTableModel::QAbstractTableModel;
    void SetEntries(std::vector<Entry> entries);
    const Entry& At(int row) const {
        return m_entries[static_cast<size_t>(row)];
    }

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    std::vector<Entry> m_entries;
};

class StringsFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void SetFilter(const QString& needle, int min_length);

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    QString m_needle;
    int m_minLength = 4;
};

class StringsDialog : public QDialog {
    Q_OBJECT
public:
    StringsDialog(const QString& title, std::shared_ptr<const std::vector<u8>> data,
                  const QString& suggestedFileName, QWidget* parent = nullptr);

private slots:
    void applyFilter();
    void onCopySelected();
    void onExport();

private:
    void startExtraction();
    void updateCountLabel();

    std::shared_ptr<const std::vector<u8>> m_data;
    QString m_suggestedFileName;

    StringsModel* m_model = nullptr;
    StringsFilterModel* m_proxy = nullptr;
    QTableView* m_view = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QSpinBox* m_minLength = nullptr;
    QLabel* m_countLabel = nullptr;
    QTimer* m_filterTimer = nullptr;
};
