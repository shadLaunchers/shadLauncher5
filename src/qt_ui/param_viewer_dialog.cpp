// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string>
#include <string_view>
#include <vector>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPixmap>
#include <QRegularExpression>
#include <QSplitter>
#include <QTextStream>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include "core/file_format/param.h"
#include "param_key_map.h"
#include "param_viewer_dialog.h"
#include "table_item_delegate.h"

ParamViewerDialog::ParamViewerDialog(QWidget* parent, const QString& paramPath)
    : QDialog(parent), m_paramPath(paramPath), m_paramInfo(paramPath) {
    setWindowTitle(tr("param.json viewer"));
    resize(1100, 560);
    setupUi();
    loadParam(m_paramPath);
}

ParamViewerDialog::~ParamViewerDialog() = default;

void ParamViewerDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    auto* toolbar = new QToolBar(this);

    QAction* reloadAct = toolbar->addAction(tr("Reload"));
    QAction* exportAct = toolbar->addAction(tr("Export"));
    QAction* openFolderAct = toolbar->addAction(tr("Open Folder"));
    QAction* copyAct = toolbar->addAction(tr("Copy Value"));

    mainLayout->addWidget(toolbar);

    connect(reloadAct, &QAction::triggered, this, &ParamViewerDialog::onReload);
    connect(exportAct, &QAction::triggered, this, &ParamViewerDialog::onExport);
    connect(openFolderAct, &QAction::triggered, this, &ParamViewerDialog::onOpenFolder);
    connect(copyAct, &QAction::triggered, this, &ParamViewerDialog::onCopyValue);

    auto* splitter = new QSplitter(this);

    // Left panel
    auto* leftWidget = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(8, 8, 8, 8);

    m_iconLabel = new QLabel(leftWidget);
    m_iconLabel->setFixedSize(128, 128);
    m_iconLabel->setFrameShape(QFrame::Box);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setScaledContents(true);
    leftLayout->addWidget(m_iconLabel);

    m_titleLabel = new QLabel(tr("Title: -"), leftWidget);
    m_titleLabel->setWordWrap(true);
    leftLayout->addWidget(m_titleLabel);
    m_titleIdLabel = new QLabel(tr("Title ID: -"), leftWidget);
    leftLayout->addWidget(m_titleIdLabel);
    m_contentIdLabel = new QLabel(tr("Content ID: -"), leftWidget);
    m_contentIdLabel->setWordWrap(true);
    leftLayout->addWidget(m_contentIdLabel);
    m_versionLabel = new QLabel(tr("Content version: -"), leftWidget);
    leftLayout->addWidget(m_versionLabel);
    m_categoryLabel = new QLabel(tr("Category: -"), leftWidget);
    leftLayout->addWidget(m_categoryLabel);

    m_localizedGroup = new QGroupBox(tr("Localized Titles"), leftWidget);
    auto* locLayout = new QVBoxLayout(m_localizedGroup);
    m_localizedList = new QListWidget(m_localizedGroup);
    locLayout->addWidget(m_localizedList);
    leftLayout->addWidget(m_localizedGroup);

    leftLayout->addStretch();

    // Right panel
    auto* rightWidget = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(8, 8, 8, 8);

    m_tabs = new QTabWidget(rightWidget);

    auto* fieldsTab = new QWidget(m_tabs);
    auto* fieldsLayout = new QVBoxLayout(fieldsTab);
    fieldsLayout->setContentsMargins(0, 8, 0, 0);

    auto* searchLayout = new QHBoxLayout();
    m_searchEdit = new QLineEdit(fieldsTab);
    m_searchEdit->setPlaceholderText(tr("Search..."));
    searchLayout->addWidget(m_searchEdit);

    m_regexCheck = new QCheckBox(tr("Regex"), fieldsTab);
    searchLayout->addWidget(m_regexCheck);
    fieldsLayout->addLayout(searchLayout);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &ParamViewerDialog::onSearchTextChanged);
    connect(m_regexCheck, &QCheckBox::toggled, this, &ParamViewerDialog::onRegexToggled);

    m_tableView = new QTableView(fieldsTab);
    m_model = new ParamModel(this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    // Match against every column, so searching for a JSON path works as well as
    // searching for a label or a value.
    m_proxy->setFilterKeyColumn(-1);
    m_tableView->setModel(m_proxy);
    m_tableView->setSortingEnabled(true);
    // setSortingEnabled() immediately sorts by column 0, which scrambles the
    // document into alphabetical-by-label order before the user asks for it.
    // Clearing the sort column restores source order until a header is clicked.
    m_tableView->sortByColumn(-1, Qt::AscendingOrder);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setItemDelegate(new TableItemDelegate(this, false));
    m_tableView->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    QFontMetrics fm(m_tableView->font());
    m_tableView->verticalHeader()->setMinimumSectionSize(fm.height() + 8);

    fieldsLayout->addWidget(m_tableView);
    m_tabs->addTab(fieldsTab, tr("Fields"));

    m_rawView = new QPlainTextEdit(m_tabs);
    m_rawView->setReadOnly(true);
    m_rawView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_rawView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_tabs->addTab(m_rawView, tr("Raw JSON"));

    rightLayout->addWidget(m_tabs);

    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter);

    auto* h = m_tableView->horizontalHeader();
    h->setStretchLastSection(false);
    h->setSectionResizeMode(ParamModel::FieldColumn, QHeaderView::ResizeToContents);
    h->setSectionResizeMode(ParamModel::KeyColumn, QHeaderView::ResizeToContents);
    h->setSectionResizeMode(ParamModel::ValueColumn, QHeaderView::Stretch);
    h->setSectionResizeMode(ParamModel::TypeColumn, QHeaderView::ResizeToContents);
}

