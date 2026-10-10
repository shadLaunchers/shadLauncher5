// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "elf_info_dialog.h"

#include "core/analysis/sdk_metadata.h"
#include "core/file_format/nid_catalog.h"
#include "find_export_dialog.h"
#include "nid_catalog_sync.h"
#include "strings_dialog.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStringList>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace {

constexpr int kNodeKindRole = Qt::UserRole;
constexpr int kNodeIndexRole = Qt::UserRole + 1;
constexpr int kNodeGroupRole = Qt::UserRole + 2;
constexpr int kPopulatedRole = Qt::UserRole + 3;
constexpr int kNodeSubIndexRole = Qt::UserRole + 4;

QString S(const std::string& s) {
    return QString::fromStdString(s);
}

} // namespace

ElfInfoDialog::ElfInfoDialog(const QString& title, const Loader::ElfInfo::ParsedInfo& info,
                             const QString& suggestedFileName, QWidget* parent,
                             std::shared_ptr<const std::vector<u8>> raw, const QString& gameTag)
    : QDialog(parent), m_info(info), m_suggestedFileName(suggestedFileName), m_raw(std::move(raw)),
      m_gameTag(gameTag.isEmpty() ? title : gameTag) {
    m_text = FullText();
    setWindowTitle(tr("ELF Info - %1").arg(title));
    resize(900, 640);

    auto* layout = new QVBoxLayout(this);
    m_formatLabel = new QLabel(this);
    QFont format_font = m_formatLabel->font();
    format_font.setBold(true);
    m_formatLabel->setFont(format_font);

    QString banner;
    const QString platform =
        m_info.is_valid_elf ? S(Loader::ElfInfo::PlatformName(m_info)) : QStringLiteral("PS4/PS5");
    if (m_info.is_self) {
        banner = tr("SELF-wrapped %1 ").arg(platform);
    } else if (m_info.is_valid_elf) {
        banner = tr("Plain (not SELF-wrapped) %1 ").arg(platform);
    }

    if (m_info.is_valid_elf) {
        const auto module_class = Loader::ElfInfo::ClassifyModule(m_info);
        switch (module_class.kind) {
        case Loader::ElfInfo::ModuleKind::SharedModule:
            banner += tr("shared module (PRX/SPRX)");
            if (!module_class.so_name.empty()) {
                banner += QStringLiteral(" - ") + S(module_class.so_name);
            }
            break;
        case Loader::ElfInfo::ModuleKind::Executable:
            banner += tr("main executable");
            break;
        case Loader::ElfInfo::ModuleKind::Unknown:
        default:
            banner += tr("file (couldn't tell executable from PRX/SPRX - see ELF "
                         "Header for details)");
            break;
        }
        m_formatLabel->setText(banner);
    } else {
        m_formatLabel->setText(
            tr("Not recognized as SELF or ELF - this may not be a valid eboot.bin"));
        m_formatLabel->setStyleSheet(QStringLiteral("color: palette(bright-text); "
                                                    "background-color: palette(mid);"));
    }
    layout->addWidget(m_formatLabel);

    auto* tabs = new QTabWidget(this);
    layout->addWidget(tabs);

    // --- Structured tab: tree on the left, field/value detail on the right ---
    auto* structured = new QWidget(tabs);
    auto* structured_layout = new QVBoxLayout(structured);
    structured_layout->setContentsMargins(0, 0, 0, 0);

    auto* splitter = new QSplitter(Qt::Horizontal, structured);

    m_tree = new QTreeWidget(splitter);
    m_tree->setHeaderHidden(true);
    m_tree->setMinimumWidth(260);
    splitter->addWidget(m_tree);

    auto* detail_panel = new QWidget(splitter);
    auto* detail_layout = new QVBoxLayout(detail_panel);

    m_detailTitle = new QLabel(detail_panel);
    QFont title_font = m_detailTitle->font();
    title_font.setBold(true);
    title_font.setPointSize(title_font.pointSize() + 1);
    m_detailTitle->setFont(title_font);
    detail_layout->addWidget(m_detailTitle);

    m_detailNote = new QLabel(detail_panel);
    m_detailNote->setWordWrap(true);
    m_detailNote->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_detailNote->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_detailNote->setStyleSheet(QStringLiteral("color: palette(mid);"));
    m_detailNote->hide();
    detail_layout->addWidget(m_detailNote);

    m_detailTable = new QTableWidget(0, 2, detail_panel);
    m_detailTable->setHorizontalHeaderLabels({tr("Field"), tr("Value")});
    m_detailTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_detailTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_detailTable->verticalHeader()->setVisible(false);
    m_detailTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_detailTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_detailTable->setAlternatingRowColors(true);
    detail_layout->addWidget(m_detailTable, /*stretch=*/1); // spare height goes here, not the note

    // Shown instead of m_detailTable for hex-dump nodes - a field/value
    // table doesn't suit a multi-line, fixed-width dump.
    m_detailHexView = new QPlainTextEdit(detail_panel);
    m_detailHexView->setReadOnly(true);
    m_detailHexView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_detailHexView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_detailHexView->hide();
    detail_layout->addWidget(m_detailHexView,
                             /*stretch=*/1); // spare height goes here, not the note

    splitter->addWidget(detail_panel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({280, 620});

    structured_layout->addWidget(splitter);
    tabs->addTab(structured, tr("Structured"));

    // --- Raw Text tab: the flat dump, for anyone who just wants everything ---
    m_rawView = new QPlainTextEdit(tabs);
    m_rawView->setReadOnly(true);
    m_rawView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_rawView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_rawView->setPlainText(m_text);
    tabs->addTab(m_rawView, tr("Raw Text"));

    auto* buttons = new QHBoxLayout();
    auto* copy_btn = new QPushButton(tr("Copy to Clipboard"), this);
    auto* save_btn = new QPushButton(tr("Save to File..."), this);
    auto* close_btn = new QPushButton(tr("Close"), this);
    buttons->addWidget(copy_btn);
    buttons->addWidget(save_btn);
    if (!m_info.dynamic.symbols.empty()) {
        auto* load_nid_db_btn = new QPushButton(tr("Load NID Database..."), this);
        buttons->addWidget(load_nid_db_btn);
        connect(load_nid_db_btn, &QPushButton::clicked, this, &ElfInfoDialog::onLoadNidDatabase);
        auto* sync_nid_btn = new QPushButton(tr("Sync NID Catalog"), this);
        sync_nid_btn->setToolTip(tr("Download the latest community NID catalog and apply it "
                                    "to these symbols.") +
                                 QLatin1Char('\n') + NidCatalogSync::StatusText());
        buttons->addWidget(sync_nid_btn);
        connect(sync_nid_btn, &QPushButton::clicked, this,
                [this] { NidCatalogSync::RunWithProgress(this); });
        connect(NidCatalogEvents::Instance(), &NidCatalogEvents::CatalogChanged, sync_nid_btn,
                [sync_nid_btn] {
                    sync_nid_btn->setToolTip(
                        tr("Download the latest community NID catalog and apply it to these "
                           "symbols.") +
                        QLatin1Char('\n') + NidCatalogSync::StatusText());
                });

        m_exportUnresolvedBtn = new QPushButton(tr("Export Unresolved NIDs..."), this);
        m_exportUnresolvedBtn->setToolTip(
            tr("Save imports whose NID has no known name as CSV (library,nid,count,games) - "
               "the format used for community catalog submissions."));
        m_exportUnresolvedBtn->setEnabled(UnresolvedImportCount() > 0);
        buttons->addWidget(m_exportUnresolvedBtn);
        connect(m_exportUnresolvedBtn, &QPushButton::clicked, this,
                &ElfInfoDialog::onExportUnresolvedNids);
    }
    if (m_raw && m_info.is_self && m_info.is_valid_elf) {
        auto* extract_btn = new QPushButton(tr("Extract ELF..."), this);
        extract_btn->setToolTip(tr("Rebuild a standalone ELF from this SELF container."));
        buttons->addWidget(extract_btn);
        connect(extract_btn, &QPushButton::clicked, this, &ElfInfoDialog::onExtractElf);
    }
    if (m_raw) {
        auto* strings_btn = new QPushButton(tr("Strings..."), this);
        buttons->addWidget(strings_btn);
        connect(strings_btn, &QPushButton::clicked, this, &ElfInfoDialog::onShowStrings);
    }
    buttons->addStretch();
    buttons->addWidget(close_btn);
    layout->addLayout(buttons);

    connect(copy_btn, &QPushButton::clicked, this, &ElfInfoDialog::onCopy);
    connect(save_btn, &QPushButton::clicked, this, &ElfInfoDialog::onSave);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &ElfInfoDialog::onTreeSelectionChanged);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &ElfInfoDialog::onItemExpanded);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this,
            &ElfInfoDialog::onTreeContextMenu);
    connect(NidCatalogEvents::Instance(), &NidCatalogEvents::CatalogChanged, this,
            &ElfInfoDialog::RefreshSymbolResolution);

    buildTree();
}

