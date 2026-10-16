// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "pkg_info_dialog.h"

#include <filesystem>
#include <functional>
#include <map>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cmrc/cmrc.hpp>
#include "common/path_util.h"
#include "core/file_format/dds.h"
#include "core/file_format/ucp.h"
#include "core/file_format/uds.h"
#include "npbind_dialog.h"
#include "param_viewer_dialog.h"

CMRC_DECLARE(res);

namespace {

constexpr int kIconSize = 128;

QString S(const std::string& s) {
    return QString::fromStdString(s);
}

QString S(std::string_view s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

QString Hex(qulonglong v, int width = 0) {
    return QStringLiteral("0x") +
           QString::number(v, 16).rightJustified(width, QLatin1Char('0')).toUpper();
}

QString Size(qulonglong v) {
    return QStringLiteral("%1 (%2)").arg(QLocale().formattedDataSize(static_cast<qint64>(v)),
                                         QLocale().toString(v));
}

QTableWidgetItem* Cell(const QString& text, bool right = false) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    if (right) {
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    return item;
}

class NumberCell : public QTableWidgetItem {
public:
    NumberCell(const QString& text, qulonglong value) : QTableWidgetItem(text), m_value(value) {
        setFlags(flags() & ~Qt::ItemIsEditable);
        setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    bool operator<(const QTableWidgetItem& other) const override {
        if (const auto* o = dynamic_cast<const NumberCell*>(&other)) {
            return m_value < o->m_value;
        }
        return QTableWidgetItem::operator<(other);
    }

private:
    qulonglong m_value;
};

constexpr int kEntryIndexRole = Qt::UserRole + 1;
constexpr int kArtworkPreview = 360;
constexpr qulonglong kMaxPreviewBytes = 2u << 20;

QImage DecodeArtwork(const PS5Pkg::ArtworkData& art, QString* format) {
    if (art.is_dds) {
        const auto img = Dds::Decode(art.bytes);
        if (!img) {
            return {};
        }
        *format = QString::fromStdString(img->format);
        return QImage(img->rgba.data(), static_cast<int>(img->width), static_cast<int>(img->height),
                      static_cast<int>(img->width) * 4, QImage::Format_RGBA8888)
            .copy();
    }
    *format = QStringLiteral("PNG");
    QImage image;
    image.loadFromData(art.bytes.data(), static_cast<int>(art.bytes.size()));
    return image;
}

QString TextPreview(const std::vector<u8>& data) {
    if (data.size() > kMaxPreviewBytes) {
        return {};
    }
    for (const u8 c : data) {
        if (c == 0 || (c < 0x20 && c != '\n' && c != '\r' && c != '\t')) {
            return {};
        }
    }
    return QString::fromUtf8(reinterpret_cast<const char*>(data.data()),
                             static_cast<qsizetype>(data.size()));
}

QString HexPreview(const std::vector<u8>& data, size_t max_bytes = 512) {
    QString out;
    const size_t n = std::min(data.size(), max_bytes);
    for (size_t row = 0; row < n; row += 16) {
        QString hex;
        QString ascii;
        for (size_t i = row; i < row + 16; i++) {
            if (i < n) {
                hex += QStringLiteral("%1 ").arg(data[i], 2, 16, QLatin1Char('0'));
                ascii += data[i] >= 0x20 && data[i] < 0x7F ? QChar(data[i]) : QLatin1Char('.');
            } else {
                hex += QStringLiteral("   ");
            }
        }
        out += QStringLiteral("%1  %2 %3\n").arg(row, 8, 16, QLatin1Char('0')).arg(hex, ascii);
    }
    if (data.size() > n) {
        out += QObject::tr("... (%1 more bytes)").arg(data.size() - n);
    }
    return out;
}

QTableWidget* MakeTable(QWidget* parent, const QStringList& headers, int stretch_column) {
    auto* t = new QTableWidget(parent);
    t->setColumnCount(static_cast<int>(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    if (stretch_column >= 0) {
        t->horizontalHeader()->setSectionResizeMode(stretch_column, QHeaderView::Stretch);
    }
    return t;
}

} // namespace

PkgInfoDialog::PkgInfoDialog(const QString& path, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("PS5 PKG Info"));
    setAcceptDrops(true);
    resize(900, 640);

    auto* layout = new QVBoxLayout(this);

    auto* header = new QHBoxLayout();
    m_icon = new QLabel(this);
    m_icon->setFixedSize(kIconSize, kIconSize);
    m_icon->setAlignment(Qt::AlignCenter);
    m_icon->setFrameShape(QFrame::StyledPanel);
    header->addWidget(m_icon, 0, Qt::AlignTop);

    auto* text_col = new QVBoxLayout();
    m_title = new QLabel(this);
    QFont title_font = m_title->font();
    title_font.setBold(true);
    title_font.setPointSize(title_font.pointSize() + 3);
    m_title->setFont(title_font);
    m_title->setWordWrap(true);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_subtitle = new QLabel(this);
    m_subtitle->setWordWrap(true);
    m_subtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    text_col->addWidget(m_title);
    text_col->addWidget(m_subtitle);
    text_col->addWidget(m_status);
    text_col->addStretch();
    header->addLayout(text_col, 1);
    layout->addLayout(header);

    m_tabs = new QTabWidget(this);

    m_overview = new QTreeWidget(this);
    m_overview->setColumnCount(2);
    m_overview->setHeaderLabels({tr("Field"), tr("Value")});
    m_overview->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_overview->header()->setStretchLastSection(true);
    m_overview->setRootIsDecorated(true);
    m_tabs->addTab(m_overview, tr("Overview"));

    m_tabs->addTab(buildArtworkTab(), tr("Artwork"));
    m_tabs->addTab(buildTrophiesTab(), tr("Trophies"));
    m_tabs->addTab(buildActivitiesTab(), tr("Activities"));
    m_tabs->addTab(buildPlayGoTab(), tr("PlayGo"));
    m_tabs->addTab(buildFilesTab(), tr("Game Files"));
    m_tabs->addTab(buildEntriesTab(), tr("Entries"));
    m_tabs->addTab(buildSegmentsTab(), tr("Segments"));
    m_tabs->addTab(buildSiTab(), tr("Install Metadata"));
    m_tabs->addTab(buildKeysTab(), tr("Keys"));

    m_tabs->addTab(buildParamTab(), QStringLiteral("param.json"));

    layout->addWidget(m_tabs, 1);

    auto* buttons = new QHBoxLayout();
    auto* open_btn = new QPushButton(tr("Open PKG..."), this);
    m_copy = new QPushButton(tr("Copy Summary"), this);
    auto* close_btn = new QPushButton(tr("Close"), this);
    buttons->addWidget(open_btn);
    buttons->addWidget(m_copy);
    m_export = new QPushButton(tr("Export Report..."), this);
    m_export->setToolTip(tr("Save everything shown here as a JSON or text report."));
    buttons->addWidget(m_export);
    buttons->addStretch();
    buttons->addWidget(close_btn);
    layout->addLayout(buttons);

    connect(open_btn, &QPushButton::clicked, this, &PkgInfoDialog::onOpen);
    connect(m_copy, &QPushButton::clicked, this, &PkgInfoDialog::onCopySummary);
    connect(m_export, &QPushButton::clicked, this, &PkgInfoDialog::onExportReport);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);

    if (!path.isEmpty()) {
        Load(path);
    } else {
        populate();
    }
}

QWidget* PkgInfoDialog::buildEntriesTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    m_entries = MakeTable(
        page, {tr("ID"), tr("Name"), tr("Offset"), tr("Size"), tr("Flags"), tr("Encrypted")}, 1);
    m_entries->setSortingEnabled(true);
    layout->addWidget(m_entries, 1);
    auto* row = new QHBoxLayout();
    m_save_entry = new QPushButton(tr("Save Entry..."), page);
    m_save_entry->setToolTip(tr("Save the selected metadata entry (for example icon0.png or "
                                "param.json) to a file."));
    row->addWidget(m_save_entry);
    m_open_npbind = new QPushButton(tr("Open in NPBind Viewer"), page);
    m_open_npbind->setToolTip(
        tr("Decrypt the selected npbind.dat and show it in the NPBind viewer."));
    row->addWidget(m_open_npbind);
    row->addStretch();
    layout->addLayout(row);
    connect(m_save_entry, &QPushButton::clicked, this, &PkgInfoDialog::onSaveEntry);
    connect(m_open_npbind, &QPushButton::clicked, this, &PkgInfoDialog::onOpenNpbind);
    connect(m_entries, &QTableWidget::itemSelectionChanged, this,
            &PkgInfoDialog::onEntrySelectionChanged);
    connect(m_entries, &QTableWidget::cellDoubleClicked, this, [this] { onSaveEntry(); });
    return page;
}

QWidget* PkgInfoDialog::buildArtworkTab() {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    m_artwork_page = new QWidget(scroll);
    auto* outer = new QVBoxLayout(m_artwork_page);
    m_artwork_empty = new QLabel(m_artwork_page);
    m_artwork_empty->setWordWrap(true);
    outer->addWidget(m_artwork_empty);
    m_artwork_grid = new QGridLayout();
    m_artwork_grid->setHorizontalSpacing(16);
    m_artwork_grid->setVerticalSpacing(16);
    outer->addLayout(m_artwork_grid);
    outer->addStretch();
    scroll->setWidget(m_artwork_page);
    return scroll;
}

QWidget* PkgInfoDialog::buildSegmentsTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    m_segments =
        MakeTable(page, {tr("Segment"), tr("Offset"), tr("Size"), tr("End"), tr("Contents")}, 4);
    layout->addWidget(m_segments, 1);
    m_segments_note = new QLabel(page);
    m_segments_note->setWordWrap(true);
    layout->addWidget(m_segments_note);
    return page;
}

QWidget* PkgInfoDialog::buildSiTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* split = new QSplitter(Qt::Vertical, page);
    m_si = MakeTable(split, {tr("File"), tr("Size"), tr("Stored"), tr("Method")}, 0);
    m_si_preview = new QPlainTextEdit(split);
    m_si_preview->setReadOnly(true);
    m_si_preview->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_si_preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_si_preview->setPlaceholderText(tr("Select a file to preview it."));
    split->addWidget(m_si);
    split->addWidget(m_si_preview);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);
    auto* row = new QHBoxLayout();
    m_save_si = new QPushButton(tr("Save File..."), page);
    row->addWidget(m_save_si);
    row->addStretch();
    layout->addLayout(row);
    connect(m_save_si, &QPushButton::clicked, this, &PkgInfoDialog::onSaveSiMember);
    connect(m_si, &QTableWidget::itemSelectionChanged, this, &PkgInfoDialog::onSiSelectionChanged);
    connect(m_si, &QTableWidget::cellDoubleClicked, this, [this] { onSaveSiMember(); });
    return page;
}

QString PkgInfoDialog::PickPackage(QWidget* parent) {
    return QFileDialog::getOpenFileName(parent, tr("Open PS5 Package"), QString(),
                                        tr("PS5 packages (*.pkg);;All files (*)"));
}

void PkgInfoDialog::Load(const QString& path) {
    m_path = path;
    m_info = PS5Pkg::ReadPkgInfo(path.toStdU16String());
    populate();
}

void PkgInfoDialog::dragEnterEvent(QDragEnterEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (urls.size() == 1 && urls.front().isLocalFile()) {
        event->acceptProposedAction();
    }
}

void PkgInfoDialog::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (urls.size() == 1 && urls.front().isLocalFile()) {
        Load(urls.front().toLocalFile());
        event->acceptProposedAction();
    }
}

void PkgInfoDialog::onOpen() {
    const QString path = PickPackage(this);
    if (!path.isEmpty()) {
        Load(path);
    }
}

QString PkgInfoDialog::statusText() const {
    using PS5Pkg::PackageKind;
    switch (m_info.kind) {
    case PackageKind::Debug:
        if (m_keys.plaintext) {
            return tr("Its filesystem is stored unencrypted (verified); install it with File > "
                      "Install Packages, or save single files from Game Files.");
        }
        return tr("Its files are encrypted with keys derived from the content ID, so they can "
                  "be installed (File > Install Packages) without console keys.");
    case PackageKind::Retail:
        if (m_keys.plaintext) {
            return tr("Its header is marked retail, but its filesystem is stored unencrypted "
                      "(verified), so it can be installed without any keys.");
        }
        if (keysMatched()) {
            return tr("Its header is marked retail, but its keys derive from the content ID "
                      "(verified), so it can be installed without console keys.");
        }
        return tr("Its files are encrypted with console keys and cannot be "
                  "extracted here.");
    case PackageKind::Patch:
        return tr("Update package. Its metadata is shown here; its files are encrypted and "
                  "cannot be extracted yet.");
    case PackageKind::MetadataOnly:
        return tr("It describes a package but holds no game files.");
    case PackageKind::PS4:
        return tr("This is not a PS5 package. Use shadLauncher4 to install it.");
    case PackageKind::Unknown:
    default:
        return tr("Not a recognized PS5 package.");
    }
}

