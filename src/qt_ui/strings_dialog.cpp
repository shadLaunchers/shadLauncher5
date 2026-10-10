// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "strings_dialog.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>
#include "core/analysis/string_analysis.h"

namespace {
enum Column { ColOffset = 0, ColLength, ColText, ColCount };
}

void StringsModel::SetEntries(std::vector<Entry> entries) {
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
}

int StringsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

int StringsModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColCount;
}

QVariant StringsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(m_entries.size())) {
        return {};
    }
    const auto& e = m_entries[static_cast<size_t>(index.row())];
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColOffset:
            return QStringLiteral("0x%1").arg(e.offset, 8, 16, QLatin1Char('0'));
        case ColLength:
            return static_cast<int>(e.text.size());
        case ColText:
            return e.text;
        default:
            return {};
        }
    }
    if (role == Qt::ToolTipRole && index.column() == ColText && e.text.size() > 80) {
        return e.text;
    }
    return {};
}

QVariant StringsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case ColOffset:
        return tr("File Offset");
    case ColLength:
        return tr("Length");
    case ColText:
        return tr("String");
    default:
        return {};
    }
}

void StringsFilterModel::SetFilter(const QString& needle, int min_length) {
    m_needle = needle;
    m_minLength = min_length;
    invalidateFilter();
}

bool StringsFilterModel::filterAcceptsRow(int source_row, const QModelIndex&) const {
    const auto* model = static_cast<const StringsModel*>(sourceModel());
    const auto& e = model->At(source_row);
    if (e.text.size() < m_minLength) {
        return false;
    }
    return m_needle.isEmpty() || e.text.contains(m_needle, Qt::CaseInsensitive);
}

bool StringsFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    const auto* model = static_cast<const StringsModel*>(sourceModel());
    const auto& a = model->At(left.row());
    const auto& b = model->At(right.row());
    switch (left.column()) {
    case ColOffset:
        return a.offset < b.offset;
    case ColLength:
        return a.text.size() < b.text.size();
    default:
        return QString::compare(a.text, b.text, Qt::CaseInsensitive) < 0;
    }
}

StringsDialog::StringsDialog(const QString& title, std::shared_ptr<const std::vector<u8>> data,
                             const QString& suggestedFileName, QWidget* parent)
    : QDialog(parent), m_data(std::move(data)), m_suggestedFileName(suggestedFileName) {
    setWindowTitle(tr("Strings - %1").arg(title));
    resize(900, 640);

    auto* layout = new QVBoxLayout(this);

    auto* filter_row = new QHBoxLayout();
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Filter (case-insensitive substring)..."));
    m_filterEdit->setClearButtonEnabled(true);
    m_minLength = new QSpinBox(this);
    m_minLength->setRange(4, 256);
    m_minLength->setValue(6);
    m_minLength->setPrefix(tr("Min length: "));
    filter_row->addWidget(m_filterEdit, 1);
    filter_row->addWidget(m_minLength);
    layout->addLayout(filter_row);

    m_model = new StringsModel(this);
    m_proxy = new StringsFilterModel(this);
    m_proxy->setSourceModel(m_model);

    m_view = new QTableView(this);
    m_view->setModel(m_proxy);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(ColOffset, Qt::AscendingOrder);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setAlternatingRowColors(true);
    m_view->setWordWrap(false);
    m_view->verticalHeader()->setVisible(false);
    m_view->verticalHeader()->setDefaultSectionSize(m_view->fontMetrics().height() + 4);
    m_view->horizontalHeader()->setSectionResizeMode(ColOffset, QHeaderView::ResizeToContents);
    m_view->horizontalHeader()->setSectionResizeMode(ColLength, QHeaderView::ResizeToContents);
    m_view->horizontalHeader()->setSectionResizeMode(ColText, QHeaderView::Stretch);
    m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_view, 1);

    auto* buttons = new QHBoxLayout();
    m_countLabel = new QLabel(tr("Extracting strings..."), this);
    auto* copy_btn = new QPushButton(tr("Copy Selected"), this);
    auto* export_btn = new QPushButton(tr("Export Shown..."), this);
    auto* close_btn = new QPushButton(tr("Close"), this);
    buttons->addWidget(m_countLabel, 1);
    buttons->addWidget(copy_btn);
    buttons->addWidget(export_btn);
    buttons->addWidget(close_btn);
    layout->addLayout(buttons);

    m_filterTimer = new QTimer(this);
    m_filterTimer->setSingleShot(true);
    m_filterTimer->setInterval(250);

    connect(m_filterEdit, &QLineEdit::textChanged, m_filterTimer, qOverload<>(&QTimer::start));
    connect(m_minLength, qOverload<int>(&QSpinBox::valueChanged), m_filterTimer,
            qOverload<>(&QTimer::start));
    connect(m_filterTimer, &QTimer::timeout, this, &StringsDialog::applyFilter);
    connect(copy_btn, &QPushButton::clicked, this, &StringsDialog::onCopySelected);
    connect(export_btn, &QPushButton::clicked, this, &StringsDialog::onExport);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);

    m_proxy->SetFilter(QString(), m_minLength->value());
    startExtraction();
}

void StringsDialog::startExtraction() {
    using Entries = std::vector<StringsModel::Entry>;
    auto* watcher = new QFutureWatcher<Entries>(this);
    connect(watcher, &QFutureWatcher<Entries>::finished, this, [this, watcher] {
        m_model->SetEntries(watcher->result());
        watcher->deleteLater();
        updateCountLabel();
    });
    watcher->setFuture(QtConcurrent::run([data = m_data]() -> Entries {
        Entries out;
        if (!data) {
            return out;
        }
        auto found = Core::Analysis::ExtractStrings(*data, 4);
        out.reserve(found.size());
        for (auto& s : found) {
            out.push_back({s.offset, QString::fromLatin1(s.text.data(),
                                                         static_cast<qsizetype>(s.text.size()))});
        }
        return out;
    }));
}

void StringsDialog::applyFilter() {
    m_proxy->SetFilter(m_filterEdit->text(), m_minLength->value());
    updateCountLabel();
}

void StringsDialog::updateCountLabel() {
    m_countLabel->setText(
        tr("Showing %1 of %2 strings").arg(m_proxy->rowCount()).arg(m_model->rowCount()));
}

void StringsDialog::onCopySelected() {
    QStringList lines;
    const auto rows = m_view->selectionModel()->selectedRows(ColText);
    for (const auto& idx : rows) {
        lines << idx.data().toString();
    }
    if (!lines.isEmpty()) {
        QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    }
}

void StringsDialog::onExport() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Strings"), m_suggestedFileName, tr("Text Files (*.txt)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not write to %1").arg(path));
        return;
    }
    QTextStream stream(&file);
    for (int row = 0; row < m_proxy->rowCount(); row++) {
        const auto src = m_proxy->mapToSource(m_proxy->index(row, 0));
        const auto& e = m_model->At(src.row());
        stream << QStringLiteral("0x%1  ").arg(e.offset, 8, 16, QLatin1Char('0')) << e.text << '\n';
    }
}