void ElfInfoDialog::addTreeNode(QTreeWidgetItem* parent, const QString& label, NodeKind kind,
                                int index, int group) {
    auto* item = new QTreeWidgetItem(parent, {label});
    item->setData(0, kNodeKindRole, static_cast<int>(kind));
    item->setData(0, kNodeIndexRole, index);
    item->setData(0, kNodeGroupRole, group);
}

void ElfInfoDialog::buildTree() {
    m_tree->clear();

    if (m_info.is_self) {
        addLazyGroup(tr("SELF Wrapper"), NodeKind::SelfWrapper, 0, m_info.self_segments.size());
        if (m_info.self_segments.empty()) {
            auto* self_root = new QTreeWidgetItem(m_tree, {tr("SELF Wrapper")});
            self_root->setData(0, kNodeKindRole, static_cast<int>(NodeKind::SelfWrapper));
            self_root->setData(0, kNodeIndexRole, -1);
            self_root->setData(0, kPopulatedRole, true); // nothing to lazily populate
        }
    }

    if (m_info.is_valid_elf) {
        auto* ehdr_item = new QTreeWidgetItem(m_tree, {tr("ELF Header")});
        ehdr_item->setData(0, kNodeKindRole, static_cast<int>(NodeKind::ElfHeader));
        ehdr_item->setData(0, kNodeIndexRole, -1);

        if (Loader::ElfInfo::GetTlsSummary(m_info).present) {
            auto* tls_item = new QTreeWidgetItem(m_tree, {tr("TLS (Thread-Local Storage)")});
            tls_item->setData(0, kNodeKindRole, static_cast<int>(NodeKind::TlsInfo));
            tls_item->setData(0, kNodeIndexRole, -1);
        }

        if (m_info.lib_versions_present) {
            if (m_info.lib_versions.empty()) {
                auto* item = new QTreeWidgetItem(m_tree, {tr("SDK Library Versions")});
                item->setData(0, kNodeKindRole, static_cast<int>(NodeKind::LibVersionGroup));
                item->setData(0, kNodeIndexRole, -1);
                item->setData(0, kPopulatedRole, true);
            } else {
                addLazyGroup(tr("SDK Library Versions"), NodeKind::LibVersionGroup, 0,
                             m_info.lib_versions.size());
            }
        }

        addLazyGroup(tr("Program Headers"), NodeKind::ProgramHeadersRoot, 0, m_info.phdrs.size());
        addLazyGroup(tr("Section Headers"), NodeKind::SectionHeadersRoot, 0, m_info.shdrs.size());

        if (m_info.dynamic.present) {
            auto* dyn_item = new QTreeWidgetItem(m_tree, {tr("Dynamic Section")});
            dyn_item->setData(0, kNodeKindRole, static_cast<int>(NodeKind::DynamicSection));
            dyn_item->setData(0, kNodeIndexRole, -1);

            if (m_info.dynamic.readable && !m_info.dynamic.summary.empty()) {
                auto* sum_item = new QTreeWidgetItem(m_tree, {tr("Dynamic Linking Summary")});
                sum_item->setData(0, kNodeKindRole, static_cast<int>(NodeKind::DynamicSummary));
                sum_item->setData(0, kNodeIndexRole, -1);
            }
        }

        const auto& dyn = m_info.dynamic;
        addLazyGroup(tr("Imported Modules"), NodeKind::ModuleGroup, 0, dyn.import_modules.size());
        addLazyGroup(tr("Exported Modules"), NodeKind::ModuleGroup, 1, dyn.export_modules.size());
        addLazyGroup(tr("Imported Libraries"), NodeKind::LibraryGroup, 0, dyn.import_libs.size());
        addLazyGroup(tr("Exported Libraries"), NodeKind::LibraryGroup, 1, dyn.export_libs.size());
        addLazyGroup(tr("Imported Symbols"), NodeKind::SymbolGroup, 0, SymbolIndices(0).size());
        addLazyGroup(tr("Exported Symbols"), NodeKind::SymbolGroup, 1, SymbolIndices(1).size());
        addLazyGroup(tr("PLT Relocations"), NodeKind::RelocationGroup, 0,
                     RelocationIndices(0).size());
        addLazyGroup(tr("RELA Relocations"), NodeKind::RelocationGroup, 1,
                     RelocationIndices(1).size());
    }

    if (!m_info.is_valid_elf) {
        if (m_info.is_self) {
            auto* item = new QTreeWidgetItem(m_tree, {tr("Raw Bytes at ELF Offset (hex)")});
            item->setData(0, kNodeKindRole, static_cast<int>(NodeKind::RawEhdrOffsetDump));
            item->setData(0, kNodeIndexRole, -1);
        } else {
            auto* item = new QTreeWidgetItem(m_tree, {tr("Raw File Header (hex)")});
            item->setData(0, kNodeKindRole, static_cast<int>(NodeKind::RawFileHeaderDump));
            item->setData(0, kNodeIndexRole, -1);
        }
    }

    if (m_tree->topLevelItemCount() > 0) {
        m_tree->setCurrentItem(m_tree->topLevelItem(0));
    } else {
        showDetail(tr("Nothing to show"), tr("The file is too small to inspect."), {});
    }
}

QTreeWidgetItem* ElfInfoDialog::addLazyGroup(const QString& label, NodeKind root_kind, int group,
                                             size_t count) {
    if (count == 0) {
        return nullptr;
    }
    auto* root = new QTreeWidgetItem(m_tree, {tr("%1 (%2)").arg(label).arg(count)});
    root->setData(0, kNodeKindRole, static_cast<int>(root_kind));
    root->setData(0, kNodeIndexRole, -1);
    root->setData(0, kNodeGroupRole, group);
    root->setData(0, kPopulatedRole, false);
    new QTreeWidgetItem(root, {tr("Loading...")});
    return root;
}

void ElfInfoDialog::onItemExpanded(QTreeWidgetItem* item) {
    populateGroupChildren(item);
}

