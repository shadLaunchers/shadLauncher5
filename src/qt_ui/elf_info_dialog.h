// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <utility>
#include <vector>
#include <QDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QTreeWidget>
#include "core/file_format/elf_info.h"

class ElfInfoDialog : public QDialog {
    Q_OBJECT
public:
    explicit ElfInfoDialog(const QString& title, const Loader::ElfInfo::ParsedInfo& info,
                           const QString& suggestedFileName, QWidget* parent = nullptr,
                           std::shared_ptr<const std::vector<u8>> raw = nullptr,
                           const QString& gameTag = QString());
    void SetGameContext(std::filesystem::path game_root, std::filesystem::path sys_modules_dir,
                        const QString& game_title);
    void SelectSymbol(const std::string& nid, bool exported);

private slots:
    void onCopy();
    void onSave();
    void onLoadNidDatabase();
    void onExtractElf();
    void onShowStrings();
    void onExportUnresolvedNids();
    void onTreeContextMenu(const QPoint& pos);
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
        LibVersionGroup,   // PT_SCE_LIBVERSION root
        LibVersionEntry,   // one library/version entry
        CategoryEntry,     // one category (index into m_categories)
        CategoryLibrary,   // one library in a category (group = category index)
        SymbolModule,      // module under Imported/Exported Symbols (group + module index)
        SymbolLibrary,     // library under a SymbolModule (sub-index role = library index)
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
    void FitDetailNote();

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
    FieldList LibVersionGroupFields() const;
    FieldList LibVersionEntryFields(int index) const;
    size_t UnresolvedImportCount() const;

    struct LibraryBucket {
        QString name;
        std::vector<int> symbol_indices;
        std::vector<int> positions;
    };
    struct CategoryBucket {
        QString name;
        size_t import_count = 0;
        std::vector<LibraryBucket> libraries;
    };
    const std::vector<CategoryBucket>& Categories() const;
    FieldList CategoryGroupFields() const;
    FieldList CategoryEntryFields(int index) const;
    FieldList CategoryLibraryFields(int category, int index) const;
    QString SymbolDisplayName(const Loader::ElfInfo::SymbolInfo& s) const;

    struct SymbolLibraryBucket {
        QString name;
        std::vector<int> positions;
    };
    struct SymbolModuleBucket {
        QString name;
        QString version;
        size_t count = 0;
        std::vector<SymbolLibraryBucket> libraries;
    };
    const std::vector<SymbolModuleBucket>& SymbolModules(int group) const;
    void addSymbolLeaves(QTreeWidgetItem* parent, int group, const std::vector<int>& positions);
    FieldList SymbolModuleFields(int group, int module) const;
    QString FullText() const;
    Loader::ElfInfo::ParsedInfo m_info;
    QString m_text;
    QString m_suggestedFileName;
    std::shared_ptr<const std::vector<u8>> m_raw;
    QString m_gameTag;
    std::filesystem::path m_gameRoot;
    std::filesystem::path m_sysModulesDir;
    QString m_gameTitle;
    QPushButton* m_exportUnresolvedBtn = nullptr;

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

    mutable std::vector<SymbolModuleBucket> m_symbolModules[2];
    mutable bool m_symbolModulesBuilt[2] = {false, false};

    mutable std::vector<CategoryBucket> m_categories;
    mutable bool m_categoriesBuilt = false;
};
