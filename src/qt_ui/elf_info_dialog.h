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
    };

    using FieldList = std::vector<std::pair<QString, QString>>;

    void buildTree();
    void addTreeNode(QTreeWidgetItem* parent, const QString& label, NodeKind kind, int index);
    void showDetail(const QString& sectionTitle, const QString& note, const FieldList& fields);
    void showDetailForItem(QTreeWidgetItem* item);

    FieldList SelfWrapperFields() const;
    FieldList SelfSegmentFields(int index) const;
    FieldList ElfHeaderFields() const;
    FieldList ProgramHeadersSummaryFields() const;
    FieldList ProgramHeaderFields(int index) const;
    FieldList SectionHeadersSummaryFields() const;
    FieldList SectionHeaderFields(int index) const;
    FieldList DynamicSectionFields() const;

    Loader::ElfInfo::ParsedInfo m_info;
    QString m_text; // flat dump, for the Raw Text tab / copy / save
    QString m_suggestedFileName;

    QTreeWidget* m_tree = nullptr;
    QLabel* m_formatLabel = nullptr; // always-visible "what kind of file is this" banner
    QLabel* m_detailTitle = nullptr;
    QLabel* m_detailNote = nullptr;
    QTableWidget* m_detailTable = nullptr;
    QPlainTextEdit* m_rawView = nullptr;
};