void ElfInfoDialog::populateGroupChildren(QTreeWidgetItem* root) {
    if (root == nullptr || root->data(0, kPopulatedRole).toBool()) {
        return;
    }
    root->setData(0, kPopulatedRole, true);
    while (root->childCount() > 0) {
        delete root->takeChild(0);
    }

    const auto kind = static_cast<NodeKind>(root->data(0, kNodeKindRole).toInt());
    const int group = root->data(0, kNodeGroupRole).toInt();
    const auto& dyn = m_info.dynamic;

    switch (kind) {
    case NodeKind::SelfWrapper:
        for (size_t i = 0; i < m_info.self_segments.size(); i++) {
            addTreeNode(root, tr("Segment [%1]").arg(i), NodeKind::SelfSegment,
                        static_cast<int>(i));
        }
        break;
    case NodeKind::LibVersionGroup:
        for (size_t i = 0; i < m_info.lib_versions.size(); i++) {
            const auto& e = m_info.lib_versions[i];
            addTreeNode(root, S(e.name) + QStringLiteral("  ") + S(e.GuessedVersionString()),
                        NodeKind::LibVersionEntry, static_cast<int>(i));
        }
        break;
    case NodeKind::ProgramHeadersRoot:
        for (size_t i = 0; i < m_info.phdrs.size(); i++) {
            const QString type_name = S(Loader::ElfInfo::PhdrTypeName(m_info.phdrs[i].p_type));
            addTreeNode(root, tr("[%1] %2").arg(i).arg(type_name), NodeKind::ProgramHeader,
                        static_cast<int>(i));
        }
        break;
    case NodeKind::SectionHeadersRoot:
        for (size_t i = 0; i < m_info.shdrs.size(); i++) {
            const QString name =
                m_info.section_names[i].empty() ? tr("<unnamed>") : S(m_info.section_names[i]);
            addTreeNode(root, tr("[%1] %2").arg(i).arg(name), NodeKind::SectionHeader,
                        static_cast<int>(i));
        }
        break;
    case NodeKind::ModuleGroup: {
        const auto& v = group == 0 ? dyn.import_modules : dyn.export_modules;
        for (size_t i = 0; i < v.size(); i++) {
            addTreeNode(root, S(v[i].name), NodeKind::ModuleEntry, static_cast<int>(i), group);
        }
        break;
    }
    case NodeKind::LibraryGroup: {
        const auto& v = group == 0 ? dyn.import_libs : dyn.export_libs;
        for (size_t i = 0; i < v.size(); i++) {
            addTreeNode(root, S(v[i].name), NodeKind::LibraryEntry, static_cast<int>(i), group);
        }
        break;
    }
    case NodeKind::SymbolGroup: {
        if (group == 0) {
            // Imports: category -> library -> symbol (e.g. kernel -> libc -> malloc).
            const auto& cats = Categories();
            for (size_t c = 0; c < cats.size(); c++) {
                auto* cat_item = new QTreeWidgetItem(
                    root, {tr("%1 (%2)").arg(cats[c].name).arg(cats[c].import_count)});
                cat_item->setData(0, kNodeKindRole, static_cast<int>(NodeKind::CategoryEntry));
                cat_item->setData(0, kNodeIndexRole, static_cast<int>(c));
                cat_item->setData(0, kNodeGroupRole, 0);
                // Filled right here, not lazily - keep onItemExpanded from clearing it.
                cat_item->setData(0, kPopulatedRole, true);
                for (size_t l = 0; l < cats[c].libraries.size(); l++) {
                    const auto& lib = cats[c].libraries[l];
                    auto* lib_item = new QTreeWidgetItem(
                        cat_item, {tr("%1 (%2)").arg(lib.name).arg(lib.positions.size())});
                    lib_item->setData(0, kNodeKindRole,
                                      static_cast<int>(NodeKind::CategoryLibrary));
                    lib_item->setData(0, kNodeIndexRole, static_cast<int>(l));
                    lib_item->setData(0, kNodeGroupRole, static_cast<int>(c));
                    lib_item->setData(0, kPopulatedRole, true);
                    addSymbolLeaves(lib_item, 0, lib.positions);
                }
            }
            break;
        }
        const auto& modules = SymbolModules(group);
        for (size_t m = 0; m < modules.size(); m++) {
            const auto& mod = modules[m];
            const QString label = mod.version.isEmpty()
                                      ? tr("%1 (%2)").arg(mod.name).arg(mod.count)
                                      : tr("%1 %2 (%3)").arg(mod.name, mod.version).arg(mod.count);
            auto* mod_item = new QTreeWidgetItem(root, {label});
            mod_item->setData(0, kNodeKindRole, static_cast<int>(NodeKind::SymbolModule));
            mod_item->setData(0, kNodeIndexRole, static_cast<int>(m));
            mod_item->setData(0, kNodeGroupRole, group);
            // Filled right here, not lazily - keep onItemExpanded from clearing it.
            mod_item->setData(0, kPopulatedRole, true);
            if (mod.libraries.size() == 1) {
                addSymbolLeaves(mod_item, group, mod.libraries.front().positions);
                continue;
            }
            for (size_t l = 0; l < mod.libraries.size(); l++) {
                const auto& lib = mod.libraries[l];
                auto* lib_item = new QTreeWidgetItem(
                    mod_item, {tr("%1 (%2)").arg(lib.name).arg(lib.positions.size())});
                lib_item->setData(0, kNodeKindRole, static_cast<int>(NodeKind::SymbolLibrary));
                lib_item->setData(0, kNodeIndexRole, static_cast<int>(m));
                lib_item->setData(0, kNodeGroupRole, group);
                lib_item->setData(0, kNodeSubIndexRole, static_cast<int>(l));
                lib_item->setData(0, kPopulatedRole, true);
                addSymbolLeaves(lib_item, group, lib.positions);
            }
        }
        break;
    }
    case NodeKind::RelocationGroup: {
        const auto& indices = RelocationIndices(group);
        for (size_t i = 0; i < indices.size(); i++) {
            const auto& r = dyn.relocations[static_cast<size_t>(indices[i])];
            const QString type = S(Loader::ElfInfo::RelocTypeName(r.type));
            const QString label = S(Loader::ElfInfo::Hex(r.offset)) + QStringLiteral(" ") +
                                  (type.isEmpty() ? tr("unsupported (%1)").arg(r.type) : type);
            addTreeNode(root, label, NodeKind::RelocationEntry, static_cast<int>(i), group);
        }
        break;
    }
    default:
        break; // not a group kind; nothing to populate
    }
}

void ElfInfoDialog::showDetail(const QString& sectionTitle, const QString& note,
                               const FieldList& fields) {
    m_detailTitle->setText(sectionTitle);

    if (note.isEmpty()) {
        m_detailNote->hide();
    } else {
        m_detailNote->setText(note);
        FitDetailNote();
        m_detailNote->show();
    }

    m_detailHexView->hide();
    m_detailTable->show();

    m_detailTable->setRowCount(static_cast<int>(fields.size()));
    for (int row = 0; row < static_cast<int>(fields.size()); row++) {
        auto* field_item = new QTableWidgetItem(fields[row].first);
        field_item->setFlags(field_item->flags() & ~Qt::ItemIsEditable);
        auto* value_item = new QTableWidgetItem(fields[row].second);
        value_item->setFlags(value_item->flags() & ~Qt::ItemIsEditable);
        m_detailTable->setItem(row, 0, field_item);
        m_detailTable->setItem(row, 1, value_item);
    }
}

void ElfInfoDialog::showDetailHex(const QString& sectionTitle, const QString& note,
                                  const QString& hexText) {
    m_detailTitle->setText(sectionTitle);

    if (note.isEmpty()) {
        m_detailNote->hide();
    } else {
        m_detailNote->setText(note);
        FitDetailNote();
        m_detailNote->show();
    }

    m_detailTable->hide();
    m_detailHexView->setPlainText(hexText);
    m_detailHexView->show();
}

