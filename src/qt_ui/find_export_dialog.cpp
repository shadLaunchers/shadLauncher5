// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "find_export_dialog.h"

#include <fstream>
#include <iterator>
#include <QApplication>
#include <QCheckBox>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTableView>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>
#include "common/path_util.h"
#include "core/file_format/elf_info.h"
#include "core/file_sys/game_backend.h"
#include "elf_info_dialog.h"
#include "module_opener.h"
#include "nid_catalog_sync.h"

namespace {

enum Column { ColName, ColNid, ColSource, ColLibrary, ColAddress, ColSize, ColMatch, ColCount };
constexpr size_t kMaxShownHits = 5000;

QString S(const std::string& s) {
    return QString::fromStdString(s);
}

QString PathString(const std::filesystem::path& p) {
    QString out;
    Common::FS::PathToQString(out, p);
    return out;
}

} // namespace

// --- model ---------------------------------------------------------------

void ExportHitsModel::SetHits(std::vector<Core::Analysis::ExportHit> hits) {
    beginResetModel();
    m_hits = std::move(hits);
    endResetModel();
}

void ExportHitsModel::RefreshNames() {
    if (!m_hits.empty()) {
        emit dataChanged(index(0, ColName), index(rowCount() - 1, ColName));
    }
}

int ExportHitsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_hits.size());
}

int ExportHitsModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColCount;
}

QVariant ExportHitsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const auto& hit = m_hits[static_cast<size_t>(index.row())];
    const auto& e = hit.index->entries[hit.entry];
    const auto& src = hit.index->sources[e.source];
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColName: {
            const std::string name = Core::Analysis::ExportName(e);
            return name.empty() ? QObject::tr("(name unknown)") : S(name);
        }
        case ColNid:
            return S(e.nid);
        case ColSource:
            return S(src.display_path);
        case ColLibrary:
            return S(e.library);
        case ColAddress:
            return S(Loader::ElfInfo::Hex(e.address));
        case ColSize:
            return QString::number(e.size);
        case ColMatch:
            switch (hit.kind) {
            case Core::Analysis::MatchKind::ExactNid:
                return QObject::tr("NID");
            case Core::Analysis::MatchKind::HashedName:
                return QObject::tr("exact name");
            default:
                return QObject::tr("name contains");
            }
        default:
            return {};
        }
    }
    if (role == Qt::ForegroundRole && index.column() == ColName &&
        Core::Analysis::ExportName(e).empty()) {
        return QApplication::palette().color(QPalette::PlaceholderText);
    }
    if (role == Qt::ToolTipRole && index.column() == ColSource) {
        return src.from_sys_modules ? QObject::tr("From your sys_modules folder")
                                    : QObject::tr("Shipped with the game");
    }
    return {};
}

QVariant ExportHitsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case ColName:
        return tr("Function");
    case ColNid:
        return tr("NID");
    case ColSource:
        return tr("Provided by");
    case ColLibrary:
        return tr("Library");
    case ColAddress:
        return tr("Address");
    case ColSize:
        return tr("Size");
    case ColMatch:
        return tr("Match");
    default:
        return {};
    }
}

// --- dialog --------------------------------------------------------------