bool ParamViewerDialog::loadParam(const QString& path) {
    QApplication::setOverrideCursor(Qt::WaitCursor);

    Param param;
    if (!param.Open(path.toStdWString())) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, tr("param.json Error"),
                             tr("Failed to load param.json:\n%1").arg(path));
        return false;
    }

    const std::vector<Param::Entry> flattened = param.GetEntries();
    std::vector<ParamEntry> entries;
    entries.reserve(flattened.size());
    for (const auto& e : flattened) {
        const std::string_view type_name = Param::TypeName(e.type);

        ParamEntry row;
        row.key = QString::fromStdString(e.key);
        row.lookupKey = QString::fromStdString(e.lookup_key);
        row.rawValue = QString::fromStdString(e.value);
        row.type = QString::fromUtf8(type_name.data(), qsizetype(type_name.size()));
        row.depth = e.depth;
        row.displayName = paramDisplayName(row.lookupKey);
        row.value = (e.type == ParamValueType::Object || e.type == ParamValueType::Array)
                        ? row.rawValue
                        : paramDecodeValue(row.lookupKey, row.rawValue);
        entries.push_back(std::move(row));
    }
    m_model->setEntries(entries);

    m_rawView->setPlainText(QString::fromStdString(param.PrettyJson()));

    // Summary panel
    const auto orDash = [](const std::string& s) {
        return s.empty() ? QStringLiteral("-") : QString::fromStdString(s);
    };
    m_titleLabel->setText(tr("Title: %1").arg(orDash(param.title)));
    m_titleIdLabel->setText(tr("Title ID: %1").arg(orDash(param.title_id)));
    m_contentIdLabel->setText(tr("Content ID: %1").arg(orDash(param.content_id)));
    m_versionLabel->setText(tr("Content version: %1").arg(orDash(param.app_ver)));
    m_categoryLabel->setText(tr("Category: %1").arg(orDash(param.category)));

    // Localized titles
    m_localizedList->clear();
    const QString defaultLocale = QString::fromStdString(param.default_language);
    if (!param.title.empty()) {
        m_localizedList->addItem(
            tr("[Default: %1] %2")
                .arg(paramLanguageName(defaultLocale), QString::fromStdString(param.title)));
    }
    for (const auto& [index, localized] : param.localized_titles) {
        const auto locale = Param::LocaleFromLanguage(index);
        const QString localeStr = QString::fromUtf8(locale.data(), qsizetype(locale.size()));
        // The default language already has a row above; don't repeat it.
        if (localeStr == defaultLocale) {
            continue;
        }
        m_localizedList->addItem(QStringLiteral("[%1] %2").arg(paramLanguageName(localeStr),
                                                               QString::fromStdString(localized)));
    }
    m_localizedGroup->setVisible(m_localizedList->count() > 0);

    // Load icon
    QString iconPath;
    for (const QString& ext : {QStringLiteral("png"), QStringLiteral("jpg")}) {
        const QString candidate = m_paramInfo.absolutePath() + QStringLiteral("/icon0.") + ext;
        if (QFile::exists(candidate)) {
            iconPath = candidate;
            break;
        }
    }

    QPixmap pix(iconPath);
    if (!pix.isNull()) {
        m_iconLabel->setPixmap(pix.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        m_iconLabel->setText(tr("No Icon"));
    }

    QApplication::restoreOverrideCursor();
    return true;
}

void ParamViewerDialog::onReload() {
    loadParam(m_paramPath);
}