void ElfInfoDialog::showDetailForItem(QTreeWidgetItem* item) {
    if (item == nullptr) {
        showDetail(QString(), QString(), {});
        return;
    }

    const auto kind = static_cast<NodeKind>(item->data(0, kNodeKindRole).toInt());
    const int index = item->data(0, kNodeIndexRole).toInt();
    const int group = item->data(0, kNodeGroupRole).toInt();

    switch (kind) {
    case NodeKind::SelfWrapper:
        showDetail(tr("SELF Wrapper"),
                   tr("Segment payload bytes aren't decoded here - SELF's segment "
                      "compression is a proprietary, undocumented Sony format. This "
                      "shows only what the segment directory itself states."),
                   SelfWrapperFields());
        break;
    case NodeKind::SelfSegment:
        showDetail(tr("SELF Segment [%1]").arg(index), QString(), SelfSegmentFields(index));
        break;
    case NodeKind::ElfHeader:
        showDetail(tr("ELF Header"), QString(), ElfHeaderFields());
        break;
    case NodeKind::ProgramHeadersRoot:
        showDetail(tr("Program Headers"), QString(), ProgramHeadersSummaryFields());
        break;
    case NodeKind::ProgramHeader:
        showDetail(tr("Program Header [%1]").arg(index), QString(), ProgramHeaderFields(index));
        break;
    case NodeKind::SectionHeadersRoot:
        showDetail(tr("Section Headers"), QString(), SectionHeadersSummaryFields());
        break;
    case NodeKind::SectionHeader:
        showDetail(tr("Section Header [%1]").arg(index), QString(), SectionHeaderFields(index));
        break;
    case NodeKind::DynamicSection:
        showDetail(
            tr("Dynamic Section"),
            m_info.dynamic.readable
                ? QString()
                : tr("Present, but not readable: %1").arg(S(m_info.dynamic.unavailable_reason)),
            DynamicSectionFields());
        break;
    case NodeKind::DynamicSummary:
        showDetail(tr("Dynamic Linking Summary"),
                   m_info.dynamic.strings_unavailable_reason.empty()
                       ? QString()
                       : tr("Names and libraries couldn't be resolved: %1")
                             .arg(S(m_info.dynamic.strings_unavailable_reason)),
                   DynamicSummaryFields());
        break;
    case NodeKind::ModuleGroup:
        showDetail(group == 0 ? tr("Imported Modules") : tr("Exported Modules"), QString(),
                   ModuleGroupFields(group));
        break;
    case NodeKind::ModuleEntry:
        showDetail(tr("Module"), QString(), ModuleEntryFields(group, index));
        break;
    case NodeKind::LibraryGroup:
        showDetail(group == 0 ? tr("Imported Libraries") : tr("Exported Libraries"), QString(),
                   LibraryGroupFields(group));
        break;
    case NodeKind::LibraryEntry:
        showDetail(tr("Library"), QString(), LibraryEntryFields(group, index));
        break;
    case NodeKind::SymbolGroup:
        if (group == 0) {
            FieldList fields = {{tr("Count"), QString::number(SymbolIndices(0).size())}};
            const FieldList cats = CategoryGroupFields();
            fields.insert(fields.end(), cats.begin(), cats.end());
            showDetail(tr("Imported Symbols"),
                       tr("Grouped by subsystem, then library. Names are NIDs - hashes of the "
                          "real function names - until a NID database resolves them. Prototypes "
                          "come from a small built-in set of unverified signatures."),
                       fields);
        } else {
            showDetail(tr("Exported Symbols"),
                       tr("Grouped by module, then by library when a module has several."),
                       {{tr("Count"), QString::number(SymbolIndices(1).size())},
                        {tr("Modules"), QString::number(SymbolModules(1).size())}});
        }
        break;
    case NodeKind::SymbolEntry:
        showDetail(tr("Symbol"), QString(), SymbolEntryFields(group, index));
        break;
    case NodeKind::SymbolModule:
        if (index >= 0 && static_cast<size_t>(index) < SymbolModules(group).size()) {
            showDetail(tr("Module: %1").arg(SymbolModules(group)[static_cast<size_t>(index)].name),
                       QString(), SymbolModuleFields(group, index));
        }
        break;
    case NodeKind::SymbolLibrary: {
        const int lib_index = item->data(0, kNodeSubIndexRole).toInt();
        const auto& modules = SymbolModules(group);
        if (index >= 0 && static_cast<size_t>(index) < modules.size() && lib_index >= 0 &&
            static_cast<size_t>(lib_index) < modules[static_cast<size_t>(index)].libraries.size()) {
            const auto& lib =
                modules[static_cast<size_t>(index)].libraries[static_cast<size_t>(lib_index)];
            showDetail(tr("Library: %1").arg(lib.name), QString(),
                       {{tr("Module"), modules[static_cast<size_t>(index)].name},
                        {tr("Symbols"), QString::number(lib.positions.size())}});
        }
        break;
    }
    case NodeKind::TlsInfo:
        showDetail(tr("TLS (Thread-Local Storage)"), QString(), TlsInfoFields());
        break;
    case NodeKind::LibVersionGroup:
        showDetail(tr("SDK Library Versions"),
                   m_info.lib_versions_unavailable_reason.empty()
                       ? tr("From the PT_SCE_LIBVERSION segment: the SDK version each library "
                            "was built against. The major.minor.patch split follows the PS4 "
                            "encoding and is a best guess on PS5 - the raw value is shown too.")
                       : tr("Present, but not readable: %1")
                             .arg(S(m_info.lib_versions_unavailable_reason)),
                   LibVersionGroupFields());
        break;
    case NodeKind::LibVersionEntry:
        showDetail(tr("Library Version"), QString(), LibVersionEntryFields(index));
        break;
    case NodeKind::CategoryEntry:
        if (index >= 0 && static_cast<size_t>(index) < Categories().size()) {
            showDetail(tr("Category: %1").arg(Categories()[static_cast<size_t>(index)].name),
                       QString(), CategoryEntryFields(index));
        }
        break;
    case NodeKind::CategoryLibrary:
        if (group >= 0 && static_cast<size_t>(group) < Categories().size()) {
            const auto& cat = Categories()[static_cast<size_t>(group)];
            if (index >= 0 && static_cast<size_t>(index) < cat.libraries.size()) {
                showDetail(cat.libraries[static_cast<size_t>(index)].name,
                           tr("Imported functions, with their prototype where one is known "
                              "(otherwise their NID)."),
                           CategoryLibraryFields(group, index));
            }
        }
        break;
    case NodeKind::RelocationGroup:
        showDetail(group == 0 ? tr("PLT Relocations") : tr("RELA Relocations"), QString(),
                   RelocationGroupFields(group));
        break;
    case NodeKind::RelocationEntry:
        showDetail(tr("Relocation"), QString(), RelocationEntryFields(group, index));
        break;
    case NodeKind::RawFileHeaderDump:
        showDetailHex(tr("Raw File Header"),
                      tr("Neither the SELF wrapper magic nor the ELF magic (\\x7fELF) was found "
                         "anywhere this looked. This may not be a PS4/PS5 executable at all - "
                         "here are the first %1 bytes of the file (size: %2 bytes total), so you "
                         "can identify the actual format by eye.")
                          .arg(m_info.raw_header.size())
                          .arg(m_info.file_size),
                      S(Loader::ElfInfo::HexDump(m_info.raw_header)));
        break;
    case NodeKind::RawEhdrOffsetDump:
        showDetailHex(
            tr("Raw Bytes at ELF Offset"),
            tr("The SELF wrapper was recognized, but the bytes at offset %1 (where its "
               "declared header + segment directory say the ELF header should start) "
               "aren't a valid ELF header - could be a different container format "
               "nested inside, an unsupported SELF variant, or a corrupted file.")
                .arg(S(Loader::ElfInfo::Hex(m_info.ehdr_file_offset))),
            S(Loader::ElfInfo::HexDump(m_info.raw_at_ehdr_offset, m_info.ehdr_file_offset)));
        break;
    }
}

void ElfInfoDialog::FitDetailNote() {
    // Let the layout size the word-wrapped note through height-for-width; the
    // table below has the stretch, so the note only ever gets the height its
    // text needs. (Pinning the height by hand misbehaved on some platforms.)
    m_detailNote->setMinimumHeight(0);
    m_detailNote->setMaximumHeight(QWIDGETSIZE_MAX);
    m_detailNote->updateGeometry();
}

void ElfInfoDialog::SetGameContext(std::filesystem::path game_root,
                                   std::filesystem::path sys_modules_dir,
                                   const QString& game_title) {
    m_gameRoot = std::move(game_root);
    m_sysModulesDir = std::move(sys_modules_dir);
    m_gameTitle = game_title;
}

void ElfInfoDialog::SelectSymbol(const std::string& nid, bool exported) {
    const int group = exported ? 1 : 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); i++) {
        QTreeWidgetItem* root = m_tree->topLevelItem(i);
        if (static_cast<NodeKind>(root->data(0, kNodeKindRole).toInt()) != NodeKind::SymbolGroup ||
            root->data(0, kNodeGroupRole).toInt() != group) {
            continue;
        }
        populateGroupChildren(root);
        const auto& indices = SymbolIndices(group);
        std::function<QTreeWidgetItem*(QTreeWidgetItem*)> find =
            [&](QTreeWidgetItem* item) -> QTreeWidgetItem* {
            for (int c = 0; c < item->childCount(); c++) {
                QTreeWidgetItem* child = item->child(c);
                if (static_cast<NodeKind>(child->data(0, kNodeKindRole).toInt()) ==
                    NodeKind::SymbolEntry) {
                    const int pos = child->data(0, kNodeIndexRole).toInt();
                    if (pos >= 0 && static_cast<size_t>(pos) < indices.size() &&
                        m_info.dynamic.symbols[static_cast<size_t>(indices[pos])].nid == nid) {
                        return child;
                    }
                } else if (QTreeWidgetItem* hit = find(child)) {
                    return hit;
                }
            }
            return nullptr;
        };
        if (QTreeWidgetItem* hit = find(root)) {
            for (QTreeWidgetItem* p = hit->parent(); p != nullptr; p = p->parent()) {
                p->setExpanded(true);
            }
            m_tree->setCurrentItem(hit);
            m_tree->scrollToItem(hit);
        }
        return;
    }
}