FindExportDialog::FindExportDialog(const QString& title, std::filesystem::path game_root,
                                   std::filesystem::path sys_modules_dir,
                                   const QString& initial_query, bool include_sys_modules,
                                   QWidget* parent)
    : QDialog(parent), m_title(title), m_gameRoot(std::move(game_root)),
      m_sysDir(std::move(sys_modules_dir)) {
    setWindowTitle(tr("Find Export - %1").arg(title));
    resize(980, 620);

    auto* layout = new QVBoxLayout(this);

    auto* search_row = new QHBoxLayout();
    m_query = new QLineEdit(this);
    m_query->setPlaceholderText(tr("Function name, part of a name, or NID..."));
    m_query->setClearButtonEnabled(true);
    m_query->setText(initial_query);
    m_includeSys = new QCheckBox(tr("Also search sys_modules"), this);
    m_includeSys->setToolTip(tr("Include the modules in %1").arg(PathString(m_sysDir)));
    m_includeSys->setChecked(include_sys_modules || m_gameRoot.empty());
    m_includeSys->setEnabled(!m_gameRoot.empty()); // sys-only search always includes it
    search_row->addWidget(m_query, 1);
    search_row->addWidget(m_includeSys);
    layout->addLayout(search_row);

    m_verdict = new QLabel(this);
    m_verdict->setWordWrap(true);
    m_verdict->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont verdict_font = m_verdict->font();
    verdict_font.setBold(true);
    m_verdict->setFont(verdict_font);
    layout->addWidget(m_verdict);

    m_model = new ExportHitsModel(this);
    m_view = new QTableView(this);
    m_view->setModel(m_model);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setAlternatingRowColors(true);
    m_view->setWordWrap(false);
    m_view->verticalHeader()->setVisible(false);
    m_view->verticalHeader()->setDefaultSectionSize(m_view->fontMetrics().height() + 6);
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_view->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_view->setToolTip(tr("Double-click a result to open ELF Info for that module."));
    layout->addWidget(m_view, 1);

    auto* footer = new QHBoxLayout();
    m_progress = new QProgressBar(this);
    m_progress->setMaximumWidth(220);
    m_progress->setTextVisible(false);
    m_progress->hide();
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    auto* modules_btn = new QPushButton(tr("Modules..."), this);
    modules_btn->setToolTip(tr("List every scanned file, whether its exports could be read, and "
                               "the module/library names it declares."));
    auto* close_btn = new QPushButton(tr("Close"), this);
    footer->addWidget(m_progress);
    footer->addWidget(m_status, 1);
    footer->addWidget(modules_btn);
    footer->addWidget(close_btn);
    connect(modules_btn, &QPushButton::clicked, this, &FindExportDialog::showModules);
    layout->addLayout(footer);

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(200);

    connect(m_query, &QLineEdit::textChanged, m_searchTimer, qOverload<>(&QTimer::start));
    connect(m_searchTimer, &QTimer::timeout, this, &FindExportDialog::runSearch);
    connect(m_includeSys, &QCheckBox::toggled, this, [this](bool on) {
        if (on && !m_sysIndex && !m_sysIndexing) {
            startSysIndex();
        }
        runSearch();
    });
    connect(m_view, &QTableView::doubleClicked, this, &FindExportDialog::onResultActivated);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
    // Names come from the NID catalog; refresh them when it changes.
    connect(NidCatalogEvents::Instance(), &NidCatalogEvents::CatalogChanged, this,
            &FindExportDialog::runSearch);

    if (!m_gameRoot.empty()) {
        startGameIndex();
    }
    if (m_includeSys->isChecked()) {
        startSysIndex();
    }
    updateStatus();
    m_query->setFocus();
}

FindExportDialog::~FindExportDialog() {
    m_cancel->store(true); // stop any indexing still running in the background
}

void FindExportDialog::startGameIndex() {
    m_gameIndexing = true;
    QPointer<FindExportDialog> self(this);
    Core::Analysis::IndexProgress progress;
    progress.cancel = m_cancel.get();
    progress.on_step = [self](const std::string& file, size_t done, size_t total) {
        const QString f = QString::fromStdString(file);
        QMetaObject::invokeMethod(
            qApp,
            [self, f, done, total] {
                if (!self) {
                    return;
                }
                self->m_progress->setRange(0, static_cast<int>(total));
                self->m_progress->setValue(static_cast<int>(done));
                self->m_progressText = tr("Indexing game: %1 (%2/%3)").arg(f).arg(done).arg(total);
                self->updateStatus();
            },
            Qt::QueuedConnection);
    };
    using Result = std::shared_ptr<const Core::Analysis::ExportIndex>;
    auto* watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher] {
        m_gameIndex = watcher->result();
        watcher->deleteLater();
        m_gameIndexing = false;
        updateStatus();
        runSearch();
    });
    watcher->setFuture(QtConcurrent::run([root = m_gameRoot, progress, cancel = m_cancel]() {
        (void)cancel; // keeps the flag alive while this runs
        return Result(std::make_shared<Core::Analysis::ExportIndex>(
            Core::Analysis::BuildGameExportIndex(root, progress)));
    }));
}