void PkgInfoDialog::populate() {
    // Temporary copies belong to the previous package.
    m_temp_dir.reset();
    const bool loaded = !m_path.isEmpty();
    setWindowTitle(loaded ? tr("PS5 PKG Info - %1").arg(QFileInfo(m_path).fileName())
                          : tr("PS5 PKG Info"));

    // Header: icon, title, ids, status.
    populateArtwork();
    m_icon->clear();
    m_icon->setText(tr("No icon"));
    for (const auto& art : m_artwork) {
        if (art.name == QStringLiteral("icon0")) {
            m_icon->setPixmap(QPixmap::fromImage(art.image.scaled(
                kIconSize, kIconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        }
    }

    if (!loaded) {
        m_title->setText(tr("No package loaded"));
        m_subtitle->setText(tr("Open a .pkg file or drop one onto this window."));
        m_status->clear();
    } else {
        const auto& p = m_info.param;
        QString title = p && !p->title.empty() ? S(p->title) : QFileInfo(m_path).fileName();
        m_title->setText(title);

        QStringList ids;
        if (p && !p->title_id.empty()) {
            ids << S(p->title_id);
        }
        if (!m_info.content_id.empty()) {
            ids << S(m_info.content_id);
        }
        if (p && !p->app_ver.empty()) {
            ids << tr("Version %1").arg(S(p->app_ver));
        }
        // Only patch packages (LIH) are labelled as patches here; a full image whose
        // content flags carry patch bits is shown in the Overview instead.
        if (m_info.has_lih && m_info.patch_kind != PS5Pkg::PatchKind::None) {
            ids << S(PS5Pkg::PatchKindName(m_info.patch_kind));
        }
        if (!m_info.content_id.empty()) {
            ids << S(PS5Pkg::RegionFromContentId(m_info.content_id));
        }
        ids << QLocale().formattedDataSize(static_cast<qint64>(m_info.file_size));
        m_subtitle->setText(ids.join(QStringLiteral("  ·  ")));

        updateHeaderStatus();
    }

    populateOverview();
    populateEntries();
    populateSegments();
    populateSi();
    populateKeys();
    if (m_verify->isEnabled()) {
        onVerifyKeys();
    } else {
        refreshDecryptedViews();
    }

    if (m_info.param) {
        m_param->setPlainText(S(m_info.param->PrettyJson()));
    } else {
        m_param->setPlainText(loaded && !m_info.param_problem.empty() ? S(m_info.param_problem)
                                                                      : QString());
    }
    m_tabs->setTabText(m_tabs->indexOf(m_artwork_page->parentWidget()->parentWidget()),
                       tr("Artwork (%1)").arg(m_artwork.size()));
    m_tabs->setTabText(m_tabs->indexOf(m_entries->parentWidget()),
                       tr("Entries (%1)").arg(m_info.entries.size()));
    m_tabs->setTabText(m_tabs->indexOf(m_si->parentWidget()->parentWidget()),
                       tr("Install Metadata (%1)").arg(m_info.si_members.size()));
    m_copy->setEnabled(loaded);
    m_export->setEnabled(loaded);
    onEntrySelectionChanged();
}

QTreeWidgetItem* PkgInfoDialog::addSection(const QString& label) {
    auto* item = new QTreeWidgetItem(m_overview, {label});
    QFont f = item->font(0);
    f.setBold(true);
    item->setFont(0, f);
    item->setFirstColumnSpanned(true);
    item->setExpanded(true);
    return item;
}

void PkgInfoDialog::addRow(QTreeWidgetItem* section, const QString& key, const QString& value) {
    auto* item = new QTreeWidgetItem(section, {key, value});
    item->setToolTip(1, value);
}

void PkgInfoDialog::populateOverview() {
    m_overview->clear();
    if (m_path.isEmpty()) {
        return;
    }
    const auto& i = m_info;

    auto* pkg = addSection(tr("Package"));
    addRow(pkg, tr("File"), QDir::toNativeSeparators(m_path));
    addRow(pkg, tr("File size"), Size(i.file_size));
    addRow(pkg, tr("Type"), kindLabel());
    if (i.has_cnt) {
        addRow(pkg, tr("Content ID"), S(i.content_id));
        addRow(pkg, tr("Content type"), S(PS5Pkg::ContentTypeName(i.content_type)));
        addRow(pkg, tr("DRM type"), S(PS5Pkg::DrmTypeName(i.drm_type)));
        addRow(pkg, tr("Region"), S(PS5Pkg::RegionFromContentId(i.content_id)));
        if (i.has_lih) {
            addRow(pkg, tr("Patch type"), S(PS5Pkg::PatchKindName(i.patch_kind)));
        } else if (i.patch_kind != PS5Pkg::PatchKind::None) {
            addRow(pkg, tr("Patch flags"),
                   tr("%1 (built from a patch; this is a full image)")
                       .arg(S(PS5Pkg::PatchKindName(i.patch_kind))));
        }
    }

    if (i.param) {
        const auto& p = *i.param;
        auto* app = addSection(tr("Application (param.json)"));
        addRow(app, tr("Title"), S(p.title));
        addRow(app, tr("Title ID"), S(p.title_id));
        if (!p.concept_id.empty()) {
            addRow(app, tr("Concept ID"), S(p.concept_id));
        }
        if (!p.category.empty()) {
            addRow(app, tr("Category"), S(p.category));
        }
        if (!p.drm_type.empty()) {
            addRow(app, tr("Application DRM type"), S(p.drm_type));
        }
        addRow(app, tr("Content version"), S(p.app_ver));
        if (!p.master_version.empty()) {
            addRow(app, tr("Master version"), S(p.master_version));
        }
        addRow(app, tr("Required system software"), S(p.system_ver_string));
        addRow(app, tr("SDK version"), S(p.sdk_ver_string));
        // Fields the Param class doesn't model, looked up by their JSON path.
        std::map<std::string, std::string> fields;
        std::vector<std::pair<QString, QString>> titles;
        static constexpr std::string_view kTitlePrefix = "localizedParameters.";
        static constexpr std::string_view kTitleSuffix = ".titleName";
        for (const auto& e : p.GetEntries()) {
            fields.emplace(e.key, e.value);
            if (e.key.starts_with(kTitlePrefix) && e.key.ends_with(kTitleSuffix) &&
                e.key.size() > kTitlePrefix.size() + kTitleSuffix.size()) {
                titles.emplace_back(
                    S(e.key.substr(kTitlePrefix.size(),
                                   e.key.size() - kTitlePrefix.size() - kTitleSuffix.size())),
                    S(e.value));
            }
        }
        auto field = [&](const char* key, const QString& label) {
            // Some packages pad values with spaces; show them trimmed.
            if (const auto it = fields.find(key); it != fields.end()) {
                const QString value = S(it->second).trimmed();
                if (!value.isEmpty()) {
                    addRow(app, label, value);
                }
            }
        };
        field("targetContentVersion", tr("Target content version"));
        field("originContentVersion", tr("Origin content version"));
        field("contentBadgeType", tr("Content badge type"));
        if (const auto it = fields.find("downloadDataSize"); it != fields.end()) {
            bool ok = false;
            const qulonglong mib = S(it->second).toULongLong(&ok);
            addRow(app, tr("Download data size"),
                   !ok ? S(it->second)
                   : mib == 0
                       ? tr("None")
                       : QLocale().formattedDataSize(static_cast<qint64>(mib) * 1024 * 1024));
        }
        field("ageLevel.default", tr("Age level (default)"));
        field("pubtools.creationDate", tr("Creation date"));
        field("pubtools.toolVersion", tr("Tool version"));
        field("versionFileUri", tr("Version file URI"));
        if (!p.default_language.empty()) {
            addRow(app, tr("Default language"), S(p.default_language));
        }
        if (!titles.empty()) {
            auto* t = new QTreeWidgetItem(
                app, {tr("Localized titles"),
                      tr("%n language(s)", nullptr, static_cast<int>(titles.size()))});
            for (const auto& [locale, name] : titles) {
                auto* item = new QTreeWidgetItem(t, {locale, name});
                item->setToolTip(1, name);
            }
        }
    }

    if (i.has_cnt && contentIdKeys()) {
        const auto np = PS5Pkg::ReadNpInfo(i, m_passcode->text().toStdString());
        if (!np.IsEmpty() || !np.problems.empty()) {
            auto* psn = addSection(tr("PlayStation Network and licence"));
            if (!np.np_comm_ids.empty()) {
                QStringList ids;
                for (const auto& id : np.np_comm_ids) {
                    ids << S(id);
                }
                addRow(psn, tr("NP communication ID"), ids.join(QStringLiteral(", ")));
            }
            if (!np.np_title_id.empty()) {
                addRow(psn, tr("NP title ID"), S(np.np_title_id));
            }
            if (np.has_license) {
                addRow(psn, tr("Licence"), tr("RIF version %1").arg(np.rif_version));
                addRow(psn, tr("Licensed to"),
                       np.account_id == 0 ? tr("Any account (not bound to a user)")
                                          : tr("Account %1").arg(Hex(np.account_id, 16)));
                auto when = [](u64 secs) {
                    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(secs))
                        .toUTC()
                        .toString(QStringLiteral("yyyy-MM-dd HH:mm 'UTC'"));
                };
                if (np.start_time != 0) {
                    addRow(psn, tr("Valid from"), when(np.start_time));
                }
                constexpr u64 kNever = 0x7FFFFFFFFFFFFFFFull;
                addRow(psn, tr("Expires"),
                       np.expiry_time == 0 || np.expiry_time >= kNever ? tr("Never")
                                                                       : when(np.expiry_time));
                if (!np.license_content_id.empty() && np.license_content_id != i.content_id) {
                    addRow(psn, tr("Licence content ID"),
                           tr("%1 (differs from the package!)").arg(S(np.license_content_id)));
                }
            }
            // "name: reason" -- collapse to one row when every entry fails the same way.
            QStringList names, reasons;
            for (const auto& p : np.problems) {
                const QString text = S(p);
                const int colon = text.indexOf(QStringLiteral(": "));
                names << text.left(colon);
                reasons << text.mid(colon + 2);
            }
            reasons.removeDuplicates();
            if (reasons.size() == 1) {
                addRow(psn, tr("Not readable"),
                       tr("%1 (%2)").arg(reasons.front(), names.join(QStringLiteral(", "))));
            } else {
                for (const auto& p : np.problems) {
                    addRow(psn, tr("Not readable"), S(p));
                }
            }
        }
    }

    if (i.has_lih) {
        auto* lih = addSection(tr("Patch image"));
        addRow(lih, tr("Header version"), QString::number(i.lih_version));
        addRow(lih, tr("Metadata container offset"), Hex(i.cnt_offset));
    }

    if (i.has_fih) {
        auto* fih = addSection(tr("Finalized image"));
        addRow(fih, tr("Signed byte"),
               tr("%1 (%2)").arg(Hex(i.signed_byte, 2),
                                 i.signed_byte == 0 ? tr("debug") : tr("retail")));
        addRow(fih, tr("Format version"), QString::number(i.format_version));
        addRow(fih, tr("Filesystem image offset"), Hex(i.pfs_offset));
        addRow(fih, tr("Filesystem image size"), Size(i.pfs_size));
        addRow(fih, tr("Metadata container offset"), Hex(i.cnt_offset));
        if (i.inner_image_size != 0) {
            addRow(fih, tr("Game data size"), Size(i.inner_image_size));
        }
        if (i.app_file_count != 0) {
            addRow(fih, tr("Game files (outside sce_sys)"),
                   QLocale().toString(static_cast<qulonglong>(i.app_file_count)));
        }
        if (i.content_inode_count != 0) {
            addRow(fih, tr("Files and folders"),
                   QLocale().toString(static_cast<qulonglong>(i.content_inode_count)));
        }
        if (const auto ver = PS5Pkg::FormatContentVersion(i.content_version_echo); !ver.empty()) {
            QString text = S(ver);
            if (i.param && !i.param->app_ver.empty() && i.param->app_ver != ver) {
                text += tr(" (param.json says %1)").arg(S(i.param->app_ver));
            }
            addRow(fih, tr("Content version"), text);
        }
        if (i.layout_table_size != 0) {
            addRow(fih, tr("Layout table size"), Size(i.layout_table_size));
        }
    }

    QTreeWidgetItem* fs = i.has_fih ? addSection(tr("Outer filesystem (PFS)")) : nullptr;
    if (fs == nullptr) {
        // A bare metadata container has no filesystem image.
    } else if (i.outer_pfs.found) {
        const auto& sb = i.outer_pfs;
        addRow(fs, tr("Superblock offset"), Hex(sb.offset));
        addRow(fs, tr("Version"),
               tr("%1 (%2)")
                   .arg(sb.version)
                   .arg(sb.version == 2 ? QStringLiteral("PS5") : QStringLiteral("PS4")));
        addRow(fs, tr("Block size"), Hex(sb.block_size));
        addRow(fs, tr("Blocks"), QLocale().toString(static_cast<qlonglong>(sb.block_count)));
        addRow(fs, tr("Inodes"), QLocale().toString(static_cast<qlonglong>(sb.dinode_count)));
        QStringList mode;
        mode << (sb.IsEncrypted() ? tr("encrypted") : tr("plaintext"));
        if (sb.IsSigned()) {
            mode << tr("signed");
        }
        if (sb.Is64Bit()) {
            mode << tr("64-bit");
        }
        addRow(fs, tr("Mode"), QStringLiteral("%1 (%2)").arg(Hex(sb.mode, 4), mode.join(", ")));
    } else {
        addRow(fs, tr("Superblock"),
               i.kind == PS5Pkg::PackageKind::Retail
                   ? tr("Not readable (encrypted with console keys)")
                   : tr("Not found"));
    }

    if (i.has_cnt) {
        auto* cnt = addSection(tr("Metadata container"));
        addRow(cnt, tr("Flags"), Hex(i.cnt_flags, 8));
        addRow(cnt, tr("Entries"), QString::number(i.entry_count));
        addRow(cnt, tr("Body offset"), Hex(i.body_offset));
        addRow(cnt, tr("Body size"), Size(i.body_size));
        addRow(cnt, tr("Content flags"), Hex(i.content_flags, 8));
        addRow(cnt, tr("Encrypted entries"),
               tr("%1 of %2").arg(i.encrypted_entry_count).arg(i.entries.size()));
    }
}

void PkgInfoDialog::populateEntries() {
    m_entries->setSortingEnabled(false);
    m_entries->clearContents();
    m_entries->setRowCount(static_cast<int>(m_info.entries.size()));
    for (int row = 0; row < static_cast<int>(m_info.entries.size()); row++) {
        const auto& e = m_info.entries[static_cast<size_t>(row)];
        auto* id = new NumberCell(Hex(e.id, 4), e.id);
        id->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        id->setData(kEntryIndexRole, row);
        m_entries->setItem(row, 0, id);
        m_entries->setItem(row, 1, Cell(e.name.empty() ? tr("(unnamed)") : S(e.name)));
        m_entries->setItem(row, 2, new NumberCell(Hex(e.offset), e.offset));
        m_entries->setItem(row, 3, new NumberCell(QLocale().formattedDataSize(e.size), e.size));
        m_entries->item(row, 3)->setToolTip(QLocale().toString(e.size));
        m_entries->setItem(row, 4, Cell(Hex(e.flags1, 8) + QLatin1Char(' ') + Hex(e.flags2, 8)));
        const bool decryptable = contentIdKeys();
        m_entries->setItem(row, 5,
                           Cell(!e.IsEncrypted() ? tr("No")
                                : decryptable
                                    ? tr("Yes (slot %1, decryptable)").arg(PS5Pkg::EntryKeyIndex(e))
                                    : tr("Yes")));
    }
    m_entries->setSortingEnabled(true);
    m_entries->sortByColumn(0, Qt::AscendingOrder);
}

PkgInfoDialog::~PkgInfoDialog() = default;

const PS5Pkg::CntEntry* PkgInfoDialog::selectedEntry() const {
    const auto rows = m_entries->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return nullptr;
    }
    const int idx = m_entries->item(rows.front().row(), 0)->data(kEntryIndexRole).toInt();
    return &m_info.entries[static_cast<size_t>(idx)];
}

std::optional<std::vector<u8>> PkgInfoDialog::entryData(const PS5Pkg::CntEntry& entry,
                                                        const QString& title) {
    std::string problem;
    // Encrypted entries use the passcode from the Keys tab.
    auto data = PS5Pkg::DecryptEntry(m_info, entry, m_passcode->text().toStdString(), &problem);
    if (!data) {
        QMessageBox::warning(this, title, S(problem));
    }
    return data;
}

void PkgInfoDialog::onEntrySelectionChanged() {
    const auto* e = selectedEntry();
    const bool readable = e != nullptr && (!e->IsEncrypted() || contentIdKeys());
    m_save_entry->setEnabled(readable);
    m_open_npbind->setEnabled(readable && S(e->name).endsWith(QStringLiteral("npbind.dat")));
}

void PkgInfoDialog::onOpenNpbind() {
    const auto* e = selectedEntry();
    if (e == nullptr) {
        return;
    }
    const auto data = entryData(*e, tr("Open in NPBind Viewer"));
    if (!data) {
        return;
    }
    if (!m_temp_dir) {
        m_temp_dir = std::make_unique<QTemporaryDir>();
    }
    const QString file =
        m_temp_dir->filePath(QStringLiteral("%1_npbind.dat").arg(e->id, 4, 16, QLatin1Char('0')));
    QFile out(file);
    if (!m_temp_dir->isValid() || !out.open(QIODevice::WriteOnly) ||
        out.write(reinterpret_cast<const char*>(data->data()), static_cast<qint64>(data->size())) !=
            static_cast<qint64>(data->size())) {
        QMessageBox::warning(this, tr("Open in NPBind Viewer"),
                             tr("Could not write a temporary copy of the file."));
        return;
    }
    out.close();
    auto* viewer = new NpBindDialog(this, file);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    viewer->setWindowTitle(tr("%1 - %2").arg(viewer->windowTitle(), S(e->name)));
    viewer->show();
}

void PkgInfoDialog::onSaveEntry() {
    const auto* sel = selectedEntry();
    if (sel == nullptr) {
        return;
    }
    const auto& e = *sel;
    const auto data = entryData(e, tr("Save Entry"));
    if (!data) {
        return;
    }
    QString suggested = e.name.empty()
                            ? QStringLiteral("entry_%1.bin").arg(e.id, 4, 16, QLatin1Char('0'))
                            : S(e.name);
    suggested.replace(QLatin1Char('/'), QLatin1Char('_'));
    const QString out = QFileDialog::getSaveFileName(this, tr("Save Entry"),
                                                     QFileInfo(m_path).dir().filePath(suggested));
    if (out.isEmpty()) {
        return;
    }
    QFile f(out);
    if (!f.open(QIODevice::WriteOnly) ||
        f.write(reinterpret_cast<const char*>(data->data()), static_cast<qint64>(data->size())) !=
            static_cast<qint64>(data->size())) {
        QMessageBox::warning(this, tr("Save Entry"),
                             tr("Could not write %1.").arg(QDir::toNativeSeparators(out)));
    }
}

QString PkgInfoDialog::summaryText() const {
    QStringList lines;
    lines << m_title->text() << m_subtitle->text()
          << QStringLiteral("%1: %2").arg(kindLabel(), statusText());
    for (int s = 0; s < m_overview->topLevelItemCount(); s++) {
        const auto* sec = m_overview->topLevelItem(s);
        lines << QString() << QStringLiteral("[%1]").arg(sec->text(0));
        for (int c = 0; c < sec->childCount(); c++) {
            const auto* row = sec->child(c);
            lines << QStringLiteral("%1: %2").arg(row->text(0), row->text(1));
            for (int g = 0; g < row->childCount(); g++) {
                lines << QStringLiteral("  %1: %2")
                             .arg(row->child(g)->text(0), row->child(g)->text(1));
            }
        }
    }
    if (!m_info.segments.empty()) {
        lines << QString() << QStringLiteral("[%1]").arg(tr("Segments"));
        for (const auto& seg : m_info.segments) {
            lines << QStringLiteral("%1: %2 - %3 (%4)")
                         .arg(S(seg.name), Hex(seg.offset), Hex(seg.offset + seg.size),
                              QLocale().formattedDataSize(static_cast<qint64>(seg.size)));
        }
    }
    if (!m_path.isEmpty()) {
        lines << QString() << QStringLiteral("[%1]").arg(tr("Artwork"));
        for (const auto& art : m_artwork) {
            lines << QStringLiteral("%1: %2, %3x%4")
                         .arg(art.name, art.source)
                         .arg(art.image.width())
                         .arg(art.image.height());
        }
        if (!m_artwork_empty->text().isEmpty()) {
            lines << m_artwork_empty->text();
        }
    }
    if (!m_info.si_members.empty()) {
        lines << QString() << QStringLiteral("[%1]").arg(tr("Install metadata"));
        for (const auto& m : m_info.si_members) {
            lines << QStringLiteral("%1 (%2)").arg(
                S(m.name), QLocale().formattedDataSize(static_cast<qint64>(m.size)));
        }
    }
    if (!m_trophy_text.isEmpty()) {
        lines << QString() << QStringLiteral("[%1]").arg(tr("Trophies")) << m_trophy_text;
    }
    if (!m_uds_text.isEmpty()) {
        lines << QString() << QStringLiteral("[%1]").arg(tr("Activities")) << m_uds_text;
    }
    if (!m_playgo_text.isEmpty()) {
        lines << QString() << QStringLiteral("[%1]").arg(tr("PlayGo")) << m_playgo_text;
    }
    if (!m_keys.passcode.empty()) { // Verify Keys has been run
        lines << QString() << QStringLiteral("[%1]").arg(tr("Keys"));
        lines << (m_keys.verified ? tr("Result: verified")
                                  : tr("Result: not verified - %1").arg(S(m_keys.error)));
        if (!m_keys.scheme.empty()) {
            lines << tr("Key derivation: %1").arg(S(m_keys.scheme))
                  << tr("Key fingerprint: %1").arg(S(m_keys.ekpfs_fingerprint))
                  << tr("Blocks checked: %1 of %2 matched")
                         .arg(m_keys.blocks_matched)
                         .arg(m_keys.blocks_checked);
        }
        if (!m_keys.game_data_hint.empty()) {
            lines << tr("Game data starts with: %1").arg(S(m_keys.game_data_hint));
        }
        for (int row = 0; row < m_outer_files->rowCount(); row++) {
            QStringList cells;
            for (int col = 0; col < m_outer_files->columnCount(); col++) {
                if (const auto* item = m_outer_files->item(row, col);
                    item && !item->text().isEmpty()) {
                    cells << item->text();
                }
            }
            lines << cells.join(QStringLiteral(" | "));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void PkgInfoDialog::onCopySummary() {
    QApplication::clipboard()->setText(summaryText());
}

void PkgInfoDialog::populateArtwork() {
    m_artwork.clear();
    while (QLayoutItem* item = m_artwork_grid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    QStringList undecodable;
    for (const auto& slot : PS5Pkg::ARTWORK_SLOTS) {
        const auto art = PS5Pkg::ReadArtwork(m_info, slot);
        if (!art) {
            continue;
        }
        QString format;
        QImage image = DecodeArtwork(*art, &format);
        if (image.isNull()) {
            undecodable << S(art->entry_name);
            continue;
        }
        const int index = static_cast<int>(m_artwork.size());
        m_artwork.push_back({S(slot.name), S(art->entry_name), image});

        auto* card = new QWidget(m_artwork_page);
        auto* v = new QVBoxLayout(card);
        v->setContentsMargins(0, 0, 0, 0);
        auto* pic = new QLabel(card);
        pic->setAlignment(Qt::AlignCenter);
        pic->setFrameShape(QFrame::StyledPanel);
        const QPixmap preview = QPixmap::fromImage(image.scaled(
            kArtworkPreview, kArtworkPreview, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        pic->setPixmap(preview);
        // Keep the preview at full size; the scroll area handles overflow.
        const int frame = pic->frameWidth() * 2;
        pic->setFixedSize(preview.width() + frame, preview.height() + frame);
        v->addWidget(pic);
        auto* row = new QHBoxLayout();
        auto* caption = new QLabel(QStringLiteral("<b>%1</b>  %2 · %3×%4 · %5")
                                       .arg(S(slot.name), S(art->entry_name))
                                       .arg(image.width())
                                       .arg(image.height())
                                       .arg(format),
                                   card);
        caption->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto* save = new QPushButton(tr("Save Image..."), card);
        connect(save, &QPushButton::clicked, this, [this, index] { saveImage(index); });
        row->addWidget(caption, 1);
        row->addWidget(save);
        v->addLayout(row);
        m_artwork_grid->addWidget(card, index / 2, index % 2, Qt::AlignTop | Qt::AlignLeft);
    }

    QString note;
    if (m_path.isEmpty()) {
        note.clear();
    } else if (m_artwork.empty()) {
        note = tr("This package has no readable artwork in its metadata.");
    }
    if (!undecodable.isEmpty()) {
        note += (note.isEmpty() ? QString() : QStringLiteral(" ")) +
                tr("Could not decode: %1.").arg(undecodable.join(QStringLiteral(", ")));
    }
    m_artwork_empty->setText(note);
    m_artwork_empty->setVisible(!note.isEmpty());
}

void PkgInfoDialog::saveImage(int index) {
    if (index < 0 || index >= static_cast<int>(m_artwork.size())) {
        return;
    }
    const auto& art = m_artwork[static_cast<size_t>(index)];
    const QString base = m_info.param && !m_info.param->title_id.empty()
                             ? QStringLiteral("%1_%2.png").arg(S(m_info.param->title_id), art.name)
                             : art.name + QStringLiteral(".png");
    const QString out =
        QFileDialog::getSaveFileName(this, tr("Save Image"), QFileInfo(m_path).dir().filePath(base),
                                     tr("PNG image (*.png);;JPEG image (*.jpg);;All files (*)"));
    if (out.isEmpty()) {
        return;
    }
    if (!art.image.save(out)) {
        QMessageBox::warning(this, tr("Save Image"),
                             tr("Could not write %1.").arg(QDir::toNativeSeparators(out)));
    }
}

void PkgInfoDialog::populateSegments() {
    m_segments->clearContents();
    m_segments->setRowCount(static_cast<int>(m_info.segments.size()));
    for (int row = 0; row < static_cast<int>(m_info.segments.size()); row++) {
        const auto& seg = m_info.segments[static_cast<size_t>(row)];
        m_segments->setItem(row, 0, Cell(S(seg.name)));
        m_segments->setItem(row, 1, Cell(Hex(seg.offset), true));
        m_segments->setItem(row, 2,
                            Cell(QLocale().formattedDataSize(static_cast<qint64>(seg.size)), true));
        m_segments->item(row, 2)->setToolTip(QLocale().toString(static_cast<qulonglong>(seg.size)));
        m_segments->setItem(row, 3, Cell(Hex(seg.offset + seg.size), true));
        m_segments->setItem(row, 4, Cell(S(seg.description)));
    }
    if (m_path.isEmpty()) {
        m_segments_note->clear();
        return;
    }
    QStringList notes;
    if (m_info.has_cnt) {
        notes << tr("%1 of %2 metadata entries are encrypted.")
                     .arg(m_info.encrypted_entry_count)
                     .arg(m_info.entries.size());
    }
    if (!m_info.si_problem.empty()) {
        notes << S(m_info.si_problem);
    }
    m_segments_note->setText(notes.join(QLatin1Char(' ')));
}

void PkgInfoDialog::populateSi() {
    m_si->clearContents();
    m_si_preview->clear();
    m_si->setRowCount(static_cast<int>(m_info.si_members.size()));
    for (int row = 0; row < static_cast<int>(m_info.si_members.size()); row++) {
        const auto& m = m_info.si_members[static_cast<size_t>(row)];
        auto* name = Cell(S(m.name));
        name->setData(kEntryIndexRole, row);
        m_si->setItem(row, 0, name);
        m_si->setItem(
            row, 1,
            new NumberCell(QLocale().formattedDataSize(static_cast<qint64>(m.size)), m.size));
        m_si->setItem(
            row, 2,
            new NumberCell(QLocale().formattedDataSize(static_cast<qint64>(m.compressed_size)),
                           m.compressed_size));
        m_si->setItem(row, 3,
                      Cell(m.method == 0   ? tr("stored")
                           : m.method == 8 ? tr("deflate")
                                           : QString::number(m.method)));
    }
    if (!m_path.isEmpty() && m_info.si_members.empty()) {
        m_si_preview->setPlainText(m_info.si_problem.empty()
                                       ? tr("This package has no install-metadata archive.")
                                       : S(m_info.si_problem));
    }
    onSiSelectionChanged();
}

void PkgInfoDialog::onSiSelectionChanged() {
    const auto rows = m_si->selectionModel()->selectedRows();
    m_save_si->setEnabled(!rows.isEmpty());
    if (rows.isEmpty()) {
        return;
    }
    const int idx = m_si->item(rows.front().row(), 0)->data(kEntryIndexRole).toInt();
    const auto& member = m_info.si_members[static_cast<size_t>(idx)];
    std::string problem;
    const auto data = PS5Pkg::ReadSiMember(m_info, member, &problem);
    if (!data) {
        m_si_preview->setPlainText(S(problem));
        return;
    }
    const QString text = TextPreview(*data);
    m_si_preview->setPlainText(text.isEmpty() ? HexPreview(*data) : text);
}

void PkgInfoDialog::onSaveSiMember() {
    const auto rows = m_si->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return;
    }
    const int idx = m_si->item(rows.front().row(), 0)->data(kEntryIndexRole).toInt();
    const auto& member = m_info.si_members[static_cast<size_t>(idx)];
    std::string problem;
    const auto data = PS5Pkg::ReadSiMember(m_info, member, &problem);
    if (!data) {
        QMessageBox::warning(this, tr("Save File"), S(problem));
        return;
    }
    const QString file_name = QFileInfo(S(member.name)).fileName();
    const QString out = QFileDialog::getSaveFileName(this, tr("Save File"),
                                                     QFileInfo(m_path).dir().filePath(file_name));
    if (out.isEmpty()) {
        return;
    }
    QFile f(out);
    if (!f.open(QIODevice::WriteOnly) ||
        f.write(reinterpret_cast<const char*>(data->data()), static_cast<qint64>(data->size())) !=
            static_cast<qint64>(data->size())) {
        QMessageBox::warning(this, tr("Save File"),
                             tr("Could not write %1.").arg(QDir::toNativeSeparators(out)));
    }
}

QWidget* PkgInfoDialog::buildKeysTab() {
    m_keys_page = new QWidget(this);
    auto* layout = new QVBoxLayout(m_keys_page);

    auto* intro = new QLabel(
        tr("Debug packages are encrypted with a key derived from the content ID and a passcode "
           "(normally 32 zeros). Verifying decrypts the package's outer filesystem and checks "
           "every block against the hash stored for it, so a match proves the key is right."),
        m_keys_page);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("Passcode:"), m_keys_page));
    m_passcode = new QLineEdit(
        QString::fromLatin1(PS5Pkg::DEFAULT_PASSCODE.data(), PS5Pkg::DEFAULT_PASSCODE.size()),
        m_keys_page);
    m_passcode->setMaxLength(32);
    m_passcode->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_passcode->setToolTip(tr("32 characters. Leave as zeros unless the package was built with "
                              "a custom passcode."));
    m_verify = new QPushButton(tr("Verify Keys"), m_keys_page);
    row->addWidget(m_passcode, 1);
    row->addWidget(m_verify);
    layout->addLayout(row);

    m_key_result = new QLabel(m_keys_page);
    m_key_result->setWordWrap(true);
    m_key_result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_key_result);

    // Results live in one container so the tab stays compact when there are none.
    m_key_results = new QWidget(m_keys_page);
    auto* results = new QVBoxLayout(m_key_results);
    results->setContentsMargins(0, 0, 0, 0);
    m_key_details = new QTreeWidget(m_key_results);
    m_key_details->setColumnCount(2);
    m_key_details->setHeaderHidden(true);
    m_key_details->setRootIsDecorated(false);
    m_key_details->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_key_details->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    results->addWidget(m_key_details);

    m_outer_files = MakeTable(
        m_key_results, {tr("Path"), tr("Size"), tr("Blocks"), tr("First block"), tr("Sectors")}, 0);
    results->addWidget(m_outer_files, 1);

    auto* buttons = new QHBoxLayout();
    m_save_outer = new QPushButton(tr("Save File..."), m_key_results);
    m_save_outer->setToolTip(tr("Save the selected decrypted file. Only small files (up to 12 "
                                "blocks) can be saved here."));
    buttons->addWidget(m_save_outer);
    buttons->addStretch();
    results->addLayout(buttons);
    layout->addWidget(m_key_results, 1);
    layout->addStretch(0);

    connect(m_verify, &QPushButton::clicked, this, &PkgInfoDialog::onVerifyKeys);
    connect(m_passcode, &QLineEdit::returnPressed, this, &PkgInfoDialog::onVerifyKeys);
    connect(m_save_outer, &QPushButton::clicked, this, &PkgInfoDialog::onSaveOuterFile);
    connect(m_outer_files, &QTableWidget::itemSelectionChanged, this,
            &PkgInfoDialog::onOuterSelectionChanged);
    return m_keys_page;
}

void PkgInfoDialog::populateKeys() {
    m_keys = {};
    m_key_details->clear();
    m_outer_files->clearContents();
    m_outer_files->setRowCount(0);
    m_key_results->setVisible(false);

    const bool eligible = m_info.has_fih && (m_info.kind == PS5Pkg::PackageKind::Debug ||
                                             m_info.kind == PS5Pkg::PackageKind::Retail);
    m_verify->setEnabled(eligible);
    m_passcode->setEnabled(eligible);
    if (m_path.isEmpty()) {
        m_key_result->clear();
    } else if (eligible) {
        m_key_result->setText(tr("Press Verify Keys to check this package's key."));
    } else {
        m_key_result->setText(tr("Only finalized debug packages can be verified."));
    }
}

void PkgInfoDialog::onVerifyKeys() {
    if (!m_verify->isEnabled()) {
        return;
    }
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    m_keys = PS5Pkg::VerifyKeys(m_info, m_passcode->text().toStdString());
    QGuiApplication::restoreOverrideCursor();

    const bool keys_found = !m_keys.scheme.empty();
    QString text;
    if (m_keys.verified && m_keys.plaintext) {
        text = QStringLiteral("<b style=\"color:#2e9d4a\">%1</b> %2")
                   .arg(tr("Verified, no keys needed."),
                        tr("The filesystem is stored unencrypted; all %1 checked blocks matched "
                           "their hashes.")
                            .arg(m_keys.blocks_checked)
                            .toHtmlEscaped());
    } else if (m_keys.verified) {
        text = QStringLiteral("<b style=\"color:#2e9d4a\">%1</b> %2")
                   .arg(tr("Keys verified."),
                        tr("All %1 checked blocks decrypted and matched their hashes.")
                            .arg(m_keys.blocks_checked)
                            .toHtmlEscaped());
    } else if (keys_found) {
        text =
            QStringLiteral("<b style=\"color:#d08a1a\">%1</b> %2")
                .arg(m_keys.plaintext ? tr("Unencrypted image, but some blocks did not check out.")
                                      : tr("Key found, but some blocks did not check out."),
                     S(m_keys.error).toHtmlEscaped());
    } else {
        text = QStringLiteral("<b style=\"color:#d9534f\">%1</b> %2")
                   .arg(tr("Not verified."), S(m_keys.error).toHtmlEscaped());
    }
    m_key_result->setText(text);
    // The Overview, Entries and decrypted tabs depend on the result.
    refreshDecryptedViews();

    m_key_details->clear();
    if (keys_found) {
        auto add = [&](const QString& k, const QString& v) {
            auto* item = new QTreeWidgetItem(m_key_details, {k, v});
            item->setToolTip(1, v);
        };
        add(tr("Key derivation"), S(m_keys.scheme));
        if (!m_keys.plaintext) {
            add(tr("Key fingerprint"), S(m_keys.ekpfs_fingerprint));
        }
        add(tr("Blocks checked"),
            tr("%1 of %2 matched").arg(m_keys.blocks_matched).arg(m_keys.blocks_checked));
        if (!m_keys.game_data_hint.empty()) {
            add(tr("Game data starts with"), S(m_keys.game_data_hint));
        }
    }
    // Size the details box to its rows.
    if (m_key_details->topLevelItemCount() > 0) {
        const int row_h = m_key_details->sizeHintForRow(0);
        m_key_details->setFixedHeight(row_h * m_key_details->topLevelItemCount() +
                                      2 * m_key_details->frameWidth() + 4);
    }

    m_outer_files->clearContents();
    m_outer_files->setRowCount(static_cast<int>(m_keys.files.size()));
    for (int row = 0; row < static_cast<int>(m_keys.files.size()); row++) {
        const auto& f = m_keys.files[static_cast<size_t>(row)];
        auto* path = Cell(f.is_dir ? S(f.path) + QLatin1Char('/') : S(f.path));
        path->setData(kEntryIndexRole, row);
        m_outer_files->setItem(row, 0, path);
        QString size =
            f.is_dir ? QString() : QLocale().formattedDataSize(static_cast<qint64>(f.size));
        m_outer_files->setItem(row, 1, new NumberCell(size, f.size));
        if (!f.is_dir && f.logical_size > f.size) {
            m_outer_files->item(row, 1)->setToolTip(
                tr("Unpacked: %1")
                    .arg(QLocale().formattedDataSize(static_cast<qint64>(f.logical_size))));
        }
        m_outer_files->setItem(row, 2,
                               new NumberCell(QLocale().toString(f.block_count), f.block_count));
        QString check;
        switch (f.first_block) {
        case PS5Pkg::BlockCheck::Ok:
            check = tr("hash OK");
            break;
        case PS5Pkg::BlockCheck::Mismatch:
            check = tr("hash mismatch");
            break;
        case PS5Pkg::BlockCheck::Unreadable:
            check = tr("unreadable");
            break;
        default:
            check = f.is_dir ? tr("directory") : QString();
            break;
        }
        m_outer_files->setItem(row, 3, Cell(check));
        m_outer_files->setItem(row, 4,
                               Cell(f.first_block != PS5Pkg::BlockCheck::Ok ? QString()
                                    : f.data_sectors                        ? tr("data")
                                                                            : tr("metadata")));
    }
    m_outer_files->setVisible(!m_keys.files.empty());
    m_save_outer->setVisible(!m_keys.files.empty());
    m_key_details->setVisible(keys_found);
    m_key_results->setVisible(keys_found);
    onOuterSelectionChanged();
}

void PkgInfoDialog::onOuterSelectionChanged() {
    const auto rows = m_outer_files->selectionModel()->selectedRows();
    bool can_save = false;
    if (!rows.isEmpty()) {
        const int idx = m_outer_files->item(rows.front().row(), 0)->data(kEntryIndexRole).toInt();
        const auto& f = m_keys.files[static_cast<size_t>(idx)];
        can_save = !f.is_dir && f.all_direct && f.first_block == PS5Pkg::BlockCheck::Ok;
    }
    m_save_outer->setEnabled(can_save);
}

void PkgInfoDialog::onSaveOuterFile() {
    const auto rows = m_outer_files->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return;
    }
    const int idx = m_outer_files->item(rows.front().row(), 0)->data(kEntryIndexRole).toInt();
    const auto& file = m_keys.files[static_cast<size_t>(idx)];
    std::string problem;
    const auto data = PS5Pkg::ReadOuterFile(m_info, m_keys, file, &problem);
    if (!data) {
        QMessageBox::warning(this, tr("Save File"), S(problem));
        return;
    }
    const QString out = QFileDialog::getSaveFileName(
        this, tr("Save File"),
        QFileInfo(m_path).dir().filePath(QFileInfo(S(file.path)).fileName()));
    if (out.isEmpty()) {
        return;
    }
    QFile f(out);
    if (!f.open(QIODevice::WriteOnly) ||
        f.write(reinterpret_cast<const char*>(data->data()), static_cast<qint64>(data->size())) !=
            static_cast<qint64>(data->size())) {
        QMessageBox::warning(this, tr("Save File"),
                             tr("Could not write %1.").arg(QDir::toNativeSeparators(out)));
    }
}

// ---------------------------------------------------------------- param.json

QWidget* PkgInfoDialog::buildParamTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    m_param = new QPlainTextEdit(page);
    m_param->setReadOnly(true);
    m_param->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_param->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_param, 1);
    auto* row = new QHBoxLayout();
    auto* open = new QPushButton(tr("Open in Param Viewer"), page);
    open->setToolTip(tr("Show param.json in the structured viewer, with search and per-key "
                        "descriptions."));
    row->addWidget(open);
    row->addStretch();
    layout->addLayout(row);
    connect(open, &QPushButton::clicked, this, &PkgInfoDialog::onOpenParamViewer);
    return page;
}

