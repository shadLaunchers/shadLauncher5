// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <utility>
#include <vector>
#include <QDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QString>
#include <QTableWidget>
#include <QTreeWidget>
#include "core/file_format/elf_info.h"

class ElfInfoDialog : public QDialog {
    Q_OBJECT
public:
    explicit ElfInfoDialog(const QString& title, const Loader::ElfInfo::ParsedInfo& info,
                           const QString& suggestedFileName, QWidget* parent = nullptr);

private slots:
    void onCopy();
    void onSave();
    void onTreeSelectionChanged();

private:
    enum class NodeKind {
        SelfWrapper,
        SelfSegment,
        ElfHeader,
        ProgramHeadersRoot,
        ProgramHeader,
        SectionHeadersRoot,
        SectionHeader,
        DynamicSection,
        DynamicSummary,
        ModuleGroup,       // Imported/Exported Modules root (group: 0=import, 1=export)
        ModuleEntry,       // one module (group + index)
        LibraryGroup,      // Imported/Exported Libraries root (group: 0=import, 1=export)
        LibraryEntry,      // one library (group + index)
        RawFileHeaderDump, // hex dump of the start of the file - shown when nothing recognized
        RawEhdrOffsetDump, // hex dump at ehdr_file_offset - shown when SELF recognized but inner
                           // ELF isn't
    };

    using FieldList = std::vector<std::pair<QString, QString>>;

    void buildTree();
    void addTreeNode(QTreeWidgetItem* parent, const QString& label, NodeKind kind, int index);
    void showDetail(const QString& sectionTitle, const QString& note, const FieldList& fields);
    void showDetailHex(const QString& sectionTitle, const QString& note, const QString& hexText);
    void showDetailForItem(QTreeWidgetItem* item);

    FieldList SelfWrapperFields() const;
    FieldList SelfSegmentFields(int index) const;
    FieldList ElfHeaderFields() const;
    FieldList ProgramHeadersSummaryFields() const;
    FieldList ProgramHeaderFields(int index) const;
    FieldList SectionHeadersSummaryFields() const;
    FieldList SectionHeaderFields(int index) const;
    FieldList DynamicSectionFields() const;
    FieldList DynamicSummaryFields() const;
    FieldList ModuleGroupFields(int group) const;
    FieldList ModuleEntryFields(int group, int index) const;
    FieldList LibraryGroupFields(int group) const;
    FieldList LibraryEntryFields(int group, int index) const;

    Loader::ElfInfo::ParsedInfo m_info;
    QString m_text; // flat dump, for the Raw Text tab / copy / save
    QString m_suggestedFileName;

    QTreeWidget* m_tree = nullptr;
    QLabel* m_formatLabel = nullptr; // always-visible "what kind of file is this" banner
    QLabel* m_detailTitle = nullptr;
    QLabel* m_detailNote = nullptr;
    QTableWidget* m_detailTable = nullptr;
    QPlainTextEdit* m_detailHexView = nullptr; // shown instead of m_detailTable for hex-dump nodes
    QPlainTextEdit* m_rawView = nullptr;
};