void FindExportDialog::startSysIndex() {
    m_sysIndexing = true;
    QPointer<FindExportDialog> self(this);
    Core::Analysis::IndexProgress progress;
    progress.cancel = m_cancel.get();
    progress.on_step = [self](const std::string& file, size_t done, size_t total) {
        const QString f = QString::fromStdString(file);
        QMetaObject::invokeMethod(
            qApp,
            [self, f, done, total] {
                if (!self) {
                    return;
                }
                self->m_progress->setRange(0, static_cast<int>(total));
                self->m_progress->setValue(static_cast<int>(done));
                self->m_progressText =
                    tr("Indexing sys_modules: %1 (%2/%3)").arg(f).arg(done).arg(total);
                self->updateStatus();
            },
            Qt::QueuedConnection);
    };
    using Result = std::shared_ptr<const Core::Analysis::ExportIndex>;
    auto* watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher] {
        m_sysIndex = watcher->result();
        watcher->deleteLater();
        m_sysIndexing = false;
        updateStatus();
        runSearch();
    });
    watcher->setFuture(QtConcurrent::run([dir = m_sysDir, progress, cancel = m_cancel]() {
        (void)cancel; // keeps the flag alive while this runs
        return Core::Analysis::GetSysModulesExportIndex(dir, progress);
    }));
}

void FindExportDialog::updateStatus() {
    const bool busy = m_gameIndexing || m_sysIndexing;
    m_progress->setVisible(busy);

    auto describe = [](const Core::Analysis::ExportIndex& index, const QString& what) {
        size_t unreadable = 0;
        QStringList problems;
        for (const auto& s : index.sources) {
            if (!s.readable) {
                unreadable++;
                problems << QStringLiteral("%1 - %2").arg(S(s.display_path), S(s.problem));
            }
        }
        QString text = tr("%1: %2 modules, %3 exports")
                           .arg(what)
                           .arg(index.sources.size())
                           .arg(index.entries.size());
        if (unreadable > 0) {
            text += tr(" (%1 unreadable)").arg(unreadable);
        }
        return std::make_pair(text, problems);
    };

    QStringList parts;
    QStringList problems;
    if (m_gameIndex) {
        auto [t, p] = describe(*m_gameIndex, tr("Game"));
        parts << t;
        problems << p;
    }
    if (m_sysIndex && m_includeSys->isChecked()) {
        if (m_sysIndex->sources.empty()) {
            parts << tr("sys_modules: no modules found in %1").arg(PathString(m_sysDir));
        } else {
            auto [t, p] = describe(*m_sysIndex, tr("sys_modules"));
            parts << t;
            problems << p;
        }
    }
    if (busy) {
        parts.prepend(m_progressText.isEmpty() ? tr("Indexing...") : m_progressText);
    }
    m_status->setText(parts.join(QStringLiteral("  -  ")));
    m_status->setToolTip(problems.isEmpty() ? QString()
                                            : tr("Modules whose exports couldn't be read:\n%1")
                                                  .arg(problems.join(QLatin1Char('\n'))));
}