QString PkgInfoDialog::writeTempFile(const QString& name, const std::vector<u8>& data) {
    if (!m_temp_dir) {
        m_temp_dir = std::make_unique<QTemporaryDir>();
    }
    if (!m_temp_dir->isValid()) {
        return {};
    }
    const QString path = m_temp_dir->filePath(name);
    QDir().mkpath(QFileInfo(path).path());
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly) ||
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<qint64>(data.size())) !=
            static_cast<qint64>(data.size())) {
        return {};
    }
    return path;
}

void PkgInfoDialog::onOpenParamViewer() {
    const auto* entry = m_info.FindEntry(PS5Pkg::ENTRY_PARAM_JSON);
    if (entry == nullptr) {
        QMessageBox::information(this, tr("Open in Param Viewer"),
                                 tr("This package has no param.json."));
        return;
    }
    const auto data = entryData(*entry, tr("Open in Param Viewer"));
    if (!data) {
        return;
    }
    // The viewer expects a file named param.json.
    const QString path = writeTempFile(QStringLiteral("param/param.json"), *data);
    if (path.isEmpty()) {
        QMessageBox::warning(this, tr("Open in Param Viewer"),
                             tr("Could not write a temporary copy of param.json."));
        return;
    }
    // The viewer shows icon0 from the same folder.
    if (const auto* icon = m_info.FindEntry(PS5Pkg::ENTRY_ICON0_PNG)) {
        if (const auto png = PS5Pkg::ReadEntryData(m_info, *icon)) {
            writeTempFile(QStringLiteral("param/icon0.png"), *png);
        }
    }
    auto* viewer = new ParamViewerDialog(this, path);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    viewer->setWindowTitle(tr("%1 - %2").arg(viewer->windowTitle(), m_title->text()));
    viewer->show();
}

