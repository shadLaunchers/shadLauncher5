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
    void onLoadNidDatabase();
    void onTreeSelectionChanged();
    void RefreshSymbolResolution();
    void onItemExpanded(QTreeWidgetItem* item);

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
        ModuleGroup,     // Imported/Exported Modules root (group: 0=import, 1=export)
        ModuleEntry,     // one module (group + index)
        LibraryGroup,    // Imported/Exported Libraries root (group: 0=import, 1=export)
        LibraryEntry,    // one library (group + index)
        SymbolGroup,     // group: 0=imports, 1=exports
        SymbolEntry,     // group + index within that filtered list
        RelocationGroup, // group: 0=PLT, 1=RELA
        RelocationEntry, // group + index within that filtered list
        TlsInfo,
        RawFileHeaderDump, // hex dump of the start of the file - shown when nothing recognized
        RawEhdrOffsetDump, // hex dump at ehdr_file_offset - shown when SELF recognized but inner
                           // ELF isn't
    };

    using FieldList = std::vector<std::pair<QString, QString>>;

    void buildTree();
    QTreeWidgetItem* addLazyGroup(const QString& label, NodeKind root_kind, int group,
                                  size_t count);
    void populateGroupChildren(QTreeWidgetItem* root);
    void addTreeNode(QTreeWidgetItem* parent, const QString& label, NodeKind kind, int index,
                     int group = 0);
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
    const std::vector<int>& SymbolIndices(int group) const;
    FieldList SymbolEntryFields(int group, int index) const;
    const std::vector<int>& RelocationIndices(int group) const;
    FieldList RelocationGroupFields(int group) const;
    FieldList RelocationEntryFields(int group, int index) const;
    FieldList TlsInfoFields() const;

    Loader::ElfInfo::ParsedInfo m_info;
    QString m_text;
    QString m_suggestedFileName;

    QTreeWidget* m_tree = nullptr;
    QLabel* m_formatLabel = nullptr;
    QLabel* m_detailTitle = nullptr;
    QLabel* m_detailNote = nullptr;
    QTableWidget* m_detailTable = nullptr;
    QPlainTextEdit* m_detailHexView = nullptr;
    QPlainTextEdit* m_rawView = nullptr;

    mutable std::vector<int> m_importSymbolIndices;
    mutable std::vector<int> m_exportSymbolIndices;
    mutable bool m_symbolIndicesBuilt = false;

    mutable std::vector<int> m_pltRelocIndices;
    mutable std::vector<int> m_relaRelocIndices;
    mutable bool m_relocIndicesBuilt = false;
};