void FindExportDialog::runSearch() {
    std::vector<const Core::Analysis::ExportIndex*> indexes;
    if (m_gameIndex) {
        indexes.push_back(m_gameIndex.get());
    }
    if (m_sysIndex && m_includeSys->isChecked()) {
        indexes.push_back(m_sysIndex.get());
    }

    const QString query = m_query->text().trimmed();
    size_t total = 0;
    auto hits = Core::Analysis::SearchExports(indexes, query.toStdString(), kMaxShownHits, &total);

    // One-line answer for the "who provides this?" case: an exact NID or an
    // exactly spelled function name.
    QStringList providers;
    bool any_exact = false;
    for (const auto& h : hits) {
        if (h.kind == Core::Analysis::MatchKind::NameContains) {
            continue;
        }
        any_exact = true;
        const QString p = S(h.index->sources[h.index->entries[h.entry].source].display_path);
        if (!providers.contains(p)) {
            providers << p;
        }
    }
    const bool still_indexing = m_gameIndexing || (m_sysIndexing && m_includeSys->isChecked());
    const QString scope = m_gameRoot.empty()          ? tr("sys_modules")
                          : m_includeSys->isChecked() ? tr("this game or sys_modules")
                                                      : tr("this game");
    if (query.isEmpty()) {
        m_verdict->setText(tr("Type a function name, part of one, or a NID."));
    } else if (any_exact) {
        m_verdict->setText(providers.size() == 1 ? tr("Provided by %1").arg(providers.front())
                                                 : tr("Exported by %1 modules: %2")
                                                       .arg(providers.size())
                                                       .arg(providers.join(QStringLiteral(", "))));
    } else if (hits.empty()) {
        m_verdict->setText(still_indexing ? tr("No match yet - still indexing...")
                                          : tr("Not exported by any module in %1.").arg(scope));
    } else {
        m_verdict->setText(total > hits.size()
                               ? tr("%1 matches (showing the first %2)").arg(total).arg(hits.size())
                               : tr("%n match(es)", nullptr, static_cast<int>(hits.size())));
    }

    m_model->SetHits(std::move(hits));
}

void FindExportDialog::showModules() {
    std::vector<const Core::Analysis::ExportSource*> sources;
    if (m_gameIndex) {
        for (const auto& src : m_gameIndex->sources) {
            sources.push_back(&src);
        }
    }
    if (m_sysIndex && m_includeSys->isChecked()) {
        for (const auto& src : m_sysIndex->sources) {
            sources.push_back(&src);
        }
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Scanned Modules"));
    dialog.resize(900, 520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* note = new QLabel(
        sources.empty()
            ? tr("Nothing has been indexed yet.")
            : tr("Files that start with an ELF/SELF header. A module whose exports can't be read "
                 "is usually still encrypted - only decrypted dumps can be searched."),
        &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* table = new QTableWidget(static_cast<int>(sources.size()), 5, &dialog);
    table->setHorizontalHeaderLabels(
        {tr("File"), tr("Status"), tr("Exports"), tr("Declares module"), tr("Declares libraries")});
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    auto join = [](const std::vector<std::string>& v) {
        QStringList out;
        for (const auto& x : v) {
            out << S(x);
        }
        return out.join(QStringLiteral(", "));
    };
    for (int row = 0; row < static_cast<int>(sources.size()); row++) {
        const auto& src = *sources[static_cast<size_t>(row)];
        table->setItem(row, 0, new QTableWidgetItem(S(src.display_path)));
        auto* status = new QTableWidgetItem(src.readable ? tr("OK") : S(src.problem));
        if (!src.readable) {
            status->setForeground(QApplication::palette().color(QPalette::PlaceholderText));
        }
        table->setItem(row, 1, status);
        auto* count = new QTableWidgetItem(QString::number(src.export_count));
        count->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table->setItem(row, 2, count);
        table->setItem(row, 3, new QTableWidgetItem(join(src.declared_modules)));
        table->setItem(row, 4, new QTableWidgetItem(join(src.declared_libraries)));
    }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder); // keep scan order
    table->setSortingEnabled(true);
    layout->addWidget(table, 1);

    auto* close = new QPushButton(tr("Close"), &dialog);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    auto* row = new QHBoxLayout();
    row->addStretch();
    row->addWidget(close);
    layout->addLayout(row);
    dialog.exec();
}

void FindExportDialog::onResultActivated(const QModelIndex& index) {
    if (index.isValid()) {
        openSourceAt(m_model->HitAt(index.row()));
    }
}

void FindExportDialog::openSourceAt(const Core::Analysis::ExportHit& hit) {
    const auto& entry = hit.index->entries[hit.entry];
    const auto& src = hit.index->sources[entry.source];
    ModuleLocation location;
    location.display_path = S(src.display_path);
    if (src.from_sys_modules) {
        location.host_path = src.host_path;
    } else {
        location.game_root = m_gameRoot;
        location.rel_path = src.rel_path;
    }
    OpenModuleInElfInfo(parentWidget(), location, m_title, m_gameRoot, m_sysDir, entry.nid);
}