void PkgInfoDialog::extractContainer(const QString& ucp_path) {
    if (ucp_path.isEmpty()) {
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Extract Container To"),
                                                          QFileInfo(m_path).dir().path());
    if (dir.isEmpty()) {
        return;
    }
    const QString target = QDir(dir).filePath(QFileInfo(ucp_path).completeBaseName());
    UCP ucp;
    if (!ucp.Open(ucp_path.toStdU16String()) || !ucp.ExtractAll(target.toStdU16String())) {
        QMessageBox::warning(this, tr("Extract Container"),
                             tr("Could not extract to %1.").arg(QDir::toNativeSeparators(target)));
        return;
    }
    QMessageBox::information(
        this, tr("Extract Container"),
        tr("Extracted %n file(s) to %1.", nullptr, static_cast<int>(ucp.GetEntries().size()))
            .arg(QDir::toNativeSeparators(target)));
}

namespace {

constexpr int kTrophyIconSize = 56;

QIcon GradeBadge(TrophyGrade grade) {
    static std::map<TrophyGrade, QIcon> cache;
    if (const auto it = cache.find(grade); it != cache.end()) {
        return it->second;
    }
    QIcon icon;
    const std::string name(TrophySet::GradeIconFileName(grade));
    if (!name.empty()) {
        try {
            const auto fs = cmrc::res::get_filesystem();
            const auto file = fs.open("src/images/" + name);
            QPixmap pm;
            if (pm.loadFromData(reinterpret_cast<const uchar*>(file.begin()),
                                static_cast<uint>(file.size()))) {
                // Small badge; QIcon never scales a pixmap up past its own size.
                icon = QIcon(pm.scaled(28, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        } catch (...) {
            // Missing resource: show the text only.
        }
    }
    cache.emplace(grade, icon);
    return icon;
}

} // namespace

QWidget* PkgInfoDialog::buildTrophiesTab() {
    m_trophy_page = new QWidget(this);
    auto* layout = new QVBoxLayout(m_trophy_page);
    auto* top = new QHBoxLayout();
    m_trophy_summary = new QLabel(m_trophy_page);
    m_trophy_summary->setWordWrap(true);
    m_trophy_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    top->addWidget(m_trophy_summary, 1);
    m_trophy_container = new QComboBox(m_trophy_page);
    m_trophy_container->setToolTip(tr("Trophy container"));
    top->addWidget(m_trophy_container);
    m_trophy_lang = new QComboBox(m_trophy_page);
    m_trophy_lang->setToolTip(tr("Language of the trophy names and descriptions"));
    top->addWidget(m_trophy_lang);
    layout->addLayout(top);

    m_trophy_table = MakeTable(
        m_trophy_page,
        {QString(), tr("Name"), tr("Description"), tr("Grade"), tr("Hidden"), tr("ID")}, 2);
    m_trophy_table->setIconSize(QSize(kTrophyIconSize, kTrophyIconSize));
    m_trophy_table->verticalHeader()->setDefaultSectionSize(kTrophyIconSize + 8);
    m_trophy_table->setWordWrap(true);
    m_trophy_table->setSortingEnabled(true);
    layout->addWidget(m_trophy_table, 1);

    auto* row = new QHBoxLayout();
    auto* extract = new QPushButton(tr("Extract Container..."), m_trophy_page);
    extract->setToolTip(tr("Save the trophy container's files (icons and JSON) to a folder."));
    row->addWidget(extract);
    row->addStretch();
    layout->addLayout(row);

    connect(m_trophy_container, &QComboBox::currentIndexChanged, this,
            &PkgInfoDialog::onTrophyContainerChanged);
    connect(m_trophy_lang, &QComboBox::currentIndexChanged, this,
            &PkgInfoDialog::onTrophyLanguageChanged);
    connect(extract, &QPushButton::clicked, this, [this] {
        const int i = m_trophy_container->currentIndex();
        if (i >= 0 && i < m_trophy_ucps.size()) {
            extractContainer(m_trophy_ucps[i]);
        }
    });
    return m_trophy_page;
}

void PkgInfoDialog::populateTrophies() {
    m_trophy_ucps.clear();
    m_trophy_text.clear();
    {
        const QSignalBlocker block(m_trophy_container);
        m_trophy_container->clear();
        for (const auto& e : m_info.entries) {
            const QString name = S(e.name);
            if (!name.startsWith(QStringLiteral("trophy")) ||
                !name.endsWith(QStringLiteral(".ucp"), Qt::CaseInsensitive)) {
                continue;
            }
            const auto data = PS5Pkg::DecryptEntry(m_info, e, m_passcode->text().toStdString());
            if (!data) {
                continue;
            }
            QString file_name = name;
            file_name.replace(QLatin1Char('/'), QLatin1Char('_'));
            const QString path = writeTempFile(QStringLiteral("ucp/") + file_name, *data);
            if (!path.isEmpty()) {
                m_trophy_ucps << path;
                m_trophy_container->addItem(QFileInfo(name).fileName());
            }
        }
    }
    m_trophy_container->setVisible(m_trophy_ucps.size() > 1);
    m_tabs->setTabVisible(m_tabs->indexOf(m_trophy_page), !m_trophy_ucps.isEmpty());
    onTrophyContainerChanged();
}

void PkgInfoDialog::onTrophyContainerChanged() {
    // Start from the package's default language.
    int language = -1;
    if (m_info.param && !m_info.param->default_language.empty()) {
        language = TrophySet::LanguageForLocale(m_info.param->default_language);
    }
    showTrophySet(language);
}

void PkgInfoDialog::onTrophyLanguageChanged() {
    showTrophySet(m_trophy_lang->currentData().toInt());
}

void PkgInfoDialog::showTrophySet(int language_index) {
    m_trophy_set.Reset();
    m_trophy_table->setSortingEnabled(false);
    m_trophy_table->clearContents();
    m_trophy_table->setRowCount(0);
    const int ci = m_trophy_container->currentIndex();
    if (ci < 0 || ci >= m_trophy_ucps.size()) {
        m_trophy_summary->clear();
        return;
    }
    UCP ucp;
    TrophySet set;
    if (!ucp.Open(m_trophy_ucps[ci].toStdU16String()) ||
        !ucp.LoadTrophySet(set, language_index < 0 ? 1 : language_index)) {
        m_trophy_summary->setText(tr("The trophy container could not be read."));
        m_trophy_text = m_trophy_summary->text();
        return;
    }

    m_trophy_set = set;

    // Language list from the set itself.
    {
        const QSignalBlocker block(m_trophy_lang);
        m_trophy_lang->clear();
        for (const auto& locale : set.languages) {
            m_trophy_lang->addItem(S(locale), TrophySet::LanguageForLocale(locale));
        }
        m_trophy_lang->setCurrentIndex(m_trophy_lang->findText(S(set.loaded_locale)));
    }
    m_trophy_lang->setVisible(m_trophy_lang->count() > 1);

    std::map<TrophyGrade, int> counts;
    int hidden = 0;
    m_trophy_table->setRowCount(static_cast<int>(set.trophies.size()));
    for (int row = 0; row < static_cast<int>(set.trophies.size()); row++) {
        const auto& t = set.trophies[static_cast<size_t>(row)];
        counts[t.grade]++;
        hidden += t.hidden ? 1 : 0;

        auto* icon = Cell(QString());
        if (const auto png = ucp.ReadEntry(t.IconFileName())) {
            QPixmap pm;
            if (pm.loadFromData(png->data(), static_cast<uint>(png->size()))) {
                icon->setIcon(QIcon(pm.scaled(kTrophyIconSize, kTrophyIconSize, Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation)));
            }
        }
        m_trophy_table->setItem(row, 0, icon);
        auto* name = Cell(t.name.empty() ? S(t.id) : S(t.name));
        QFont bold = name->font();
        bold.setBold(true);
        name->setFont(bold);
        m_trophy_table->setItem(row, 1, name);
        auto* detail = Cell(S(t.detail));
        detail->setToolTip(S(t.detail));
        m_trophy_table->setItem(row, 2, detail);
        // Sorts by grade (platinum first), not alphabetically.
        auto* grade =
            new NumberCell(S(TrophySet::GradeName(t.grade)), static_cast<qulonglong>(t.grade));
        grade->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        grade->setIcon(GradeBadge(t.grade));
        m_trophy_table->setItem(row, 3, grade);
        m_trophy_table->setItem(row, 4, Cell(t.hidden ? tr("Hidden") : QString()));
        m_trophy_table->setItem(row, 5, Cell(S(t.id)));
    }
    m_trophy_table->setSortingEnabled(true);
    m_trophy_table->sortByColumn(5, Qt::AscendingOrder);
    m_trophy_table->resizeColumnToContents(1);

    QStringList parts;
    parts << tr("%n trophies", nullptr, static_cast<int>(set.trophies.size()));
    for (const auto grade :
         {TrophyGrade::Platinum, TrophyGrade::Gold, TrophyGrade::Silver, TrophyGrade::Bronze}) {
        if (counts[grade] > 0) {
            parts << QStringLiteral("%1 %2").arg(counts[grade]).arg(S(TrophySet::GradeName(grade)));
        }
    }
    if (hidden > 0) {
        parts << tr("%1 hidden").arg(hidden);
    }
    QStringList ids;
    if (!set.np_comm_id.empty()) {
        ids << tr("NP ID %1").arg(S(set.np_comm_id));
    }
    if (!set.set_version.empty()) {
        ids << tr("set version %1").arg(S(set.set_version));
    }
    m_trophy_summary->setText(QStringLiteral("<b>%1</b><br>%2")
                                  .arg(parts.join(QStringLiteral(" · ")).toHtmlEscaped(),
                                       ids.join(QStringLiteral(" · ")).toHtmlEscaped()));
    m_trophy_text = parts.join(QStringLiteral(", "));
    if (!ids.isEmpty()) {
        m_trophy_text += QStringLiteral(" (") + ids.join(QStringLiteral(", ")) + QLatin1Char(')');
    }
    m_tabs->setTabText(m_tabs->indexOf(m_trophy_page),
                       tr("Trophies (%1)").arg(set.trophies.size()));
}

// ---------------------------------------------------------------- Activities

QWidget* PkgInfoDialog::buildActivitiesTab() {
    m_uds_page = new QWidget(this);
    auto* layout = new QVBoxLayout(m_uds_page);
    m_uds_summary = new QLabel(m_uds_page);
    m_uds_summary->setWordWrap(true);
    m_uds_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_uds_summary);
    m_uds_tree = new QTreeWidget(m_uds_page);
    m_uds_tree->setColumnCount(2);
    m_uds_tree->setHeaderLabels({tr("Definition"), tr("Details")});
    m_uds_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_uds_tree->header()->resizeSection(0, 360);
    m_uds_tree->header()->setStretchLastSection(true);
    layout->addWidget(m_uds_tree, 1);
    auto* row = new QHBoxLayout();
    auto* extract = new QPushButton(tr("Extract Container..."), m_uds_page);
    extract->setToolTip(tr("Save the activity definition files (JSON) to a folder."));
    row->addWidget(extract);
    row->addStretch();
    layout->addLayout(row);
    connect(extract, &QPushButton::clicked, this, [this] { extractContainer(m_uds_ucp); });
    return m_uds_page;
}

void PkgInfoDialog::populateActivities() {
    m_uds_tree->clear();
    m_uds_summary->clear();
    m_uds_ucp.clear();
    m_uds_text.clear();
    m_uds = {};
    for (const auto& e : m_info.entries) {
        const QString name = S(e.name);
        if (name.startsWith(QStringLiteral("uds/")) &&
            name.endsWith(QStringLiteral(".ucp"), Qt::CaseInsensitive)) {
            if (const auto data =
                    PS5Pkg::DecryptEntry(m_info, e, m_passcode->text().toStdString())) {
                m_uds_ucp =
                    writeTempFile(QStringLiteral("ucp/uds_") + QFileInfo(name).fileName(), *data);
            }
            break;
        }
    }
    m_tabs->setTabVisible(m_tabs->indexOf(m_uds_page), !m_uds_ucp.isEmpty());
    if (m_uds_ucp.isEmpty()) {
        return;
    }
    UCP ucp;
    UdsDefinitions uds;
    if (!ucp.Open(m_uds_ucp.toStdU16String()) || !uds.LoadFromUcp(ucp)) {
        m_uds_summary->setText(tr("The activity container could not be read."));
        return;
    }
    m_uds = uds;

    auto section = [&](const QString& label, size_t count) {
        auto* item = new QTreeWidgetItem(m_uds_tree, {tr("%1 (%2)").arg(label).arg(count)});
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
        item->setFirstColumnSpanned(true);
        return item;
    };
    auto add = [](QTreeWidgetItem* parent, const QString& a, const QString& b) {
        auto* item = new QTreeWidgetItem(parent, {a, b});
        item->setToolTip(0, a);
        item->setToolTip(1, b);
        return item;
    };

    auto* events = section(tr("Events"), uds.events.size());
    for (const auto& ev : uds.events) {
        auto* item = add(events, S(ev.name),
                         QStringList{S(ev.type), S(ev.group)}.join(QStringLiteral(", ")));
        for (const auto& p : ev.properties) {
            add(item, S(p.path), S(p.data_type));
        }
    }
    auto* stats = section(tr("Stats"), uds.stats.size());
    for (const auto& st : uds.stats) {
        QStringList details{S(st.data_type), S(st.aggregation)};
        if (!st.enum_source_id.empty()) {
            details << tr("values from %1").arg(S(st.enum_source_id));
        }
        auto* item = add(stats, QStringLiteral("#%1 %2").arg(st.id).arg(S(st.name)),
                         details.join(QStringLiteral(", ")));
        item->setToolTip(0, tr("Group: %1, source: %2").arg(S(st.group), S(st.source_id)));
    }
    auto* rules = section(tr("Stat rules"), uds.rules.size());
    for (const auto& r : uds.rules) {
        QString when = tr("on %1").arg(S(r.event));
        if (!r.condition.empty()) {
            when += tr(" if %1").arg(S(r.condition));
        }
        add(rules, QStringLiteral("#%1 %2").arg(r.id).arg(when),
            tr("%1 → stat #%2 %3")
                .arg(S(r.input))
                .arg(r.output_stat_id)
                .arg(S(r.output_stat_name)));
    }
    auto* enums = section(tr("Enums"), uds.enums.size());
    for (const auto& en : uds.enums) {
        auto* item = add(enums, QStringLiteral("#%1 %2").arg(en.id).arg(S(en.source_id)),
                         tr("%n value(s)", nullptr, static_cast<int>(en.values.size())));
        for (const auto& v : en.values) {
            add(item, S(v), QString());
        }
    }
    events->setExpanded(true);
    stats->setExpanded(true);

    QStringList parts;
    parts << tr("%n event(s)", nullptr, static_cast<int>(uds.events.size()))
          << tr("%n stat(s)", nullptr, static_cast<int>(uds.stats.size()))
          << tr("%n rule(s)", nullptr, static_cast<int>(uds.rules.size()))
          << tr("%n enum(s)", nullptr, static_cast<int>(uds.enums.size()));
    QStringList ids;
    if (!uds.np_comm_id.empty()) {
        ids << tr("NP ID %1").arg(S(uds.np_comm_id));
    }
    if (uds.configuration_id != 0) {
        ids << tr("configuration %1").arg(uds.configuration_id);
    }
    QString text = QStringLiteral("<b>%1</b>").arg(parts.join(QStringLiteral(" · ")));
    if (!ids.isEmpty()) {
        text += QStringLiteral("<br>") + ids.join(QStringLiteral(" · ")).toHtmlEscaped();
    }
    if (!uds.problems.empty()) {
        QStringList p;
        for (const auto& s : uds.problems) {
            p << S(s);
        }
        text += QStringLiteral("<br><span style=\"color:#d9534f\">%1</span>")
                    .arg(p.join(QStringLiteral("; ")).toHtmlEscaped());
    }
    m_uds_summary->setText(text);
    m_uds_text = parts.join(QStringLiteral(", "));
    if (!ids.isEmpty()) {
        m_uds_text += QStringLiteral(" (") + ids.join(QStringLiteral(", ")) + QLatin1Char(')');
    }
}

// ---------------------------------------------------------------- Report

QByteArray PkgInfoDialog::reportJson() const {
    const auto& i = m_info;
    auto str = [](std::string_view v) { return QJsonValue(S(v)); };
    auto hex = [](qulonglong v) { return QJsonValue(Hex(v)); };
    auto num = [](qulonglong v) { return QJsonValue(static_cast<double>(v)); };

    QJsonObject root;
    root[QStringLiteral("generator")] = QStringLiteral("shadLauncher5 PS5 PKG Info");
    root[QStringLiteral("file")] = QJsonObject{
        {QStringLiteral("path"), QDir::toNativeSeparators(m_path)},
        {QStringLiteral("size"), num(i.file_size)},
    };

    QJsonObject pkg{
        {QStringLiteral("type"), kindLabel()},
        {QStringLiteral("status"), statusText()},
    };
    if (!i.error.empty()) {
        pkg[QStringLiteral("error")] = str(i.error);
    }
    if (i.has_cnt) {
        pkg[QStringLiteral("content_id")] = str(i.content_id);
        pkg[QStringLiteral("content_type")] = str(PS5Pkg::ContentTypeName(i.content_type));
        pkg[QStringLiteral("drm_type")] = str(PS5Pkg::DrmTypeName(i.drm_type));
        pkg[QStringLiteral("region")] = str(PS5Pkg::RegionFromContentId(i.content_id));
        pkg[QStringLiteral("patch_type")] = str(PS5Pkg::PatchKindName(i.patch_kind));
    }
    root[QStringLiteral("package")] = pkg;

    if (i.param) {
        const auto& p = *i.param;
        QJsonObject app{
            {QStringLiteral("title"), str(p.title)},
            {QStringLiteral("title_id"), str(p.title_id)},
            {QStringLiteral("content_id"), str(p.content_id)},
            {QStringLiteral("concept_id"), str(p.concept_id)},
            {QStringLiteral("category"), str(p.category)},
            {QStringLiteral("application_drm_type"), str(p.drm_type)},
            {QStringLiteral("content_version"), str(p.app_ver)},
            {QStringLiteral("master_version"), str(p.master_version)},
            {QStringLiteral("required_system_software"), str(p.system_ver_string)},
            {QStringLiteral("sdk_version"), str(p.sdk_ver_string)},
            {QStringLiteral("default_language"), str(p.default_language)},
        };
        // The document as shipped, so nothing is lost.
        const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(p.RawJson()));
        if (doc.isObject()) {
            app[QStringLiteral("param_json")] = doc.object();
        }
        root[QStringLiteral("application")] = app;
    }

    if (i.has_cnt && contentIdKeys()) {
        const auto np = PS5Pkg::ReadNpInfo(i, m_passcode->text().toStdString());
        QJsonObject net;
        QJsonArray ids;
        for (const auto& id : np.np_comm_ids) {
            ids.append(str(id));
        }
        net[QStringLiteral("np_communication_ids")] = ids;
        net[QStringLiteral("np_title_id")] = str(np.np_title_id);
        if (np.has_license) {
            net[QStringLiteral("license")] = QJsonObject{
                {QStringLiteral("rif_version"), np.rif_version},
                {QStringLiteral("account_id"), hex(np.account_id)},
                {QStringLiteral("start_time"), num(np.start_time)},
                {QStringLiteral("expiry_time"), hex(np.expiry_time)},
                {QStringLiteral("content_id"), str(np.license_content_id)},
            };
        }
        QJsonArray problems;
        for (const auto& p : np.problems) {
            problems.append(str(p));
        }
        if (!problems.isEmpty()) {
            net[QStringLiteral("problems")] = problems;
        }
        root[QStringLiteral("network")] = net;
    }

    if (i.has_fih) {
        root[QStringLiteral("finalized_image")] = QJsonObject{
            {QStringLiteral("signed_byte"), hex(i.signed_byte)},
            {QStringLiteral("format_version"), i.format_version},
            {QStringLiteral("pfs_offset"), hex(i.pfs_offset)},
            {QStringLiteral("pfs_size"), num(i.pfs_size)},
            {QStringLiteral("cnt_offset"), hex(i.cnt_offset)},
            {QStringLiteral("game_data_size"), num(i.inner_image_size)},
            {QStringLiteral("game_files"), num(i.app_file_count)},
            {QStringLiteral("files_and_folders"), num(i.content_inode_count)},
            {QStringLiteral("content_version"),
             str(PS5Pkg::FormatContentVersion(i.content_version_echo))},
            {QStringLiteral("layout_table_size"), num(i.layout_table_size)},
        };
    }
    if (i.has_lih) {
        root[QStringLiteral("patch_image")] = QJsonObject{
            {QStringLiteral("version"), i.lih_version},
            {QStringLiteral("cnt_offset"), hex(i.cnt_offset)},
        };
    }
    if (i.outer_pfs.found) {
        const auto& sb = i.outer_pfs;
        root[QStringLiteral("outer_filesystem")] = QJsonObject{
            {QStringLiteral("superblock_offset"), hex(sb.offset)},
            {QStringLiteral("version"), static_cast<double>(sb.version)},
            {QStringLiteral("block_size"), num(sb.block_size)},
            {QStringLiteral("mode"), hex(sb.mode)},
            {QStringLiteral("encrypted"), sb.IsEncrypted()},
            {QStringLiteral("signed"), sb.IsSigned()},
        };
    }
    if (i.has_cnt) {
        QJsonArray entries;
        for (const auto& e : i.entries) {
            QJsonObject o{
                {QStringLiteral("id"), hex(e.id)},
                {QStringLiteral("name"), str(e.name)},
                {QStringLiteral("offset"), hex(e.offset)},
                {QStringLiteral("size"), num(e.size)},
                {QStringLiteral("flags1"), hex(e.flags1)},
                {QStringLiteral("flags2"), hex(e.flags2)},
                {QStringLiteral("encrypted"), e.IsEncrypted()},
            };
            if (e.IsEncrypted()) {
                o[QStringLiteral("key_slot")] = static_cast<int>(PS5Pkg::EntryKeyIndex(e));
            }
            entries.append(o);
        }
        root[QStringLiteral("metadata_container")] = QJsonObject{
            {QStringLiteral("flags"), hex(i.cnt_flags)},
            {QStringLiteral("content_flags"), hex(i.content_flags)},
            {QStringLiteral("body_offset"), hex(i.body_offset)},
            {QStringLiteral("body_size"), num(i.body_size)},
            {QStringLiteral("entries"), entries},
        };
    }

    QJsonArray segments;
    for (const auto& seg : i.segments) {
        segments.append(QJsonObject{{QStringLiteral("name"), str(seg.name)},
                                    {QStringLiteral("offset"), hex(seg.offset)},
                                    {QStringLiteral("size"), num(seg.size)}});
    }
    root[QStringLiteral("segments")] = segments;

    QJsonArray si;
    for (const auto& m : i.si_members) {
        si.append(
            QJsonObject{{QStringLiteral("name"), str(m.name)},
                        {QStringLiteral("size"), num(m.size)},
                        {QStringLiteral("stored_size"), num(m.compressed_size)},
                        {QStringLiteral("method"), m.method == 0   ? QStringLiteral("stored")
                                                   : m.method == 8 ? QStringLiteral("deflate")
                                                                   : QString::number(m.method)}});
    }
    root[QStringLiteral("install_metadata")] = si;

    QJsonArray art;
    for (const auto& a : m_artwork) {
        art.append(QJsonObject{{QStringLiteral("name"), a.name},
                               {QStringLiteral("source"), a.source},
                               {QStringLiteral("width"), a.image.width()},
                               {QStringLiteral("height"), a.image.height()}});
    }
    root[QStringLiteral("artwork")] = art;

    if (m_trophy_set.IsValid()) {
        QJsonArray list;
        for (const auto& t : m_trophy_set.trophies) {
            QJsonObject o{{QStringLiteral("id"), str(t.id)},
                          {QStringLiteral("grade"), str(TrophySet::GradeName(t.grade))},
                          {QStringLiteral("hidden"), t.hidden},
                          {QStringLiteral("name"), str(t.name)},
                          {QStringLiteral("detail"), str(t.detail)}};
            if (!t.platinum_id.empty()) {
                o[QStringLiteral("platinum_id")] = str(t.platinum_id);
            }
            list.append(o);
        }
        QJsonArray langs;
        for (const auto& l : m_trophy_set.languages) {
            langs.append(str(l));
        }
        root[QStringLiteral("trophies")] = QJsonObject{
            {QStringLiteral("np_communication_id"), str(m_trophy_set.np_comm_id)},
            {QStringLiteral("set_version"), str(m_trophy_set.set_version)},
            {QStringLiteral("language"), str(m_trophy_set.loaded_locale)},
            {QStringLiteral("languages"), langs},
            {QStringLiteral("list"), list},
        };
    }

    if (!m_uds.IsEmpty()) {
        QJsonArray events, stats, rules, enums;
        for (const auto& ev : m_uds.events) {
            QJsonArray props;
            for (const auto& p : ev.properties) {
                props.append(QJsonObject{{QStringLiteral("path"), str(p.path)},
                                         {QStringLiteral("type"), str(p.data_type)}});
            }
            events.append(QJsonObject{{QStringLiteral("name"), str(ev.name)},
                                      {QStringLiteral("type"), str(ev.type)},
                                      {QStringLiteral("group"), str(ev.group)},
                                      {QStringLiteral("properties"), props}});
        }
        for (const auto& st : m_uds.stats) {
            stats.append(QJsonObject{{QStringLiteral("id"), static_cast<double>(st.id)},
                                     {QStringLiteral("name"), str(st.name)},
                                     {QStringLiteral("type"), str(st.data_type)},
                                     {QStringLiteral("aggregation"), str(st.aggregation)},
                                     {QStringLiteral("enum"), str(st.enum_source_id)}});
        }
        for (const auto& r : m_uds.rules) {
            rules.append(
                QJsonObject{{QStringLiteral("id"), static_cast<double>(r.id)},
                            {QStringLiteral("event"), str(r.event)},
                            {QStringLiteral("condition"), str(r.condition)},
                            {QStringLiteral("input"), str(r.input)},
                            {QStringLiteral("output_stat"), static_cast<double>(r.output_stat_id)},
                            {QStringLiteral("output_stat_name"), str(r.output_stat_name)}});
        }
        for (const auto& en : m_uds.enums) {
            QJsonArray values;
            for (const auto& v : en.values) {
                values.append(str(v));
            }
            enums.append(QJsonObject{{QStringLiteral("id"), static_cast<double>(en.id)},
                                     {QStringLiteral("source_id"), str(en.source_id)},
                                     {QStringLiteral("values"), values}});
        }
        root[QStringLiteral("activities")] = QJsonObject{
            {QStringLiteral("np_communication_id"), str(m_uds.np_comm_id)},
            {QStringLiteral("configuration_id"), static_cast<double>(m_uds.configuration_id)},
            {QStringLiteral("events"), events},
            {QStringLiteral("stats"), stats},
            {QStringLiteral("rules"), rules},
            {QStringLiteral("enums"), enums},
        };
    }

    if (!m_playgo.magic.empty()) {
        QJsonArray chunks, scenarios, ranges;
        for (const auto& ch : m_playgo.chunks) {
            chunks.append(QJsonObject{{QStringLiteral("label"), str(ch.label)},
                                      {QStringLiteral("flag"), hex(ch.flag)},
                                      {QStringLiteral("required_locus"), ch.required_locus},
                                      {QStringLiteral("language_mask"), hex(ch.language_mask)}});
        }
        for (const auto& sc : m_playgo.scenarios) {
            QJsonArray list;
            for (const auto id : sc.chunks) {
                list.append(id);
            }
            scenarios.append(
                QJsonObject{{QStringLiteral("label"), str(sc.label)},
                            {QStringLiteral("type"), hex(sc.type)},
                            {QStringLiteral("initial_chunk_count"), sc.initial_chunk_count},
                            {QStringLiteral("chunks"), list}});
        }
        for (const auto& r : m_playgo.ranges) {
            ranges.append(QJsonObject{{QStringLiteral("offset"), hex(r.offset)},
                                      {QStringLiteral("size"), num(r.size)}});
        }
        root[QStringLiteral("playgo")] = QJsonObject{
            {QStringLiteral("format"), str(m_playgo.magic)},
            {QStringLiteral("version"), hex(m_playgo.version)},
            {QStringLiteral("default_scenario"), m_playgo.default_scenario},
            {QStringLiteral("chunks"), chunks},
            {QStringLiteral("scenarios"), scenarios},
            {QStringLiteral("storage_ranges"), ranges},
        };
    }

    if (!m_keys.passcode.empty()) {
        QJsonArray files;
        for (const auto& f : m_keys.files) {
            files.append(QJsonObject{
                {QStringLiteral("path"), str(f.path)},
                {QStringLiteral("directory"), f.is_dir},
                {QStringLiteral("size"), num(f.size)},
                {QStringLiteral("blocks"), num(f.block_count)},
                {QStringLiteral("check"),
                 f.is_dir                                        ? QStringLiteral("directory")
                 : f.first_block == PS5Pkg::BlockCheck::Ok       ? QStringLiteral("ok")
                 : f.first_block == PS5Pkg::BlockCheck::Mismatch ? QStringLiteral("mismatch")
                                                                 : QStringLiteral("unreadable")}});
        }
        root[QStringLiteral("keys")] = QJsonObject{
            {QStringLiteral("verified"), m_keys.verified},
            {QStringLiteral("error"), str(m_keys.error)},
            {QStringLiteral("scheme"), str(m_keys.scheme)},
            {QStringLiteral("unencrypted_image"), m_keys.plaintext},
            {QStringLiteral("key_fingerprint"), str(m_keys.ekpfs_fingerprint)},
            {QStringLiteral("blocks_checked"), static_cast<int>(m_keys.blocks_checked)},
            {QStringLiteral("blocks_matched"), static_cast<int>(m_keys.blocks_matched)},
            {QStringLiteral("files"), files},
        };
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

void PkgInfoDialog::onExportReport() {
    if (m_path.isEmpty()) {
        return;
    }
    const QString json_filter = tr("JSON report (*.json)");
    const QString text_filter = tr("Text report (*.txt)");
    const QString base = m_info.param && !m_info.param->title_id.empty()
                             ? S(m_info.param->title_id)
                             : QFileInfo(m_path).completeBaseName();
    QString filter = json_filter;
    QString out = QFileDialog::getSaveFileName(
        this, tr("Export Report"),
        QFileInfo(m_path).dir().filePath(base + QStringLiteral("_pkg_info.json")),
        json_filter + QStringLiteral(";;") + text_filter, &filter);
    if (out.isEmpty()) {
        return;
    }
    const bool text =
        filter == text_filter || out.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive);
    if (text && out.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        out.chop(5);
        out += QStringLiteral(".txt");
    }
    const QByteArray data = text ? summaryText().toUtf8() + '\n' : reportJson();
    QFile f(out);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
        QMessageBox::warning(this, tr("Export Report"),
                             tr("Could not write %1.").arg(QDir::toNativeSeparators(out)));
    }
}

// ---------------------------------------------------------------- Game files

QWidget* PkgInfoDialog::buildFilesTab() {
    m_files_page = new QWidget(this);
    auto* layout = new QVBoxLayout(m_files_page);
    m_files_summary = new QLabel(m_files_page);
    m_files_summary->setWordWrap(true);
    m_files_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_files_summary);
    m_files_tree = new QTreeWidget(m_files_page);
    m_files_tree->setColumnCount(2);
    m_files_tree->setHeaderLabels({tr("Name"), tr("Size")});
    m_files_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_files_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_files_tree->header()->setStretchLastSection(false);
    layout->addWidget(m_files_tree, 1);
    auto* row = new QHBoxLayout();
    m_files_save = new QPushButton(tr("Save File..."), m_files_page);
    m_files_save->setEnabled(false);
    row->addStretch();
    row->addWidget(m_files_save);
    layout->addLayout(row);
    connect(m_files_tree, &QTreeWidget::itemSelectionChanged, this, [this] {
        const auto items = m_files_tree->selectedItems();
        m_files_save->setEnabled(!items.isEmpty() &&
                                 items.front()->data(0, Qt::UserRole).isValid());
    });
    connect(m_files_tree, &QTreeWidget::itemDoubleClicked, this, [this] { onSaveGameFile(); });
    connect(m_files_save, &QPushButton::clicked, this, &PkgInfoDialog::onSaveGameFile);
    return m_files_page;
}