void ParamViewerDialog::onExport() {
    // Name the export after the title ID, which is more use than "param".
    const QString suggested = m_titleIdLabel->text().section(QStringLiteral(": "), 1, 1);
    QString fileName = QFileDialog::getSaveFileName(this, tr("Export"), suggested,
                                                    tr("CSV (*.csv);;HTML (*.html)"));
    if (fileName.isEmpty()) {
        return;
    }

    if (fileName.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) {
        QFile f(fileName);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return;
        }
        QTextStream out(&f);
        out << "Key,Field,Value,Raw,Type\n";
        const auto escape = [](QString v) {
            v.replace(QStringLiteral("\""), QStringLiteral("\"\""));
            return v;
        };
        for (const auto& e : m_model->entries()) {
            out << '"' << escape(e.key) << "\",\"" << escape(e.displayName) << "\",\""
                << escape(e.value) << "\",\"" << escape(e.rawValue) << "\",\"" << escape(e.type)
                << "\"\n";
        }
        return;
    }

    if (fileName.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive)) {
        QFile f(fileName);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return;
        }
        QTextStream out(&f);

        out << "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n"
            << "<title>"
            << m_titleIdLabel->text().section(QStringLiteral(": "), 1, 1).toHtmlEscaped()
            << "</title>\n"
            << "<style>"
               "body { font-family: Arial, sans-serif; } "
               "table { border-collapse: collapse; margin-bottom: 20px; } "
               "th, td { border: 1px solid #ccc; padding: 4px; vertical-align: top; } "
               "th { background-color: #eee; } "
               "code { font-family: Consolas, monospace; }"
               "</style>\n</head>\n<body>\n";

        out << "<h1>" << m_titleLabel->text().toHtmlEscaped() << "</h1>\n";

        if (m_localizedList->count() != 0) {
            out << "<h2>Localized Titles</h2>\n<table>\n<tr><th>Language</th><th>Title</th></tr>\n";
            for (int i = 0; i < m_localizedList->count(); ++i) {
                const QString text = m_localizedList->item(i)->text();
                const QString lang =
                    text.section(QLatin1Char(']'), 0, 0).remove(QLatin1Char('[')).trimmed();
                const QString title = text.section(QLatin1Char(']'), 1).trimmed();
                out << "<tr><td>" << lang.toHtmlEscaped() << "</td><td>" << title.toHtmlEscaped()
                    << "</td></tr>\n";
            }
            out << "</table>\n";
        }

        out << "<h2>param.json Fields</h2>\n<table>\n"
            << "<tr><th>Key</th><th>Field</th><th>Value</th><th>Type</th></tr>\n";
        for (const auto& e : m_model->entries()) {
            out << "<tr><td><code>" << e.key.toHtmlEscaped() << "</code></td><td>"
                << e.displayName.toHtmlEscaped() << "</td><td>" << e.value.toHtmlEscaped()
                << "</td><td>" << e.type.toHtmlEscaped() << "</td></tr>\n";
        }
        out << "</table>\n</body>\n</html>\n";
        return;
    }
}

void ParamViewerDialog::onOpenFolder() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_paramInfo.absolutePath()));
}

void ParamViewerDialog::onCopyValue() {
    // On the raw tab, copy whatever the user has selected there instead.
    if (m_tabs->currentWidget() == m_rawView) {
        const QString selected = m_rawView->textCursor().selectedText();
        QApplication::clipboard()->setText(selected.isEmpty() ? m_rawView->toPlainText()
                                                              : selected);
        return;
    }

    const auto idx = m_tableView->currentIndex();
    if (!idx.isValid()) {
        return;
    }
    const auto src = m_proxy->mapToSource(idx);
    if (src.row() < 0 || src.row() >= int(m_model->entries().size())) {
        return;
    }
    QApplication::clipboard()->setText(m_model->entries()[src.row()].value);
}

// ---------------- Search ----------------
void ParamViewerDialog::onSearchTextChanged(const QString& text) {
    applyFilter(text);
}

void ParamViewerDialog::onRegexToggled(bool) {
    applyFilter(m_searchEdit->text());
}

void ParamViewerDialog::applyFilter(const QString& text) {
    if (text.isEmpty()) {
        m_proxy->setFilterRegularExpression(QRegularExpression());
        return;
    }

    QRegularExpression regex;
    if (m_regexCheck->isChecked()) {
        regex = QRegularExpression(text, QRegularExpression::CaseInsensitiveOption);
        if (!regex.isValid()) {
            regex.setPattern(QRegularExpression::escape(text));
        }
    } else {
        regex.setPattern(QRegularExpression::escape(text));
        regex.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    }

    m_proxy->setFilterRegularExpression(regex);
}
