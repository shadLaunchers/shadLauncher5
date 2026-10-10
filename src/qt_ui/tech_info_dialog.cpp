// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "tech_info_dialog.h"

#include <map>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

QString S(const std::string& s) {
    return QString::fromStdString(s);
}

QString S(std::string_view s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

QTableWidgetItem* ReadOnlyItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

// Sorts numerically while displaying formatted text.
class NumberItem : public QTableWidgetItem {
public:
    NumberItem(const QString& text, qulonglong value) : QTableWidgetItem(text), m_value(value) {
        setFlags(flags() & ~Qt::ItemIsEditable);
        setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    bool operator<(const QTableWidgetItem& other) const override {
        if (const auto* o = dynamic_cast<const NumberItem*>(&other)) {
            return m_value < o->m_value;
        }
        return QTableWidgetItem::operator<(other);
    }

private:
    qulonglong m_value;
};

QString KindLabel(Core::Analysis::ModuleKind kind) {
    switch (kind) {
    case Core::Analysis::ModuleKind::ThirdParty:
        return QObject::tr("Third-party");
    case Core::Analysis::ModuleKind::Sony:
        return QObject::tr("Sony / system");
    case Core::Analysis::ModuleKind::Unknown:
    default:
        return QObject::tr("Unidentified");
    }
}

} // namespace

TechInfoDialog::TechInfoDialog(const QString& title, const QString& serial,
                               Core::Analysis::GameTechReport report, QWidget* parent)
    : QDialog(parent), m_title(title), m_serial(serial), m_report(std::move(report)) {
    setWindowTitle(tr("Engine & Middleware - %1").arg(serial.isEmpty() ? title : serial));
    resize(940, 660);

    auto* layout = new QVBoxLayout(this);

    auto* heading = new QLabel(this);
    QFont heading_font = heading->font();
    heading_font.setBold(true);
    heading_font.setPointSize(heading_font.pointSize() + 2);
    heading->setFont(heading_font);
    heading->setText(tr("Engine: %1").arg(S(Core::Analysis::EngineSummary(m_report))));
    heading->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(heading);

    size_t third_party = 0, sony = 0, unknown = 0;
    for (const auto& m : m_report.modules) {
        switch (m.kind) {
        case Core::Analysis::ModuleKind::ThirdParty:
            third_party++;
            break;
        case Core::Analysis::ModuleKind::Sony:
            sony++;
            break;
        default:
            unknown++;
            break;
        }
    }
    auto* sub = new QLabel(this);
    sub->setWordWrap(true);
    QString sub_text =
        m_report.eboot_found
            ? tr("eboot.bin: %1 (%2), %3 strings scanned.")
                  .arg(QLocale().formattedDataSize(static_cast<qint64>(m_report.eboot_size)))
                  .arg(m_report.eboot_is_self ? tr("SELF") : tr("plain ELF"))
                  .arg(m_report.eboot_strings.string_count)
            : tr("eboot.bin was not found - only module names could be checked.");
    sub_text += QLatin1Char(' ') + tr("Modules: %1 third-party, %2 unidentified, %3 Sony/system.")
                                       .arg(third_party)
                                       .arg(unknown)
                                       .arg(sony);
    sub->setText(sub_text);
    layout->addWidget(sub);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(buildOverviewTab(), tr("Fingerprints"));
    tabs->addTab(buildModulesTab(), tr("Modules (%1)").arg(m_report.modules.size()));
    if (!m_report.lib_versions.empty()) {
        tabs->addTab(buildVersionsTab(), tr("SDK Versions (%1)").arg(m_report.lib_versions.size()));
    }
    layout->addWidget(tabs, 1);

    auto* note = new QLabel(
        tr("Detection is heuristic: it matches strings inside eboot.bin and the file names "
           "of PRX modules, so it can miss engines that strip identifying strings and can "
           "over-report libraries that are only mentioned."),
        this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(note);

    auto* buttons = new QHBoxLayout();
    auto* copy_btn = new QPushButton(tr("Copy Summary"), this);
    auto* json_btn = new QPushButton(tr("Export JSON..."), this);
    auto* close_btn = new QPushButton(tr("Close"), this);
    buttons->addWidget(copy_btn);
    buttons->addWidget(json_btn);
    buttons->addStretch();
    buttons->addWidget(close_btn);
    layout->addLayout(buttons);

    connect(copy_btn, &QPushButton::clicked, this, &TechInfoDialog::onCopySummary);
    connect(json_btn, &QPushButton::clicked, this, &TechInfoDialog::onExportJson);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
}

QTreeWidgetItem* TechInfoDialog::addSection(QTreeWidget* tree, const QString& label, int count) {
    auto* item = new QTreeWidgetItem(
        tree, {count >= 0 ? tr("%1 (%2)").arg(label).arg(count) : label, QString()});
    QFont f = item->font(0);
    f.setBold(true);
    item->setFont(0, f);
    return item;
}

void TechInfoDialog::addDetection(QTreeWidgetItem* parent, const Core::Analysis::Detection& d,
                                  bool show_confidence) {
    auto* item = new QTreeWidgetItem(
        parent,
        {S(d.value), show_confidence
                         ? tr("%1% confidence (score %2)").arg(d.confidence).arg(d.score)
                         : tr("%n match(es)", nullptr, static_cast<int>(d.evidence.size()))});
    for (const auto& ev : d.evidence) {
        auto* child = new QTreeWidgetItem(item, {S(ev), tr("evidence")});
        child->setToolTip(0, S(ev));
    }
}

QWidget* TechInfoDialog::buildOverviewTab() {
    auto* tree = new QTreeWidget(this);
    tree->setColumnCount(2);
    tree->setHeaderLabels({tr("Finding"), tr("Details")});
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree->header()->setStretchLastSection(false);

    const auto& s = m_report.eboot_strings;

    auto* engine = addSection(tree, tr("Engine"));
    if (s.engine) {
        addDetection(engine, *s.engine, true);
    }
    for (const auto& hint : m_report.layout_engine_hints) {
        new QTreeWidgetItem(engine, {S(hint), tr("from file layout")});
    }
    if (engine->childCount() == 0) {
        new QTreeWidgetItem(engine, {tr("No known engine fingerprint found"), QString()});
    }
    engine->setExpanded(true);

    auto add_list = [&](const QString& label, const std::vector<Core::Analysis::Detection>& v,
                        bool expand) {
        if (v.empty()) {
            return;
        }
        auto* sec = addSection(tree, label, static_cast<int>(v.size()));
        for (const auto& d : v) {
            addDetection(sec, d, d.confidence != 0);
        }
        sec->setExpanded(expand);
    };

    // Middleware recognized from module file names, grouped by product.
    {
        std::map<QString, QStringList> by_product;
        for (const auto& m : m_report.modules) {
            if (m.kind == Core::Analysis::ModuleKind::ThirdParty) {
                by_product[S(m.vendor) + QStringLiteral(" - ") + S(m.product)] << S(m.file_name);
            }
        }
        if (!by_product.empty()) {
            auto* sec =
                addSection(tree, tr("Middleware modules"), static_cast<int>(by_product.size()));
            for (const auto& [product, files] : by_product) {
                auto* item = new QTreeWidgetItem(
                    sec, {product, tr("%n module(s)", nullptr, static_cast<int>(files.size()))});
                for (const auto& f : files) {
                    new QTreeWidgetItem(item, {f, QString()});
                }
            }
            sec->setExpanded(true);
        }
    }

    add_list(tr("Third-party libraries (eboot strings)"), s.third_party_libs, true);
    add_list(tr("Library version strings"), s.detected_versions, false);
    add_list(tr("SDK hints"), s.sdk_hints, false);
    add_list(tr("Custom engine forks"), s.custom_forks, true);

    if (s.build_system || s.source_depot || !s.project_paths.empty()) {
        auto* sec = addSection(tree, tr("Build breadcrumbs"));
        if (s.build_system) {
            auto* item = new QTreeWidgetItem(sec, {tr("Build system"), S(s.build_system->value)});
            for (const auto& ev : s.build_system->evidence) {
                new QTreeWidgetItem(item, {S(ev), tr("evidence")});
            }
        }
        if (s.source_depot) {
            auto* item = new QTreeWidgetItem(sec, {tr("Source depot"), S(s.source_depot->value)});
            for (const auto& ev : s.source_depot->evidence) {
                new QTreeWidgetItem(item, {S(ev), tr("evidence")});
            }
        }
        for (const auto& p : s.project_paths) {
            auto* item = new QTreeWidgetItem(sec, {tr("Project"), S(p.value)});
            for (const auto& ev : p.evidence) {
                new QTreeWidgetItem(item, {S(ev), tr("evidence")});
            }
        }
        sec->setExpanded(true);
    }

    if (!s.source_paths.empty()) {
        auto* sec =
            addSection(tree, tr("Engine source paths"), static_cast<int>(s.source_paths.size()));
        for (const auto& p : s.source_paths) {
            new QTreeWidgetItem(sec, {S(p), QString()});
        }
    }
    if (!s.sce_libraries.empty()) {
        auto* sec = addSection(tree, tr("SCE libraries referenced"),
                               static_cast<int>(s.sce_libraries.size()));
        for (const auto& p : s.sce_libraries) {
            new QTreeWidgetItem(sec, {S(p), QString()});
        }
    }
    return tree;
}

QWidget* TechInfoDialog::buildModulesTab() {
    enum { ColModule, ColKind, ColVendor, ColProduct, ColImports, ColExports, ColSize, ColPath };
    auto* table = new QTableWidget(static_cast<int>(m_report.modules.size()), 8, this);
    table->setHorizontalHeaderLabels({tr("Module"), tr("Kind"), tr("Vendor"), tr("Product"),
                                      tr("Imports"), tr("Exports"), tr("Size"), tr("Path")});
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);

    for (int row = 0; row < static_cast<int>(m_report.modules.size()); row++) {
        const auto& m = m_report.modules[static_cast<size_t>(row)];
        auto* name = ReadOnlyItem(S(m.module_name));
        QStringList tip;
        if (!m.description.empty()) {
            tip << S(m.description);
        }
        if (!m.parseable) {
            tip << tr("Not parseable as SELF/ELF (encrypted, or not a module).");
        }
        if (!m.import_libs.empty()) {
            QStringList libs;
            for (const auto& l : m.import_libs) {
                libs << S(l);
            }
            tip << tr("Imports from: %1").arg(libs.join(QStringLiteral(", ")));
        }
        name->setToolTip(tip.join(QLatin1Char('\n')));
        table->setItem(row, ColModule, name);
        table->setItem(row, ColKind, ReadOnlyItem(KindLabel(m.kind)));
        table->setItem(row, ColVendor, ReadOnlyItem(S(m.vendor)));
        table->setItem(row, ColProduct, ReadOnlyItem(S(m.product)));
        table->setItem(
            row, ColImports,
            new NumberItem(m.parseable ? QString::number(m.imports) : QStringLiteral("-"),
                           m.imports));
        table->setItem(
            row, ColExports,
            new NumberItem(m.parseable ? QString::number(m.exports) : QStringLiteral("-"),
                           m.exports));
        table->setItem(
            row, ColSize,
            new NumberItem(QLocale().formattedDataSize(static_cast<qint64>(m.size)), m.size));
        table->setItem(row, ColPath, ReadOnlyItem(S(m.rel_path)));
    }
    // Keep the report's order (third-party, unidentified, Sony) until the user
    // clicks a header; enabling sorting would otherwise re-sort by column 0.
    table->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    table->setSortingEnabled(true);
    table->resizeColumnsToContents();
    table->horizontalHeader()->setSectionResizeMode(ColPath, QHeaderView::Stretch);

    if (m_report.modules.empty()) {
        auto* label = new QLabel(tr("No PRX/SPRX modules were found in this game."), this);
        label->setAlignment(Qt::AlignCenter);
        table->deleteLater();
        return label;
    }
    return table;
}

QWidget* TechInfoDialog::buildVersionsTab() {
    auto* table = new QTableWidget(static_cast<int>(m_report.lib_versions.size()), 3, this);
    table->setHorizontalHeaderLabels({tr("Library"), tr("Version (guessed)"), tr("Raw")});
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    for (int row = 0; row < static_cast<int>(m_report.lib_versions.size()); row++) {
        const auto& v = m_report.lib_versions[static_cast<size_t>(row)];
        table->setItem(row, 0, ReadOnlyItem(S(v.name)));
        table->setItem(row, 1, ReadOnlyItem(S(v.GuessedVersionString())));
        table->setItem(
            row, 2,
            ReadOnlyItem(QStringLiteral("0x%1").arg(v.version_raw, 8, 16, QLatin1Char('0'))));
    }
    table->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    table->setSortingEnabled(true);
    table->resizeColumnsToContents();
    table->horizontalHeader()->setStretchLastSection(true);
    return table;
}

QString TechInfoDialog::summaryText() const {
    const auto& s = m_report.eboot_strings;
    QStringList lines;
    lines << QStringLiteral("%1 [%2]").arg(m_title, m_serial);
    lines << tr("Engine: %1").arg(S(Core::Analysis::EngineSummary(m_report)));

    QStringList middleware;
    for (const auto& m : m_report.modules) {
        if (m.kind == Core::Analysis::ModuleKind::ThirdParty) {
            const QString p = S(m.product);
            if (!middleware.contains(p)) {
                middleware << p;
            }
        }
    }
    for (const auto& d : s.third_party_libs) {
        const QString p = S(d.value);
        if (!middleware.contains(p)) {
            middleware << p;
        }
    }
    if (!middleware.isEmpty()) {
        lines << tr("Middleware: %1").arg(middleware.join(QStringLiteral(", ")));
    }
    if (s.build_system) {
        lines << tr("Build system: %1").arg(S(s.build_system->value));
    }
    if (s.source_depot) {
        lines << tr("Source depot: %1").arg(S(s.source_depot->value));
    }
    return lines.join(QLatin1Char('\n'));
}

void TechInfoDialog::onCopySummary() {
    QApplication::clipboard()->setText(summaryText());
}

void TechInfoDialog::onExportJson() {
    const QString suggested = (m_serial.isEmpty() ? QStringLiteral("game") : m_serial) +
                              QStringLiteral("_tech_info.json");
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Engine & Middleware Report"),
                                                      suggested, tr("JSON Files (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not write to %1").arg(path));
        return;
    }
    file.write(QByteArray::fromStdString(
        Core::Analysis::ReportToJson(m_report, m_title.toStdString(), m_serial.toStdString())));
}