void PkgInfoDialog::populateFiles() {
    m_files_tree->clear();
    m_files_summary->clear();
    m_inner.reset();
    const bool has_image = std::any_of(m_keys.files.begin(), m_keys.files.end(), [](const auto& f) {
        return f.path == "uroot/pfs_image.dat";
    });
    m_tabs->setTabVisible(m_tabs->indexOf(m_files_page), keysMatched() && has_image);
    if (!keysMatched() || !has_image) {
        return;
    }
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    auto inner = std::make_unique<PS5Pkg::InnerImage>();
    std::string why;
    const bool ok = inner->Open(m_info, m_keys, &why);
    QGuiApplication::restoreOverrideCursor();
    if (!ok) {
        m_files_summary->setText(tr("The game files could not be listed: %1").arg(S(why)));
        return;
    }
    const auto& files = inner->Files();
    u64 total = 0;
    QHash<QString, QTreeWidgetItem*> dirs;
    std::function<QTreeWidgetItem*(const QString&)> dir_item = [&](const QString& dir) {
        if (dir.isEmpty()) {
            return static_cast<QTreeWidgetItem*>(nullptr);
        }
        if (auto it = dirs.find(dir); it != dirs.end()) {
            return it.value();
        }
        const qsizetype slash = dir.lastIndexOf(QLatin1Char('/'));
        QTreeWidgetItem* parent = dir_item(slash < 0 ? QString() : dir.left(slash));
        const QString name = slash < 0 ? dir : dir.mid(slash + 1);
        auto* item = parent ? new QTreeWidgetItem(parent, {name})
                            : new QTreeWidgetItem(m_files_tree, {name});
        dirs.insert(dir, item);
        return item;
    };
    for (size_t i = 0; i < files.size(); i++) {
        const QString path = S(files[i].path);
        const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
        QTreeWidgetItem* parent = dir_item(slash < 0 ? QString() : path.left(slash));
        const QString name = slash < 0 ? path : path.mid(slash + 1);
        const QString size = QLocale().formattedDataSize(static_cast<qint64>(files[i].size));
        auto* item = parent ? new QTreeWidgetItem(parent, {name, size})
                            : new QTreeWidgetItem(m_files_tree, {name, size});
        item->setData(0, Qt::UserRole, static_cast<qulonglong>(i));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setToolTip(0, path);
        total += files[i].size;
    }
    m_files_tree->sortItems(0, Qt::AscendingOrder);
    for (int i = 0; i < m_files_tree->topLevelItemCount(); i++) {
        m_files_tree->topLevelItem(i)->setExpanded(true);
    }
    size_t compressed = 0;
    for (const auto& b : inner->Layout().blocks) {
        compressed += b.packed != 0 ? b.packed : b.packed_first;
    }
    m_files_summary->setText(
        tr("%n file(s), %1 in total, stored as %2 in pfs_image.dat (Kraken-compressed "
           "blocks). Double-click a file to save it.",
           nullptr, static_cast<int>(files.size()))
            .arg(QLocale().formattedDataSize(static_cast<qint64>(total)),
                 QLocale().formattedDataSize(static_cast<qint64>(compressed))));
    m_inner = std::move(inner);
}