void ElfInfoDialog::onTreeContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (item == nullptr ||
        static_cast<NodeKind>(item->data(0, kNodeKindRole).toInt()) != NodeKind::SymbolEntry) {
        return;
    }
    const int group = item->data(0, kNodeGroupRole).toInt();
    const int index = item->data(0, kNodeIndexRole).toInt();
    const auto& indices = SymbolIndices(group);
    if (index < 0 || static_cast<size_t>(index) >= indices.size()) {
        return;
    }
    const std::string nid = m_info.dynamic.symbols[static_cast<size_t>(indices[index])].nid;

    QMenu menu(this);
    QAction* find = menu.addAction(group == 0 ? tr("Find which module provides this...")
                                              : tr("Find modules that export this..."));
    QAction* copy_nid = menu.addAction(tr("Copy NID"));
    const bool can_search = !m_gameRoot.empty() || !m_sysModulesDir.empty();
    find->setEnabled(can_search);
    if (!can_search) {
        find->setToolTip(tr("Open ELF Info from the game list to search the game's modules."));
    }
    QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (chosen == copy_nid) {
        QApplication::clipboard()->setText(S(nid));
    } else if (chosen == find) {
        auto* dialog = new FindExportDialog(m_gameTitle.isEmpty() ? m_gameTag : m_gameTitle,
                                            m_gameRoot, m_sysModulesDir, S(nid),
                                            /*include_sys_modules=*/true, parentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    }
}

void ElfInfoDialog::onTreeSelectionChanged() {
    showDetailForItem(m_tree->currentItem());
}

ElfInfoDialog::FieldList ElfInfoDialog::SelfWrapperFields() const {
    const auto& s = m_info.self_header;
    return {
        {tr("Declared file size"), tr("%1 bytes").arg(static_cast<u64>(s.file_size))},
        {tr("Segment entries"), QString::number(static_cast<u16>(s.segments_num))},
    };
}

ElfInfoDialog::FieldList ElfInfoDialog::SelfSegmentFields(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= m_info.self_segments.size()) {
        return {};
    }
    const auto& seg = m_info.self_segments[static_cast<size_t>(index)];
    const u64 type = seg.type;
    const bool has_phdr = (type & 0x800) != 0;
    const u32 phdr_id = static_cast<u32>((type >> 20) & 0xFFF);
    const u64 compressed = seg.compressed_size;
    const u64 decompressed = seg.decompressed_size;

    FieldList fields = {
        {tr("Type (raw)"), S(Loader::ElfInfo::Hex(type, 16))},
        {tr("Backs a program header"), has_phdr ? tr("yes") : tr("no")},
    };
    if (has_phdr) {
        fields.push_back({tr("Program header index"), QString::number(phdr_id)});
    }
    fields.push_back({tr("Offset in file"), S(Loader::ElfInfo::Hex(seg.offset, 16))});
    fields.push_back({tr("Compressed size"), tr("%1 bytes").arg(compressed)});
    fields.push_back({tr("Decompressed size"), tr("%1 bytes").arg(decompressed)});
    fields.push_back(
        {tr("Compressed?"), compressed != decompressed ? tr("yes") : tr("no (stored as-is)")});
    return fields;
}

ElfInfoDialog::FieldList ElfInfoDialog::ElfHeaderFields() const {
    const auto& e = m_info.ehdr;
    const auto module_class = Loader::ElfInfo::ClassifyModule(m_info);
    QString module_kind_value = S(Loader::ElfInfo::ModuleKindName(module_class.kind));
    if (module_class.kind == Loader::ElfInfo::ModuleKind::SharedModule &&
        !module_class.so_name.empty()) {
        module_kind_value += QStringLiteral(" - ") + S(module_class.so_name);
    }

    const auto failures = Loader::ElfInfo::StrictValidationFailures(m_info);
    QString validation_value = tr("passes");
    if (!failures.empty()) {
        QStringList lines;
        for (const auto& line : failures) {
            lines << S(line);
        }
        validation_value =
            tr("would be rejected by Kyty's loader: %1").arg(lines.join(QStringLiteral("; ")));
    }

    return {
        {tr("Found at file offset"), S(Loader::ElfInfo::Hex(m_info.ehdr_file_offset))},
        {tr("Class"), S(Loader::ElfInfo::ElfClassName(e.e_ident[4]))},
        {tr("Data encoding"), S(Loader::ElfInfo::ElfDataName(e.e_ident[5]))},
        {tr("OS/ABI"), S(Loader::ElfInfo::ElfOsAbiName(e.e_ident[7]))},
        {tr("ABI version"), QString::number(static_cast<int>(e.e_ident[8]))},
        {tr("Type"), S(Loader::ElfInfo::ElfTypeName(e.e_type))},
        {tr("Module kind"), module_kind_value},
        {tr("Machine"), S(Loader::ElfInfo::ElfMachineName(e.e_machine))},
        {tr("Platform"), S(Loader::ElfInfo::PlatformName(m_info))},
        {tr("PS4/PS5 loader validation"), validation_value},
        {tr("Entry point"), S(Loader::ElfInfo::Hex(e.e_entry))},
        {tr("Flags"), S(Loader::ElfInfo::Hex(e.e_flags))},
        {tr("Program header count"), QString::number(static_cast<u16>(e.e_phnum))},
        {tr("Program header entry size"), tr("%1 bytes").arg(static_cast<u16>(e.e_phentsize))},
        {tr("Program header table offset"),
         S(Loader::ElfInfo::Hex(m_info.ehdr_file_offset + static_cast<u64>(e.e_phoff)))},
        {tr("Section header count"),
         QString::number(static_cast<u16>(e.e_shnum)) +
             (e.e_shnum > 0 && m_info.shdrs.empty()
                  ? tr(" (present but not read - see Raw Text tab for details)")
                  : QString())},
    };
}

ElfInfoDialog::FieldList ElfInfoDialog::ProgramHeadersSummaryFields() const {
    return {
        {tr("Count"), QString::number(m_info.phdrs.size())},
    };
}

ElfInfoDialog::FieldList ElfInfoDialog::ProgramHeaderFields(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= m_info.phdrs.size()) {
        return {};
    }
    const auto& p = m_info.phdrs[static_cast<size_t>(index)];
    return {
        {tr("Type"), S(Loader::ElfInfo::PhdrTypeName(p.p_type))},
        {tr("Flags"), S(Loader::ElfInfo::PhdrFlagsName(p.p_flags))},
        {tr("File offset"), S(Loader::ElfInfo::Hex(p.p_offset, 16))},
        {tr("Virtual address"), S(Loader::ElfInfo::Hex(p.p_vaddr, 16))},
        {tr("Physical address"), S(Loader::ElfInfo::Hex(p.p_paddr, 16))},
        {tr("File size"), tr("%1 bytes").arg(static_cast<u64>(p.p_filesz))},
        {tr("Memory size"), tr("%1 bytes").arg(static_cast<u64>(p.p_memsz))},
        {tr("Alignment"), S(Loader::ElfInfo::Hex(p.p_align))},
    };
}

ElfInfoDialog::FieldList ElfInfoDialog::SectionHeadersSummaryFields() const {
    return {
        {tr("Count"), QString::number(m_info.shdrs.size())},
    };
}

ElfInfoDialog::FieldList ElfInfoDialog::SectionHeaderFields(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= m_info.shdrs.size()) {
        return {};
    }
    const auto& sh = m_info.shdrs[static_cast<size_t>(index)];
    const QString name = m_info.section_names[static_cast<size_t>(index)].empty()
                             ? tr("<unnamed>")
                             : S(m_info.section_names[static_cast<size_t>(index)]);
    return {
        {tr("Name"), name},
        {tr("Type"), S(Loader::ElfInfo::ShdrTypeName(sh.sh_type))},
        {tr("Flags"), S(Loader::ElfInfo::Hex(sh.sh_flags))},
        {tr("Virtual address"), S(Loader::ElfInfo::Hex(sh.sh_addr, 16))},
        {tr("File offset"), S(Loader::ElfInfo::Hex(sh.sh_offset, 16))},
        {tr("Size"), tr("%1 bytes").arg(static_cast<u64>(sh.sh_size))},
        {tr("Link (sh_link)"), QString::number(static_cast<u32>(sh.sh_link))},
        {tr("Info (sh_info)"), QString::number(static_cast<u32>(sh.sh_info))},
        {tr("Address alignment"), S(Loader::ElfInfo::Hex(sh.sh_addralign))},
        {tr("Entry size"), tr("%1 bytes").arg(static_cast<u64>(sh.sh_entsize))},
    };
}

