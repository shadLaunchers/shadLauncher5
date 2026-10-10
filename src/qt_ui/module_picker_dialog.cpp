// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "module_picker_dialog.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include "common/path_util.h"
#include "core/analysis/export_index.h"
#include "core/analysis/middleware.h"
#include "core/file_sys/game_backend.h"
#include "module_opener.h"

namespace {

enum Column { ColFile, ColLocation, ColCount };
constexpr int kEntryRole = Qt::UserRole;

QString PathString(const std::filesystem::path& p) {
    QString out;
    Common::FS::PathToQString(out, p);
    return out;
}

} // namespace

ModulePickerDialog::ModulePickerDialog(const QString& title, std::filesystem::path game_root,
                                       std::filesystem::path sys_modules_dir,
                                       bool include_sys_modules, QWidget* parent)
    : QDialog(parent), m_title(title), m_gameRoot(std::move(game_root)),
      m_sysDir(std::move(sys_modules_dir)) {
    setWindowTitle(m_gameRoot.empty() ? tr("sys_modules - ELF Info")
                                      : tr("Choose a Module - %1").arg(title));
    resize(760, 560);

    auto* layout = new QVBoxLayout(this);

    auto* top = new QHBoxLayout();
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter by file name or folder..."));
    m_filter->setClearButtonEnabled(true);
    m_includeSys = new QCheckBox(tr("Show sys_modules"), this);
    m_includeSys->setToolTip(PathString(m_sysDir));
    m_includeSys->setChecked(include_sys_modules || m_gameRoot.empty());
    m_includeSys->setEnabled(!m_gameRoot.empty());
    top->addWidget(m_filter, 1);
    top->addWidget(m_includeSys);
    layout->addLayout(top);

    m_table = new QTableWidget(0, ColCount, this);
    m_table->setHorizontalHeaderLabels({tr("File"), tr("Location")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionResizeMode(ColFile, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColLocation, QHeaderView::ResizeToContents);
    m_table->setToolTip(tr("Double-click to open in ELF Info."));
    layout->addWidget(m_table, 1);

    auto* bottom = new QHBoxLayout();
    m_status = new QLabel(this);
    auto* browse_btn = new QPushButton(tr("Browse for a File..."), this);
    browse_btn->setToolTip(tr("Open any eboot, PRX/SPRX or other ELF/SELF file on disk."));
    auto* open_btn = new QPushButton(tr("Open in ELF Info"), this);
    open_btn->setDefault(true);
    auto* close_btn = new QPushButton(tr("Close"), this);
    bottom->addWidget(m_status, 1);
    bottom->addWidget(browse_btn);
    bottom->addWidget(open_btn);
    bottom->addWidget(close_btn);
    layout->addLayout(bottom);

    connect(m_filter, &QLineEdit::textChanged, this, &ModulePickerDialog::applyFilter);
    connect(m_includeSys, &QCheckBox::toggled, this, &ModulePickerDialog::startListing);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &ModulePickerDialog::openSelected);
    connect(open_btn, &QPushButton::clicked, this, &ModulePickerDialog::openSelected);
    connect(browse_btn, &QPushButton::clicked, this, &ModulePickerDialog::browseForFile);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);

    startListing();
    m_filter->setFocus();
}

