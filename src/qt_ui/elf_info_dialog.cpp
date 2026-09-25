// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "elf_info_dialog.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace {

constexpr int kNodeKindRole = Qt::UserRole;
constexpr int kNodeIndexRole = Qt::UserRole + 1;

QString S(const std::string& s) {
    return QString::fromStdString(s);
}

} // namespace

ElfInfoDialog::ElfInfoDialog(const QString& title, const Loader::ElfInfo::ParsedInfo& info,
                             const QString& suggestedFileName, QWidget* parent)
    : QDialog(parent), m_info(info), m_text(S(Loader::ElfInfo::ToText(info))),
      m_suggestedFileName(suggestedFileName) {
    setWindowTitle(tr("ELF Info - %1").arg(title));
    resize(900, 640);

    auto* layout = new QVBoxLayout(this);

    m_formatLabel = new QLabel(this);
    QFont format_font = m_formatLabel->font();
    format_font.setBold(true);
    m_formatLabel->setFont(format_font);
    if (m_info.is_self) {
        m_formatLabel->setText(tr("SELF-wrapped PS4/PS5 executable"));
    } else if (m_info.is_valid_elf) {
        m_formatLabel->setText(tr("Plain ELF executable (not SELF-wrapped)"));
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
    detail_layout->addWidget(m_detailTable);

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
    buttons->addStretch();
    buttons->addWidget(close_btn);
    layout->addLayout(buttons);

    connect(copy_btn, &QPushButton::clicked, this, &ElfInfoDialog::onCopy);
    connect(save_btn, &QPushButton::clicked, this, &ElfInfoDialog::onSave);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &ElfInfoDialog::onTreeSelectionChanged);

    buildTree();
}

void ElfInfoDialog::addTreeNode(QTreeWidgetItem* parent, const QString& label, NodeKind kind,
                                int index) {
    auto* item = new QTreeWidgetItem(parent, {label});
    item->setData(0, kNodeKindRole, static_cast<int>(kind));
    item->setData(0, kNodeIndexRole, index);
}

void ElfInfoDialog::buildTree() {
    m_tree->clear();

    if (m_info.is_self) {
        auto* self_root = new QTreeWidgetItem(m_tree, {tr("SELF Wrapper")});
        self_root->setData(0, kNodeKindRole, static_cast<int>(NodeKind::SelfWrapper));
        self_root->setData(0, kNodeIndexRole, -1);

        for (size_t i = 0; i < m_info.self_segments.size(); i++) {
            addTreeNode(self_root, tr("Segment [%1]").arg(i), NodeKind::SelfSegment,
                        static_cast<int>(i));
        }
        self_root->setExpanded(true);
    }

    if (m_info.is_valid_elf) {
        auto* ehdr_item = new QTreeWidgetItem(m_tree, {tr("ELF Header")});
        ehdr_item->setData(0, kNodeKindRole, static_cast<int>(NodeKind::ElfHeader));
        ehdr_item->setData(0, kNodeIndexRole, -1);

        if (!m_info.phdrs.empty()) {
            auto* phdr_root =
                new QTreeWidgetItem(m_tree, {tr("Program Headers (%1)").arg(m_info.phdrs.size())});
            phdr_root->setData(0, kNodeKindRole, static_cast<int>(NodeKind::ProgramHeadersRoot));
            phdr_root->setData(0, kNodeIndexRole, -1);

            for (size_t i = 0; i < m_info.phdrs.size(); i++) {
                const QString type_name = S(Loader::ElfInfo::PhdrTypeName(m_info.phdrs[i].p_type));
                addTreeNode(phdr_root, tr("[%1] %2").arg(i).arg(type_name), NodeKind::ProgramHeader,
                            static_cast<int>(i));
            }
            phdr_root->setExpanded(true);
        }

        if (!m_info.shdrs.empty()) {
            auto* shdr_root =
                new QTreeWidgetItem(m_tree, {tr("Section Headers (%1)").arg(m_info.shdrs.size())});
            shdr_root->setData(0, kNodeKindRole, static_cast<int>(NodeKind::SectionHeadersRoot));
            shdr_root->setData(0, kNodeIndexRole, -1);

            for (size_t i = 0; i < m_info.shdrs.size(); i++) {
                const QString name =
                    m_info.section_names[i].empty() ? tr("<unnamed>") : S(m_info.section_names[i]);
                addTreeNode(shdr_root, tr("[%1] %2").arg(i).arg(name), NodeKind::SectionHeader,
                            static_cast<int>(i));
            }
            shdr_root->setExpanded(true);
        }

        if (m_info.dynamic.present) {
            auto* dyn_item = new QTreeWidgetItem(m_tree, {tr("Dynamic Section")});
            dyn_item->setData(0, kNodeKindRole, static_cast<int>(NodeKind::DynamicSection));
            dyn_item->setData(0, kNodeIndexRole, -1);
        }
    }

    if (m_tree->topLevelItemCount() > 0) {
        m_tree->setCurrentItem(m_tree->topLevelItem(0));
    } else {
        showDetail(tr("Nothing to show"),
                   tr("eboot.bin isn't SELF-wrapped and doesn't contain a recognizable ELF "
                      "header either."),
                   {});
    }
}

void ElfInfoDialog::showDetail(const QString& sectionTitle, const QString& note,
                               const FieldList& fields) {
    m_detailTitle->setText(sectionTitle);

    if (note.isEmpty()) {
        m_detailNote->hide();
    } else {
        m_detailNote->setText(note);
        m_detailNote->show();
    }

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

void ElfInfoDialog::showDetailForItem(QTreeWidgetItem* item) {
    if (item == nullptr) {
        showDetail(QString(), QString(), {});
        return;
    }

    const auto kind = static_cast<NodeKind>(item->data(0, kNodeKindRole).toInt());
    const int index = item->data(0, kNodeIndexRole).toInt();

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
    return {
        {tr("Found at file offset"), S(Loader::ElfInfo::Hex(m_info.ehdr_file_offset))},
        {tr("Class"), S(Loader::ElfInfo::ElfClassName(e.e_ident[4]))},
        {tr("Data encoding"), S(Loader::ElfInfo::ElfDataName(e.e_ident[5]))},
        {tr("OS/ABI"), S(Loader::ElfInfo::ElfOsAbiName(e.e_ident[7]))},
        {tr("ABI version"), QString::number(static_cast<int>(e.e_ident[8]))},
        {tr("Type"), S(Loader::ElfInfo::ElfTypeName(e.e_type))},
        {tr("Machine"), S(Loader::ElfInfo::ElfMachineName(e.e_machine))},
        {tr("Entry point"), S(Loader::ElfInfo::Hex(e.e_entry))},
        {tr("Flags"), S(Loader::ElfInfo::Hex(e.e_flags))},
        {tr("Program header count"), QString::number(static_cast<u16>(e.e_phnum))},
        {tr("Program header entry size"), tr("%1 bytes").arg(static_cast<u16>(e.e_phentsize))},
        {tr("Program header table offset"),
         S(Loader::ElfInfo::Hex(m_info.ehdr_file_offset + static_cast<u64>(e.e_phoff)))},
        {tr("Section header count"),
         QString::number(static_cast<u16>(e.e_shnum)) +
             (m_info.is_self ? tr(" (not read for SELF files)") : QString())},
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