void PkgInfoDialog::onSaveGameFile() {
    const auto items = m_files_tree->selectedItems();
    if (!m_inner || items.isEmpty() || !items.front()->data(0, Qt::UserRole).isValid()) {
        return;
    }
    const auto& file =
        m_inner->Files()[static_cast<size_t>(items.front()->data(0, Qt::UserRole).toULongLong())];
    const QString name = S(file.path).section(QLatin1Char('/'), -1);
    const QString target = QFileDialog::getSaveFileName(this, tr("Save File"), name);
    if (target.isEmpty()) {
        return;
    }
    QProgressDialog progress(tr("Saving %1...").arg(name), tr("Cancel"), 0, 1000, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    std::string why;
    const bool ok = m_inner->ExtractFile(
        file, Common::FS::PathFromQString(target),
        [&](u64 bytes) {
            progress.setValue(file.size ? static_cast<int>(bytes * 1000 / file.size) : 1000);
            QCoreApplication::processEvents();
            return !progress.wasCanceled();
        },
        &why);
    progress.close();
    if (!ok) {
        std::error_code ec;
        std::filesystem::remove(Common::FS::PathFromQString(target), ec);
        QMessageBox::warning(this, tr("Save File"),
                             tr("%1 could not be saved: %2").arg(name, S(why)));
    }
}

// ---------------------------------------------------------------- PlayGo

QWidget* PkgInfoDialog::buildPlayGoTab() {
    m_playgo_page = new QWidget(this);
    auto* layout = new QVBoxLayout(m_playgo_page);
    m_playgo_summary = new QLabel(m_playgo_page);
    m_playgo_summary->setWordWrap(true);
    m_playgo_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_playgo_summary);
    m_playgo_tree = new QTreeWidget(m_playgo_page);
    m_playgo_tree->setColumnCount(2);
    m_playgo_tree->setHeaderLabels({tr("Item"), tr("Details")});
    m_playgo_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_playgo_tree->header()->resizeSection(0, 300);
    m_playgo_tree->header()->setStretchLastSection(true);
    layout->addWidget(m_playgo_tree, 1);
    auto* note = new QLabel(
        tr("PlayGo lets a game start before it has finished installing: the package is split "
           "into chunks, and each scenario lists the chunks it needs, in order."),
        m_playgo_page);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(note);
    return m_playgo_page;
}