ElfInfoDialog::FieldList ElfInfoDialog::DynamicSectionFields() const {
    if (!m_info.dynamic.readable) {
        return {};
    }
    FieldList fields;
    fields.reserve(m_info.dynamic.entries.size());
    for (size_t i = 0; i < m_info.dynamic.entries.size(); i++) {
        const auto& d = m_info.dynamic.entries[i];
        QString value = S(Loader::ElfInfo::Hex(d.d_val, 16));
        if (!m_info.dynamic.entry_strings[i].empty()) {
            value +=
                QStringLiteral("  (") + S(m_info.dynamic.entry_strings[i]) + QStringLiteral(")");
        }
        fields.push_back(
            {tr("[%1] %2").arg(i).arg(S(Loader::ElfInfo::DynTagName(d.d_tag))), value});
    }
    return fields;
}

ElfInfoDialog::FieldList ElfInfoDialog::DynamicSummaryFields() const {
    FieldList fields;
    for (const auto& row : m_info.dynamic.summary) {
        fields.push_back({S(row.first), S(row.second)});
    }
    return fields;
}

ElfInfoDialog::FieldList ElfInfoDialog::ModuleGroupFields(int group) const {
    const auto& v = group == 0 ? m_info.dynamic.import_modules : m_info.dynamic.export_modules;
    FieldList fields;
    for (size_t i = 0; i < v.size(); i++) {
        fields.push_back(
            {tr("[%1] %2").arg(i).arg(S(v[i].name)),
             tr("id %1, v%2.%3").arg(S(v[i].id)).arg(v[i].version_major).arg(v[i].version_minor)});
    }
    return fields;
}

ElfInfoDialog::FieldList ElfInfoDialog::ModuleEntryFields(int group, int index) const {
    const auto& v = group == 0 ? m_info.dynamic.import_modules : m_info.dynamic.export_modules;
    if (index < 0 || static_cast<size_t>(index) >= v.size()) {
        return {};
    }
    const auto& m = v[static_cast<size_t>(index)];
    return {
        {tr("Name"), S(m.name)},
        {tr("Short ID"), S(m.id)},
        {tr("Version"), tr("%1.%2").arg(m.version_major).arg(m.version_minor)},
    };
}

ElfInfoDialog::FieldList ElfInfoDialog::LibraryGroupFields(int group) const {
    const auto& v = group == 0 ? m_info.dynamic.import_libs : m_info.dynamic.export_libs;
    FieldList fields;
    for (size_t i = 0; i < v.size(); i++) {
        fields.push_back({tr("[%1] %2").arg(i).arg(S(v[i].name)),
                          tr("id %1, v%2").arg(S(v[i].id)).arg(v[i].version)});
    }
    return fields;
}

ElfInfoDialog::FieldList ElfInfoDialog::LibraryEntryFields(int group, int index) const {
    const auto& v = group == 0 ? m_info.dynamic.import_libs : m_info.dynamic.export_libs;
    if (index < 0 || static_cast<size_t>(index) >= v.size()) {
        return {};
    }
    const auto& l = v[static_cast<size_t>(index)];
    return {
        {tr("Name"), S(l.name)},
        {tr("Short ID"), S(l.id)},
        {tr("Version"), QString::number(l.version)},
    };
}

const std::vector<int>& ElfInfoDialog::SymbolIndices(int group) const {
    if (!m_symbolIndicesBuilt) {
        for (size_t i = 0; i < m_info.dynamic.symbols.size(); i++) {
            (m_info.dynamic.symbols[i].is_export ? m_exportSymbolIndices : m_importSymbolIndices)
                .push_back(static_cast<int>(i));
        }
        m_symbolIndicesBuilt = true;
    }
    return group == 0 ? m_importSymbolIndices : m_exportSymbolIndices;
}

const std::vector<ElfInfoDialog::SymbolModuleBucket>& ElfInfoDialog::SymbolModules(
    int group) const {
    const int g = group == 0 ? 0 : 1;
    if (m_symbolModulesBuilt[g]) {
        return m_symbolModules[g];
    }
    m_symbolModulesBuilt[g] = true;

    struct ModuleAcc {
        QString version;
        std::map<QString, std::vector<int>> libraries;
    };
    std::map<QString, ModuleAcc> grouped;
    const auto& indices = SymbolIndices(group);
    for (size_t pos = 0; pos < indices.size(); pos++) {
        const auto& s = m_info.dynamic.symbols[static_cast<size_t>(indices[pos])];
        QString module;
        QString version;
        if (!s.module.empty()) {
            module = S(s.module);
            version =
                QStringLiteral("v%1.%2").arg(s.module_version_major).arg(s.module_version_minor);
        } else if (!s.module_id.empty()) {
            module = tr("(unresolved module %1)").arg(S(s.module_id));
        } else {
            module = tr("(no module - not a NID symbol)");
        }
        QString library;
        if (!s.library.empty()) {
            library = S(s.library);
        } else if (!s.library_id.empty()) {
            library = tr("(unresolved library %1)").arg(S(s.library_id));
        } else {
            library = tr("(no library)");
        }
        auto& acc = grouped[module];
        acc.version = version;
        acc.libraries[library].push_back(static_cast<int>(pos));
    }

    auto by_name = [this, &indices](int a, int b) {
        const QString na =
            SymbolDisplayName(m_info.dynamic.symbols[static_cast<size_t>(indices[a])]);
        const QString nb =
            SymbolDisplayName(m_info.dynamic.symbols[static_cast<size_t>(indices[b])]);
        return QString::compare(na, nb, Qt::CaseInsensitive) < 0;
    };
    auto& out = m_symbolModules[g];
    for (auto& [module, acc] : grouped) {
        SymbolModuleBucket bucket;
        bucket.name = module;
        bucket.version = acc.version;
        for (auto& [library, positions] : acc.libraries) {
            std::sort(positions.begin(), positions.end(), by_name);
            bucket.count += positions.size();
            bucket.libraries.push_back({library, std::move(positions)});
        }
        out.push_back(std::move(bucket));
    }
    // Real modules alphabetically, the "(unresolved ...)" buckets last.
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        const bool pa = a.name.startsWith(QLatin1Char('('));
        const bool pb = b.name.startsWith(QLatin1Char('('));
        if (pa != pb) {
            return pb;
        }
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });
    return out;
}

void ElfInfoDialog::addSymbolLeaves(QTreeWidgetItem* parent, int group,
                                    const std::vector<int>& positions) {
    const auto& indices = SymbolIndices(group);
    for (const int pos : positions) {
        const auto& sym = m_info.dynamic.symbols[static_cast<size_t>(indices[pos])];
        addTreeNode(parent, SymbolDisplayName(sym), NodeKind::SymbolEntry, pos, group);
    }
}

ElfInfoDialog::FieldList ElfInfoDialog::SymbolModuleFields(int group, int module) const {
    const auto& mod = SymbolModules(group)[static_cast<size_t>(module)];
    FieldList fields = {{tr("Symbols"), QString::number(mod.count)}};
    if (!mod.version.isEmpty()) {
        fields.push_back({tr("Version"), mod.version});
    }
    for (const auto& lib : mod.libraries) {
        fields.push_back({tr("Library %1").arg(lib.name),
                          tr("%n symbol(s)", nullptr, static_cast<int>(lib.positions.size()))});
    }
    return fields;
}

ElfInfoDialog::FieldList ElfInfoDialog::SymbolEntryFields(int group, int index) const {
    const auto& indices = SymbolIndices(group);
    if (index < 0 || static_cast<size_t>(index) >= indices.size()) {
        return {};
    }
    const auto& s =
        m_info.dynamic.symbols[static_cast<size_t>(indices[static_cast<size_t>(index)])];
    FieldList fields = {
        {tr("Resolved name"), s.resolved_name.empty()
                                  ? tr("(unknown - not a common libc/pthread/libkernel name)")
                                  : S(s.resolved_name)},
        {tr("NID"), S(s.nid)},
        {tr("Raw name"), S(s.raw_name)},
        {tr("Type"), S(Loader::ElfInfo::SymbolTypeName(s.type))},
        {tr("Binding"), S(Loader::ElfInfo::SymbolBindName(s.bind))},
    };
    if (s.is_export) {
        fields.push_back({tr("Value (address)"), S(Loader::ElfInfo::Hex(s.value))});
    }
    if (s.size != 0) {
        fields.push_back({tr("Size"), tr("%1 bytes").arg(s.size)});
    }
    if (!s.library.empty()) {
        fields.push_back({tr("Library"), tr("%1 (v%2)").arg(S(s.library)).arg(s.library_version)});
    } else if (!s.library_id.empty()) {
        fields.push_back({tr("Library ID"), tr("%1 (unresolved)").arg(S(s.library_id))});
    }
    if (!s.is_export && s.nid_form) {
        // Same grouping as the Imported Symbols tree; an unresolved library ID
        // says nothing about the subsystem, so it stays "unknown".
        fields.push_back({tr("Category"), s.library.empty()
                                              ? tr("unknown")
                                              : S(Core::Analysis::InferCategory(s.library))});
    }
    if (const auto* sig = Core::Analysis::FindSignature(s.resolved_name)) {
        fields.push_back({tr("Prototype"),
                          tr("%1  (unverified)").arg(S(Core::Analysis::FormatPrototype(*sig)))});
    }
    if (!s.module.empty()) {
        fields.push_back({tr("Module"), tr("%1 (v%2.%3)")
                                            .arg(S(s.module))
                                            .arg(s.module_version_major)
                                            .arg(s.module_version_minor)});
    } else if (!s.module_id.empty()) {
        fields.push_back({tr("Module ID"), tr("%1 (unresolved)").arg(S(s.module_id))});
    }
    return fields;
}

