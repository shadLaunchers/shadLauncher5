// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "pkg_info_dialog.h"

#include <map>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include "core/file_format/dds.h"

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

// Sorts by the stored number while showing formatted text.
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

// Decodes PNG or DDS artwork bytes; returns a null image on failure.
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

// Text preview for small files that look like text; empty otherwise.
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
    m_tabs->addTab(buildEntriesTab(), tr("Entries"));
    m_tabs->addTab(buildSegmentsTab(), tr("Segments"));
    m_tabs->addTab(buildSiTab(), tr("Install Metadata"));

    m_param = new QPlainTextEdit(this);
    m_param->setReadOnly(true);
    m_param->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_param->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_tabs->addTab(m_param, QStringLiteral("param.json"));

    layout->addWidget(m_tabs, 1);

    auto* buttons = new QHBoxLayout();
    auto* open_btn = new QPushButton(tr("Open PKG..."), this);
    m_copy = new QPushButton(tr("Copy Summary"), this);
    auto* close_btn = new QPushButton(tr("Close"), this);
    buttons->addWidget(open_btn);
    buttons->addWidget(m_copy);
    buttons->addStretch();
    buttons->addWidget(close_btn);
    layout->addLayout(buttons);

    connect(open_btn, &QPushButton::clicked, this, &PkgInfoDialog::onOpen);
    connect(m_copy, &QPushButton::clicked, this, &PkgInfoDialog::onCopySummary);
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
    row->addStretch();
    layout->addLayout(row);
    connect(m_save_entry, &QPushButton::clicked, this, &PkgInfoDialog::onSaveEntry);
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
        return tr("Its files are encrypted with keys derived from the "
                  "content ID, so they can be extracted without console keys (extraction is "
                  "not available yet).");
    case PackageKind::Retail:
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
        if (m_info.patch_kind != PS5Pkg::PatchKind::None) {
            ids << S(PS5Pkg::PatchKindName(m_info.patch_kind));
        }
        if (!m_info.content_id.empty()) {
            ids << S(PS5Pkg::RegionFromContentId(m_info.content_id));
        }
        ids << QLocale().formattedDataSize(static_cast<qint64>(m_info.file_size));
        m_subtitle->setText(ids.join(QStringLiteral("  ·  ")));

        QString status = QStringLiteral("<b>%1</b> - %2")
                             .arg(S(PS5Pkg::KindName(m_info.kind)), statusText().toHtmlEscaped());
        if (!m_info.error.empty()) {
            status += QStringLiteral("<br><span style=\"color:#d9534f\">%1</span>")
                          .arg(S(m_info.error).toHtmlEscaped());
        }
        m_status->setText(status);
    }

    populateOverview();
    populateEntries();
    populateSegments();
    populateSi();

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
    addRow(pkg, tr("Type"), S(PS5Pkg::KindName(i.kind)));
    if (i.has_cnt) {
        addRow(pkg, tr("Content ID"), S(i.content_id));
        addRow(pkg, tr("Content type"), S(PS5Pkg::ContentTypeName(i.content_type)));
        addRow(pkg, tr("DRM type"), S(PS5Pkg::DrmTypeName(i.drm_type)));
        addRow(pkg, tr("Region"), S(PS5Pkg::RegionFromContentId(i.content_id)));
        addRow(pkg, tr("Patch type"), S(PS5Pkg::PatchKindName(i.patch_kind)));
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
            if (const auto it = fields.find(key); it != fields.end() && !it->second.empty()) {
                addRow(app, label, S(it->second));
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
        m_entries->setItem(row, 5, Cell(e.IsEncrypted() ? tr("Yes") : tr("No")));
    }
    m_entries->setSortingEnabled(true);
    m_entries->sortByColumn(0, Qt::AscendingOrder);
}

void PkgInfoDialog::onEntrySelectionChanged() {
    const auto rows = m_entries->selectionModel()->selectedRows();
    bool can_save = false;
    if (!rows.isEmpty()) {
        const int idx = m_entries->item(rows.front().row(), 0)->data(kEntryIndexRole).toInt();
        can_save = !m_info.entries[static_cast<size_t>(idx)].IsEncrypted();
    }
    m_save_entry->setEnabled(can_save);
}

void PkgInfoDialog::onSaveEntry() {
    const auto rows = m_entries->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return;
    }
    const int idx = m_entries->item(rows.front().row(), 0)->data(kEntryIndexRole).toInt();
    const auto& e = m_info.entries[static_cast<size_t>(idx)];
    if (e.IsEncrypted()) {
        return;
    }
    std::string problem;
    const auto data = PS5Pkg::ReadEntryData(m_info, e, &problem);
    if (!data) {
        QMessageBox::warning(this, tr("Save Entry"), S(problem));
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
          << QStringLiteral("%1: %2").arg(S(PS5Pkg::KindName(m_info.kind)), statusText());
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