void PkgInfoDialog::populatePlayGo() {
    m_playgo_tree->clear();
    m_playgo_summary->clear();
    m_playgo = {};
    m_playgo_text.clear();

    // The metadata entry, or the copy in the install-metadata archive.
    std::optional<std::vector<u8>> data;
    if (const auto* e = m_info.FindEntry(0x1001)) {
        data = PS5Pkg::DecryptEntry(m_info, *e, m_passcode->text().toStdString());
    }
    if (!data) {
        for (const auto& m : m_info.si_members) {
            if (m.name.ends_with("playgo-chunk.dat")) {
                data = PS5Pkg::ReadSiMember(m_info, m);
                break;
            }
        }
    }
    PlayGoInfo pg;
    const bool ok = data && pg.Parse(*data);
    m_tabs->setTabVisible(m_tabs->indexOf(m_playgo_page), data.has_value());
    if (!data) {
        return;
    }
    if (!ok) {
        m_playgo_summary->setText(tr("playgo-chunk.dat could not be read: %1").arg(S(pg.error)));
        return;
    }
    m_playgo = pg;

    auto section = [&](const QString& label, size_t count) {
        auto* item = new QTreeWidgetItem(m_playgo_tree, {tr("%1 (%2)").arg(label).arg(count)});
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
        item->setFirstColumnSpanned(true);
        item->setExpanded(true);
        return item;
    };
    auto label_or = [](const std::string& label, size_t index) {
        return label.empty() ? QStringLiteral("#%1").arg(index)
                             : QStringLiteral("#%1 %2").arg(index).arg(S(label));
    };

    auto* chunks = section(tr("Chunks"), pg.chunks.size());
    for (size_t k = 0; k < pg.chunks.size(); k++) {
        const auto& ch = pg.chunks[k];
        QStringList details;
        details << (PlayGoInfo::AllLanguages(ch.language_mask)
                        ? tr("all languages")
                        : tr("languages %1").arg(Hex(ch.language_mask, 16)));
        details << tr("flag %1").arg(Hex(ch.flag, 2));
        if (ch.disc_layer != 0) {
            details << tr("disc layer %1").arg(ch.disc_layer);
        }
        new QTreeWidgetItem(chunks, {label_or(ch.label, k), details.join(QStringLiteral(" · "))});
    }

    auto* scenarios = section(tr("Scenarios"), pg.scenarios.size());
    for (size_t k = 0; k < pg.scenarios.size(); k++) {
        const auto& sc = pg.scenarios[k];
        QStringList ids;
        for (const auto id : sc.chunks) {
            ids << (id < pg.chunks.size() && !pg.chunks[id].label.empty()
                        ? S(pg.chunks[id].label)
                        : QStringLiteral("#%1").arg(id));
        }
        QString name = label_or(sc.label, k);
        if (k == pg.default_scenario) {
            name += tr(" (default)");
        }
        new QTreeWidgetItem(scenarios, {name, tr("type %1 · playable after %2 of %3 chunk(s): %4")
                                                  .arg(Hex(sc.type, 2))
                                                  .arg(sc.initial_chunk_count)
                                                  .arg(sc.chunks.size())
                                                  .arg(ids.join(QStringLiteral(", ")))});
    }

    auto* ranges = section(tr("Storage ranges"), pg.ranges.size());
    for (size_t k = 0; k < pg.ranges.size(); k++) {
        const auto& r = pg.ranges[k];
        new QTreeWidgetItem(ranges,
                            {QStringLiteral("#%1").arg(k),
                             QStringLiteral("%1 – %2 (%3)")
                                 .arg(Hex(r.offset), Hex(r.offset + r.size),
                                      QLocale().formattedDataSize(static_cast<qint64>(r.size)))});
    }

    QStringList parts;
    parts << tr("%n chunk(s)", nullptr, static_cast<int>(pg.chunks.size()))
          << tr("%n scenario(s)", nullptr, static_cast<int>(pg.scenarios.size()));
    if (pg.default_scenario < pg.scenarios.size()) {
        parts << tr("default: %1")
                     .arg(label_or(pg.scenarios[pg.default_scenario].label, pg.default_scenario));
    }
    m_playgo_summary->setText(
        QStringLiteral("<b>%1</b><br>%2")
            .arg(parts.join(QStringLiteral(" · ")).toHtmlEscaped(),
                 tr("Format %1, version %2").arg(S(pg.magic), Hex(pg.version, 4)).toHtmlEscaped()));
    m_playgo_text = parts.join(QStringLiteral(", "));
}