ElfInfoDialog::FieldList ElfInfoDialog::TlsInfoFields() const {
    const auto tls = Loader::ElfInfo::GetTlsSummary(m_info);
    if (!tls.present) {
        return {};
    }
    return {
        {tr("Image vaddr"), S(Loader::ElfInfo::Hex(tls.image_vaddr))},
        {tr("Image size"), tr("%1 bytes (incl. zero-fill)").arg(tls.image_size)},
        {tr("Init data size"), tr("%1 bytes (from file)").arg(tls.init_size)},
        {tr("Zero-fill size"),
         tr("%1 bytes").arg(tls.image_size > tls.init_size ? tls.image_size - tls.init_size : 0)},
        {tr("TCB offset"), S(Loader::ElfInfo::Hex(tls.tcb_offset))},
        {tr("Alignment"), QString::number(tls.align)},
    };
}

const std::vector<int>& ElfInfoDialog::RelocationIndices(int group) const {
    if (!m_relocIndicesBuilt) {
        for (size_t i = 0; i < m_info.dynamic.relocations.size(); i++) {
            (m_info.dynamic.relocations[i].is_plt ? m_pltRelocIndices : m_relaRelocIndices)
                .push_back(static_cast<int>(i));
        }
        m_relocIndicesBuilt = true;
    }
    return group == 0 ? m_pltRelocIndices : m_relaRelocIndices;
}

ElfInfoDialog::FieldList ElfInfoDialog::RelocationGroupFields(int group) const {
    return {{tr("Count"), QString::number(RelocationIndices(group).size())}};
}

ElfInfoDialog::FieldList ElfInfoDialog::RelocationEntryFields(int group, int index) const {
    const auto& indices = RelocationIndices(group);
    if (index < 0 || static_cast<size_t>(index) >= indices.size()) {
        return {};
    }
    const auto& r =
        m_info.dynamic.relocations[static_cast<size_t>(indices[static_cast<size_t>(index)])];
    const QString type_name = S(Loader::ElfInfo::RelocTypeName(r.type));
    FieldList fields = {
        {tr("Offset"), S(Loader::ElfInfo::Hex(r.offset, 16))},
        {tr("Type"), type_name.isEmpty() ? tr("unsupported type (%1)").arg(r.type) : type_name},
        {tr("Symbol index"), QString::number(r.symbol_index)},
        {tr("Addend"), S(Loader::ElfInfo::Hex(static_cast<u64>(r.addend)))},
    };
    if (r.has_symbol) {
        fields.push_back({tr("Symbol"), S(r.symbol_nid)});
        if (!r.symbol_library.empty()) {
            fields.push_back({tr("Library"), S(r.symbol_library)});
        }
        if (!r.symbol_module.empty()) {
            fields.push_back({tr("Module"), S(r.symbol_module)});
        }
    }
    return fields;
}

ElfInfoDialog::FieldList ElfInfoDialog::LibVersionGroupFields() const {
    FieldList fields = {{tr("Count"), QString::number(m_info.lib_versions.size())}};
    for (const auto& e : m_info.lib_versions) {
        fields.push_back({S(e.name), S(e.GuessedVersionString()) + QStringLiteral("  (") +
                                         S(Loader::ElfInfo::Hex(e.version_raw, 8)) +
                                         QStringLiteral(")")});
    }
    return fields;
}

ElfInfoDialog::FieldList ElfInfoDialog::LibVersionEntryFields(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= m_info.lib_versions.size()) {
        return {};
    }
    const auto& e = m_info.lib_versions[static_cast<size_t>(index)];
    QString raw_hex;
    for (const u8 b : e.raw) {
        raw_hex += QStringLiteral("%1 ").arg(b, 2, 16, QLatin1Char('0'));
    }
    return {
        {tr("Library"), S(e.name)},
        {tr("Version (guessed)"), S(e.GuessedVersionString())},
        {tr("Version (raw)"), S(Loader::ElfInfo::Hex(e.version_raw, 8))},
        {tr("Raw entry bytes"), raw_hex.trimmed()},
    };
}

const std::vector<ElfInfoDialog::CategoryBucket>& ElfInfoDialog::Categories() const {
    if (m_categoriesBuilt) {
        return m_categories;
    }
    m_categoriesBuilt = true;
    // category -> library -> import positions (in SymbolIndices(0)).
    std::map<QString, std::map<QString, std::vector<int>>> grouped;
    const auto& imports = SymbolIndices(0);
    for (size_t pos = 0; pos < imports.size(); pos++) {
        const auto& s = m_info.dynamic.symbols[static_cast<size_t>(imports[pos])];
        QString lib;
        QString category;
        if (!s.nid_form) {
            lib = tr("(non-NID symbols)");
            category = tr("other");
        } else if (!s.library.empty()) {
            lib = S(s.library);
            category = S(Core::Analysis::InferCategory(s.library));
        } else {
            lib = tr("(unresolved library %1)").arg(S(s.library_id));
            category = tr("unknown");
        }
        grouped[category][lib].push_back(static_cast<int>(pos));
    }
    for (auto& [category, libs] : grouped) {
        CategoryBucket bucket;
        bucket.name = category;
        for (auto& [lib, positions] : libs) {
            std::sort(positions.begin(), positions.end(), [this, &imports](int a, int b) {
                return QString::compare(
                           SymbolDisplayName(
                               m_info.dynamic.symbols[static_cast<size_t>(imports[a])]),
                           SymbolDisplayName(
                               m_info.dynamic.symbols[static_cast<size_t>(imports[b])]),
                           Qt::CaseInsensitive) < 0;
            });
            LibraryBucket lb;
            lb.name = lib;
            for (const int pos : positions) {
                lb.symbol_indices.push_back(imports[static_cast<size_t>(pos)]);
            }
            lb.positions = std::move(positions);
            bucket.import_count += lb.positions.size();
            bucket.libraries.push_back(std::move(lb));
        }
        std::sort(bucket.libraries.begin(), bucket.libraries.end(),
                  [](const LibraryBucket& a, const LibraryBucket& b) {
                      return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
                  });
        m_categories.push_back(std::move(bucket));
    }
    // Biggest subsystems first.
    std::stable_sort(m_categories.begin(), m_categories.end(),
                     [](const CategoryBucket& a, const CategoryBucket& b) {
                         return a.import_count > b.import_count;
                     });
    return m_categories;
}

QString ElfInfoDialog::SymbolDisplayName(const Loader::ElfInfo::SymbolInfo& s) const {
    return s.resolved_name.empty() ? S(s.nid) : S(s.resolved_name);
}

ElfInfoDialog::FieldList ElfInfoDialog::CategoryGroupFields() const {
    FieldList fields;
    size_t with_prototype = 0;
    for (const auto& c : Categories()) {
        fields.push_back(
            {c.name, tr("%n import(s)", nullptr, static_cast<int>(c.import_count)) +
                         QStringLiteral(", ") +
                         tr("%n library(ies)", nullptr, static_cast<int>(c.libraries.size()))});
        for (const auto& lib : c.libraries) {
            for (const int idx : lib.symbol_indices) {
                if (Core::Analysis::FindSignature(
                        m_info.dynamic.symbols[static_cast<size_t>(idx)].resolved_name)) {
                    with_prototype++;
                }
            }
        }
    }
    fields.push_back({tr("Imports with a known prototype"), QString::number(with_prototype)});
    return fields;
}