void ModulePickerDialog::startListing() {
    if (m_listing) {
        return;
    }
    m_listing = true;
    m_status->setText(tr("Looking for modules..."));
    const bool want_sys = m_includeSys->isChecked();

    auto* watcher = new QFutureWatcher<std::vector<Entry>>(this);
    connect(watcher, &QFutureWatcher<std::vector<Entry>>::finished, this, [this, watcher] {
        m_entries = watcher->result();
        watcher->deleteLater();
        m_listing = false;
        populate();
    });
    watcher->setFuture(
        QtConcurrent::run([root = m_gameRoot, sys = m_sysDir, want_sys]() -> std::vector<Entry> {
            std::vector<Entry> out;
            if (!root.empty()) {
                const auto backend = Core::FileSys::OpenGameBackend(root);
                if (backend && backend->IsOpen() && backend->Exists("eboot.bin")) {
                    out.push_back({QStringLiteral("eboot.bin"), false, "eboot.bin", {}});
                }
                for (const auto& rel : Core::Analysis::ListGameModuleFiles(root)) {
                    out.push_back({QString::fromStdString(rel), false, rel, {}});
                }
            }
            if (want_sys) {
                std::error_code ec;
                for (const auto& f : Core::Analysis::FindSysModuleFiles(sys)) {
                    const std::string rel = std::filesystem::relative(f, sys, ec).generic_string();
                    out.push_back({QStringLiteral("sys_modules/") + QString::fromStdString(rel),
                                   true, rel, f});
                }
            }
            return out;
        }));
}

void ModulePickerDialog::populate() {
    m_table->setSortingEnabled(false);
    m_table->setRowCount(static_cast<int>(m_entries.size()));
    size_t game = 0, sys = 0;
    for (int row = 0; row < static_cast<int>(m_entries.size()); row++) {
        const auto& e = m_entries[static_cast<size_t>(row)];
        (e.from_sys_modules ? sys : game)++;
        auto* file = new QTableWidgetItem(e.display_path);
        file->setData(kEntryRole, row);
        m_table->setItem(row, ColFile, file);
        m_table->setItem(row, ColLocation,
                         new QTableWidgetItem(e.from_sys_modules ? tr("sys_modules") : tr("Game")));
    }
    QStringList parts;
    if (!m_gameRoot.empty()) {
        parts << tr("%n game file(s)", nullptr, static_cast<int>(game));
    }
    if (m_includeSys->isChecked()) {
        parts << (sys == 0 ? tr("no modules found in %1").arg(PathString(m_sysDir))
                           : tr("%n sys_modules file(s)", nullptr, static_cast<int>(sys)));
    }
    m_status->setText(parts.join(QStringLiteral(", ")));
    applyFilter();
    if (m_table->rowCount() > 0) {
        m_table->selectRow(0);
    }
}

void ModulePickerDialog::applyFilter() {
    const QString needle = m_filter->text().trimmed();
    for (int row = 0; row < m_table->rowCount(); row++) {
        const bool match =
            needle.isEmpty() ||
            m_table->item(row, ColFile)->text().contains(needle, Qt::CaseInsensitive);
        m_table->setRowHidden(row, !match);
    }
}

void ModulePickerDialog::openSelected() {
    const auto selected = m_table->selectionModel()->selectedRows(ColFile);
    if (selected.isEmpty()) {
        return;
    }
    const int index = m_table->item(selected.front().row(), ColFile)->data(kEntryRole).toInt();
    if (index < 0 || static_cast<size_t>(index) >= m_entries.size()) {
        return;
    }
    const auto& e = m_entries[static_cast<size_t>(index)];
    ModuleLocation location;
    location.display_path = e.display_path;
    if (e.from_sys_modules) {
        location.host_path = e.host_path;
    } else {
        location.game_root = m_gameRoot;
        location.rel_path = e.rel_path;
    }
    OpenModuleInElfInfo(parentWidget(), location, m_title, m_gameRoot, m_sysDir);
}

void ModulePickerDialog::browseForFile() {
    QString start = PathString(m_sysDir);
    if (!m_gameRoot.empty() && std::filesystem::is_directory(m_gameRoot)) {
        start = PathString(m_gameRoot);
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open a Module or Executable"), start,
        tr("PS5 executables and modules (*.bin *.prx *.sprx *.so *.elf *.self *.native);;"
           "All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    ModuleLocation location;
    location.host_path = Common::FS::PathFromQString(path);
    location.display_path = QString::fromStdString(location.host_path.filename().string());
    OpenModuleInElfInfo(parentWidget(), location, m_title, m_gameRoot, m_sysDir);
}