bool PkgInfoDialog::contentIdKeys() const {
    return m_info.kind == PS5Pkg::PackageKind::Debug ||
           (m_info.kind == PS5Pkg::PackageKind::Retail && keysMatched());
}

QString PkgInfoDialog::kindLabel() const {
    if (m_info.kind == PS5Pkg::PackageKind::Retail && keysMatched()) {
        return tr("Fake package (retail header)");
    }
    return S(PS5Pkg::KindName(m_info.kind));
}

void PkgInfoDialog::updateHeaderStatus() {
    if (m_path.isEmpty()) {
        m_status->clear();
        return;
    }
    QString status = QStringLiteral("<b>%1</b> - %2")
                         .arg(kindLabel().toHtmlEscaped(), statusText().toHtmlEscaped());
    if (!m_info.error.empty()) {
        status += QStringLiteral("<br><span style=\"color:#d9534f\">%1</span>")
                      .arg(S(m_info.error).toHtmlEscaped());
    }
    m_status->setText(status);
}

void PkgInfoDialog::refreshDecryptedViews() {
    populateOverview();
    populateEntries();
    populateTrophies();
    populateActivities();
    populatePlayGo();
    populateFiles();
    updateHeaderStatus();
}

bool PkgInfoDialog::keysMatched() const {
    return !m_keys.scheme.empty();
}