ElfInfoDialog::FieldList ElfInfoDialog::CategoryEntryFields(int index) const {
    const auto& cat = Categories()[static_cast<size_t>(index)];
    FieldList fields;
    for (const auto& lib : cat.libraries) {
        fields.push_back(
            {lib.name, tr("%n import(s)", nullptr, static_cast<int>(lib.symbol_indices.size()))});
    }
    return fields;
}

ElfInfoDialog::FieldList ElfInfoDialog::CategoryLibraryFields(int category, int index) const {
    const auto& lib =
        Categories()[static_cast<size_t>(category)].libraries[static_cast<size_t>(index)];
    FieldList fields;
    for (const int idx : lib.symbol_indices) {
        const auto& s = m_info.dynamic.symbols[static_cast<size_t>(idx)];
        QString value;
        if (const auto* sig = Core::Analysis::FindSignature(s.resolved_name)) {
            value = S(Core::Analysis::FormatPrototype(*sig));
        } else if (s.resolved_name.empty()) {
            value = tr("(name unknown)");
        } else {
            value = tr("NID %1").arg(S(s.nid)); // no prototype known - show something useful
        }
        fields.push_back({SymbolDisplayName(s), value});
    }
    std::stable_sort(fields.begin(), fields.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    return fields;
}

QString ElfInfoDialog::FullText() const {
    QString text = S(Loader::ElfInfo::ToText(m_info));
    const auto& cats = Categories();
    if (cats.empty()) {
        return text;
    }
    text += QStringLiteral("\n=== Imports by category ===\n");
    for (const auto& c : cats) {
        text += QStringLiteral("%1: %2 imports\n").arg(c.name).arg(c.import_count);
        for (const auto& lib : c.libraries) {
            text += QStringLiteral("  %1: %2\n").arg(lib.name).arg(lib.symbol_indices.size());
            for (const int idx : lib.symbol_indices) {
                const auto& s = m_info.dynamic.symbols[static_cast<size_t>(idx)];
                if (const auto* sig = Core::Analysis::FindSignature(s.resolved_name)) {
                    text +=
                        QStringLiteral("    %1\n").arg(S(Core::Analysis::FormatPrototype(*sig)));
                }
            }
        }
    }
    text += QStringLiteral("(Prototypes come from a small built-in, unverified signature set.)\n");
    return text;
}

size_t ElfInfoDialog::UnresolvedImportCount() const {
    size_t n = 0;
    for (const auto& sym : m_info.dynamic.symbols) {
        if (!sym.is_export && sym.nid_form && sym.resolved_name.empty()) {
            n++;
        }
    }
    return n;
}

void ElfInfoDialog::onExtractElf() {
    if (!m_raw) {
        return;
    }
    const auto result = Loader::ElfInfo::ExtractElf(*m_raw, m_info);
    if (!result.error.empty()) {
        QMessageBox::critical(this, tr("Extract ELF"),
                              tr("Could not extract an ELF: %1").arg(S(result.error)));
        return;
    }

    QString suggested = m_suggestedFileName;
    suggested.replace(QStringLiteral("_info.txt"), QStringLiteral(".elf"));
    if (!suggested.endsWith(QStringLiteral(".elf"))) {
        suggested = QStringLiteral("eboot.elf");
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Extracted ELF"), suggested,
                                                      tr("ELF Files (*.elf);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(reinterpret_cast<const char*>(result.elf.data()),
                   static_cast<qint64>(result.elf.size())) !=
            static_cast<qint64>(result.elf.size())) {
        QMessageBox::critical(this, tr("Error"), tr("Could not write to %1").arg(path));
        return;
    }

    QString message = tr("Wrote %1 bytes (%2 loadable segment(s) placed).")
                          .arg(result.elf.size())
                          .arg(result.copied_load_segments);
    if (result.encrypted_segments > 0 || result.compressed_segments > 0) {
        message += QStringLiteral("\n\n") +
                   tr("Warning: the SELF marks %1 segment(s) as encrypted and %2 as compressed. "
                      "Those bytes were copied as-is, so the matching parts of the ELF are not "
                      "usable code - this only gives a clean ELF for already-decrypted dumps.")
                       .arg(result.encrypted_segments)
                       .arg(result.compressed_segments);
    }
    QMessageBox::information(this, tr("Extract ELF"), message);
}

void ElfInfoDialog::onShowStrings() {
    if (!m_raw) {
        return;
    }
    QString suggested = m_suggestedFileName;
    suggested.replace(QStringLiteral("_info.txt"), QStringLiteral("_strings.txt"));
    auto* dialog = new StringsDialog(m_gameTag, m_raw, suggested, parentWidget());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void ElfInfoDialog::onExportUnresolvedNids() {
    // (library, nid) -> occurrences, mirroring ps5rs' export-unknown CSV.
    std::map<std::pair<std::string, std::string>, size_t> unresolved;
    for (const auto& sym : m_info.dynamic.symbols) {
        if (!sym.is_export && sym.nid_form && sym.resolved_name.empty()) {
            const std::string lib = sym.library.empty() ? sym.library_id : sym.library;
            unresolved[{lib, sym.nid}]++;
        }
    }
    if (unresolved.empty()) {
        QMessageBox::information(this, tr("Export Unresolved NIDs"),
                                 tr("Every imported NID already resolves to a name."));
        return;
    }

    QString suggested = m_suggestedFileName;
    suggested.replace(QStringLiteral("_info.txt"), QStringLiteral("_unknown_nids.csv"));
    if (!suggested.endsWith(QStringLiteral(".csv"))) {
        suggested = QStringLiteral("unknown_nids.csv");
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Unresolved NIDs"), suggested,
                                                      tr("CSV Files (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not write to %1").arg(path));
        return;
    }
    auto csv_field = [](QString v) {
        if (v.contains(QLatin1Char(',')) || v.contains(QLatin1Char('"'))) {
            v.replace(QStringLiteral("\""), QStringLiteral("\"\""));
            v = QStringLiteral("\"") + v + QStringLiteral("\"");
        }
        return v;
    };
    QTextStream stream(&file);
    stream << "library,nid,count,games\n";
    for (const auto& [key, count] : unresolved) {
        stream << csv_field(S(key.first)) << ',' << csv_field(S(key.second)) << ',' << count << ','
               << csv_field(m_gameTag) << '\n';
    }
    QMessageBox::information(this, tr("Export Unresolved NIDs"),
                             tr("Exported %1 unresolved NID(s).").arg(unresolved.size()));
}

void ElfInfoDialog::onCopy() {
    QApplication::clipboard()->setText(m_text);
}

void ElfInfoDialog::onSave() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save ELF Info"), m_suggestedFileName, tr("Text Files (*.txt)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not write to %1").arg(path));
        return;
    }

    QTextStream stream(&file);
    stream << m_text;
}

void ElfInfoDialog::onLoadNidDatabase() {
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Load NID Database"), QString(),
                                     tr("NID database or names file (*.csv *.txt);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }

    const std::filesystem::path fs_path = path.toStdString();
    auto& catalog = Loader::ElfInfo::GetMutableDefaultNidCatalog();
    size_t loaded = catalog.LoadNidsCsvFile(fs_path);
    if (loaded == 0) {
        loaded = catalog.LoadNamesFile(fs_path);
    }

    if (loaded == 0) {
        QMessageBox::warning(
            this, tr("Load NID Database"),
            tr("No entries could be loaded from this file. Expected either a plain list of "
               "function names (one per line), or a NID database (\"<nid> <name>\" per line, "
               "or a CSV with nid/name columns)."));
        return;
    }

    RefreshSymbolResolution();
    QMessageBox::information(
        this, tr("Load NID Database"),
        tr("Loaded %1 entries. Symbol names have been refreshed.").arg(loaded));
}

void ElfInfoDialog::RefreshSymbolResolution() {
    const auto& catalog = Loader::ElfInfo::GetDefaultNidCatalog();
    for (auto& sym : m_info.dynamic.symbols) {
        if (auto resolved = catalog.Resolve(sym.nid)) {
            sym.resolved_name = *resolved;
        }
    }

    m_symbolModulesBuilt[0] = m_symbolModulesBuilt[1] = false;
    m_symbolModules[0].clear();
    m_symbolModules[1].clear();
    m_categoriesBuilt = false;
    m_categories.clear();

    m_text = FullText();
    m_rawView->setPlainText(m_text);
    if (m_exportUnresolvedBtn != nullptr) {
        m_exportUnresolvedBtn->setEnabled(UnresolvedImportCount() > 0);
    }
    buildTree();
}
