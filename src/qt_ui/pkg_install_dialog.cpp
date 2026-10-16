// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "pkg_install_dialog.h"

#include <algorithm>

#include <atomic>
#include <mutex>
#include <optional>

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStorageInfo>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>

#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "core/file_format/param.h"
#include "core/file_format/ps5_pkg_extract.h"
#include "core/file_format/ps5_pkg_image.h"
#include "core/file_format/ps5_pkg_keys.h"
#include "gui_settings.h"
#include "pkg_info_dialog.h"

namespace {

enum Column { ColInstall, ColTitleId, ColTitle, ColType, ColVersion, ColPackage, ColNotes };

QString S(std::string_view s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

int CompareVersions(const std::string& a, const std::string& b) {
    const QStringList pa = S(a).split('.', Qt::SkipEmptyParts);
    const QStringList pb = S(b).split('.', Qt::SkipEmptyParts);
    for (qsizetype i = 0; i < std::max(pa.size(), pb.size()); i++) {
        const int va = i < pa.size() ? pa[i].toInt() : 0;
        const int vb = i < pb.size() ? pb[i].toInt() : 0;
        if (va != vb) {
            return va < vb ? -1 : 1;
        }
    }
    return 0;
}

std::string VersionOf(const PS5Pkg::PkgInfo& info) {
    return info.param ? info.param->app_ver : std::string{};
}

} // namespace

std::vector<PS5Pkg::PkgInfo> PkgInstallDialog::ReadPackages(
    const std::vector<std::filesystem::path>& files) {
    std::vector<PS5Pkg::PkgInfo> packages;
    packages.reserve(files.size());
    for (const auto& file : files) {
        packages.push_back(PS5Pkg::ReadPkgInfo(file));
    }
    std::stable_sort(packages.begin(), packages.end(), [](const auto& a, const auto& b) {
        const bool ia = PS5Pkg::CanInstallSceSys(a);
        const bool ib = PS5Pkg::CanInstallSceSys(b);
        if (ia != ib) {
            return ia; // installable packages first
        }
        const std::string ta = PS5Pkg::PackageTitleId(a);
        const std::string tb = PS5Pkg::PackageTitleId(b);
        if (ta != tb) {
            return ta < tb;
        }
        const int ka = static_cast<int>(PS5Pkg::GetInstallKind(a));
        const int kb = static_cast<int>(PS5Pkg::GetInstallKind(b));
        if (ka != kb) {
            return ka < kb;
        }
        return CompareVersions(VersionOf(a), VersionOf(b)) < 0;
    });
    return packages;
}

PkgInstallDialog::PkgInstallDialog(std::shared_ptr<EmulatorSettingsImpl> emu_settings,
                                   std::shared_ptr<GUISettings> gui_settings,
                                   std::vector<PS5Pkg::PkgInfo> packages, QWidget* parent)
    : QDialog(parent), m_emu_settings(std::move(emu_settings)),
      m_gui_settings(std::move(gui_settings)), m_packages(std::move(packages)) {
    setWindowTitle(tr("shadLauncher5 - Install PKG Files"));
    auto* layout = new QVBoxLayout(this);

    // Packages.
    auto* list_group = new QGroupBox(tr("Select PKG files to install"));
    auto* list_layout = new QVBoxLayout(list_group);
    m_table = new QTableWidget(0, 7);
    m_table->setHorizontalHeaderLabels({tr("Install"), tr("Title ID"), tr("Title"), tr("Type"),
                                        tr("Version"), tr("Package"), tr("Notes")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setStretchLastSection(true);
    list_layout->addWidget(m_table);

    auto* row_buttons = new QHBoxLayout();
    auto* select_all = new QPushButton(tr("Select All"));
    auto* select_none = new QPushButton(tr("Select None"));
    auto* invert = new QPushButton(tr("Invert Selection"));
    m_details = new QPushButton(tr("PKG Info..."));
    m_details->setToolTip(tr("Show everything about the selected package."));
    row_buttons->addWidget(select_all);
    row_buttons->addWidget(select_none);
    row_buttons->addWidget(invert);
    row_buttons->addStretch();
    row_buttons->addWidget(m_details);
    list_layout->addLayout(row_buttons);
    layout->addWidget(list_group, 1);

    auto set_all = [this](auto&& state_of) {
        for (int r = 0; r < m_table->rowCount(); r++) {
            QTableWidgetItem* item = m_table->item(r, ColInstall);
            if ((item->flags() & Qt::ItemIsUserCheckable) != 0) {
                item->setCheckState(state_of(item->checkState()));
            }
        }
    };
    connect(select_all, &QPushButton::clicked, this,
            [set_all] { set_all([](Qt::CheckState) { return Qt::Checked; }); });
    connect(select_none, &QPushButton::clicked, this,
            [set_all] { set_all([](Qt::CheckState) { return Qt::Unchecked; }); });
    connect(invert, &QPushButton::clicked, this, [set_all] {
        set_all([](Qt::CheckState s) { return s == Qt::Checked ? Qt::Unchecked : Qt::Checked; });
    });
    connect(m_details, &QPushButton::clicked, this, &PkgInstallDialog::onDetails);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &PkgInstallDialog::onDetails);
    connect(m_table, &QTableWidget::itemChanged, this, [this] { updateState(); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] { updateState(); });

    // Destination.
    auto* dir_group = new QGroupBox(tr("Installation Directory"));
    auto* dir_layout = new QVBoxLayout(dir_group);
    auto* dir_row = new QHBoxLayout();
    m_dir_combo = new QComboBox();
    m_dir_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_browse = new QPushButton(tr("Browse..."));
    dir_row->addWidget(m_dir_combo, 1);
    dir_row->addWidget(m_browse);
    dir_layout->addLayout(dir_row);
    m_dir_note = new QLabel();
    m_dir_note->setWordWrap(true);
    dir_layout->addWidget(m_dir_note);
    layout->addWidget(dir_group);

    for (const auto& dir : m_emu_settings->GetGameInstallDirs()) {
        QString text;
        Common::FS::PathToQString(text, dir);
        m_dir_combo->addItem(text);
    }
    const QString last = m_gui_settings->GetValue(GUI::general_last_pkg_install_dir).toString();
    if (const int found = m_dir_combo->findText(last); found >= 0) {
        m_dir_combo->setCurrentIndex(found);
    }
    setInstallDirectory(m_dir_combo->currentText());
    connect(m_dir_combo, &QComboBox::currentTextChanged, this,
            [this](const QString& text) { setInstallDirectory(text); });
    connect(m_browse, &QPushButton::clicked, this, &PkgInstallDialog::onBrowse);

    auto* stage =
        new QLabel(tr("Installs the game files (decompressing Kraken-packed blocks) and sce_sys: "
                      "param.json, artwork, trophies, activities, PlayGo and licence files, with "
                      "encrypted ones decrypted."));
    stage->setWordWrap(true);
    layout->addWidget(stage);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_install = buttons->button(QDialogButtonBox::Ok);
    m_install->setText(tr("Install Selected"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!selectionIsAddOnOnly()) {
            QString text;
            Common::FS::PathToQString(text, m_install_dir);
            m_gui_settings->SetValue(GUI::general_last_pkg_install_dir, text);
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    populate();
    resize(900, 440);
}

void PkgInstallDialog::populate() {
    const QSignalBlocker block(m_table);
    m_table->setRowCount(static_cast<int>(m_packages.size()));
    for (int r = 0; r < static_cast<int>(m_packages.size()); r++) {
        const PS5Pkg::PkgInfo& info = m_packages[r];
        std::string why;
        const bool installable = PS5Pkg::CanInstallSceSys(info, &why);
        QString file_name, full_path;
        Common::FS::PathToQString(file_name, info.path.filename());
        Common::FS::PathToQString(full_path, info.path);

        auto* check = new QTableWidgetItem();
        if (installable) {
            check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            check->setCheckState(Qt::Checked);
        } else {
            check->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        }
        m_table->setItem(r, ColInstall, check);

        const QString title =
            info.param && !info.param->title.empty() ? S(info.param->title) : file_name;
        auto* title_item = new QTableWidgetItem(title);
        title_item->setToolTip(full_path);
        m_table->setItem(r, ColTitleId, new QTableWidgetItem(S(PS5Pkg::PackageTitleId(info))));
        m_table->setItem(r, ColTitle, title_item);
        m_table->setItem(r, ColType,
                         new QTableWidgetItem(
                             installable ? S(PS5Pkg::InstallKindName(PS5Pkg::GetInstallKind(info)))
                                         : QString()));
        m_table->setItem(r, ColVersion, new QTableWidgetItem(S(VersionOf(info))));
        m_table->setItem(r, ColPackage, new QTableWidgetItem(S(PS5Pkg::KindName(info.kind))));

        QString notes;
        if (!installable) {
            notes = tr("Cannot install: %1").arg(S(why));
        } else {
            const auto items = PS5Pkg::ListSceSysItems(info);
            const auto encrypted = std::count_if(
                items.begin(), items.end(), [](const auto& i) { return i.entry.IsEncrypted(); });
            notes = tr("%n sce_sys file(s)", nullptr, static_cast<int>(items.size()));
            if (encrypted > 0) {
                notes += tr(", %n to decrypt", nullptr, static_cast<int>(encrypted));
            }
        }
        auto* notes_item = new QTableWidgetItem(notes);
        notes_item->setToolTip(notes);
        m_table->setItem(r, ColNotes, notes_item);
        if (!installable) {
            for (int c = 0; c < m_table->columnCount(); c++) {
                m_table->item(r, c)->setForeground(
                    palette().color(QPalette::Disabled, QPalette::Text));
            }
        }
    }
    m_table->resizeColumnsToContents();
    m_table->setColumnWidth(ColTitle, std::min(m_table->columnWidth(ColTitle), 280));
    if (m_table->rowCount() > 0) {
        m_table->selectRow(0);
    }
    updateState();
}

std::vector<PS5Pkg::PkgInfo> PkgInstallDialog::SelectedPackages() const {
    std::vector<PS5Pkg::PkgInfo> selected;
    for (int r = 0; r < m_table->rowCount(); r++) {
        if (m_table->item(r, ColInstall)->checkState() == Qt::Checked) {
            selected.push_back(m_packages[r]);
        }
    }
    return selected;
}

bool PkgInstallDialog::selectionIsAddOnOnly() const {
    bool any = false;
    for (int r = 0; r < m_table->rowCount(); r++) {
        if (m_table->item(r, ColInstall)->checkState() != Qt::Checked) {
            continue;
        }
        any = true;
        if (PS5Pkg::GetInstallKind(m_packages[r]) != PS5Pkg::InstallKind::AddOn) {
            return false;
        }
    }
    return any;
}

void PkgInstallDialog::setInstallDirectory(const QString& dir) {
    const auto path = Common::FS::PathFromQString(dir);
    std::error_code ec;
    m_install_dir =
        !dir.isEmpty() && std::filesystem::is_directory(path, ec) ? path : std::filesystem::path{};
    updateState();
}

void PkgInstallDialog::onBrowse() {
    QString current;
    Common::FS::PathToQString(current, m_install_dir);
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Directory"), current);
    if (dir.isEmpty()) {
        return;
    }
    if (m_dir_combo->findText(dir) < 0) {
        m_dir_combo->addItem(dir);
        m_emu_settings->AddGameInstallDir(Common::FS::PathFromQString(dir));
        m_emu_settings->Save();
    }
    m_dir_combo->setCurrentText(dir);
}

void PkgInstallDialog::onDetails() {
    const int row = m_table->currentRow();
    if (row < 0 || row >= static_cast<int>(m_packages.size())) {
        return;
    }
    QString path;
    Common::FS::PathToQString(path, m_packages[row].path);
    auto* dialog = new PkgInfoDialog(path, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void PkgInstallDialog::updateState() {
    if (m_install == nullptr) {
        return;
    }
    bool any = false;
    bool needs_addon_dir = false;
    for (int r = 0; r < m_table->rowCount(); r++) {
        if (m_table->item(r, ColInstall)->checkState() == Qt::Checked) {
            any = true;
            needs_addon_dir |= PS5Pkg::GetInstallKind(m_packages[r]) == PS5Pkg::InstallKind::AddOn;
        }
    }
    const bool addon_only = selectionIsAddOnOnly();
    m_dir_combo->setEnabled(!addon_only);
    m_browse->setEnabled(!addon_only);
    m_details->setEnabled(m_table->currentRow() >= 0);

    QString addon_dir;
    Common::FS::PathToQString(addon_dir, m_emu_settings->GetAddonInstallDir());
    QString note;
    bool addon_ok = true;
    if (needs_addon_dir) {
        addon_ok = !addon_dir.isEmpty();
        note = addon_ok ? tr("Add-ons are installed to the add-on folder: %1").arg(addon_dir)
                        : tr("Add-ons need an add-on folder; set one in Settings > Paths.");
    }
    if (!addon_only && m_install_dir.empty()) {
        note = (note.isEmpty() ? QString() : note + QStringLiteral("\n")) +
               tr("Choose an installation directory; add one with Browse.");
    }
    m_dir_note->setText(note);
    m_dir_note->setVisible(!note.isEmpty());
    m_install->setEnabled(any && addon_ok && (addon_only || !m_install_dir.empty()));
}

namespace {

struct InstallOutcome {
    QString title;
    QString kind;
    QString destination;
    int written = 0;
    int decrypted = 0;
    int game_files = 0;
    u64 game_bytes = 0;
    QString game_note; // why game files were not installed, if they were not
    QStringList problems;
    bool skipped = false;
    QString skip_reason;
};

bool AskYesNo(QWidget* parent, const QString& text) {
    return QMessageBox::question(parent, QObject::tr("PKG Installation"), text,
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) == QMessageBox::Yes;
}

QString PathText(const std::filesystem::path& path) {
    QString text;
    Common::FS::PathToQString(text, path);
    return text;
}

std::string InstalledVersion(const std::filesystem::path& folder) {
    Param param;
    if (param.Open(folder / "sce_sys" / "param.json")) {
        return param.app_ver;
    }
    return {};
}

std::filesystem::path ResolveDestination(QWidget* parent, EmulatorSettingsImpl& emu,
                                         const std::filesystem::path& install_dir, int depth,
                                         const PS5Pkg::PkgInfo& info, InstallOutcome& outcome) {
    namespace fs = std::filesystem;
    const std::string title_id = PS5Pkg::PackageTitleId(info);
    std::error_code ec;
    auto skip = [&](const QString& why) {
        outcome.skipped = true;
        outcome.skip_reason = why;
        return fs::path{};
    };

    switch (PS5Pkg::GetInstallKind(info)) {
    case PS5Pkg::InstallKind::AddOn: {
        const fs::path addon_root = emu.GetAddonInstallDir();
        const std::string label = PS5Pkg::EntitlementLabel(info.content_id);
        if (addon_root.empty() || label.empty()) {
            return skip(QObject::tr("No add-on folder is set, or the content ID has no label."));
        }
        const fs::path dest = addon_root / title_id / label;
        if (fs::exists(dest, ec) &&
            !AskYesNo(parent, QObject::tr("Add-on already installed:\n%1\n\nOverwrite its "
                                          "sce_sys files?")
                                  .arg(PathText(dest)))) {
            return skip({});
        }
        return dest;
    }
    case PS5Pkg::InstallKind::Patch: {
        const auto game = Common::FS::FindGameByID(install_dir, title_id, depth);
        if (!game) {
            return skip(QObject::tr("This is a patch; install the game %1 first.")
                            .arg(QString::fromStdString(title_id)));
        }
        fs::path stem = *game;
        if (stem.extension() == ".zar") {
            stem.replace_extension();
        }
        fs::path dest = stem;
        dest += "-patch";
        const std::string pkg_ver = info.param ? info.param->app_ver : std::string{};
        std::string have = InstalledVersion(dest);
        if (have.empty() && fs::is_directory(*game, ec)) {
            have = InstalledVersion(*game);
        }
        QString question;
        const int cmp = CompareVersions(pkg_ver, have);
        if (have.empty()) {
            question = QObject::tr("Install patch %1 to\n%2 ?")
                           .arg(QString::fromStdString(pkg_ver), PathText(dest));
        } else if (cmp == 0) {
            question = QObject::tr("Patch detected!\nPKG and installed versions match: %1\n"
                                   "Would you like to overwrite?")
                           .arg(QString::fromStdString(pkg_ver));
        } else if (cmp < 0) {
            question = QObject::tr("Patch detected!\nPKG version %1 is older than the installed "
                                   "version %2.\nWould you like to overwrite?")
                           .arg(QString::fromStdString(pkg_ver), QString::fromStdString(have));
        } else {
            question = QObject::tr("Patch detected!\nInstalled version: %1\nWould you like to "
                                   "install patch %2?")
                           .arg(QString::fromStdString(have), QString::fromStdString(pkg_ver));
        }
        if (!AskYesNo(parent, question)) {
            return skip({});
        }
        return dest;
    }
    case PS5Pkg::InstallKind::Game:
    default: {
        fs::path dest = install_dir / title_id;
        if (const auto game = Common::FS::FindGameByID(install_dir, title_id, depth)) {
            if (!fs::is_directory(*game, ec)) {
                return skip(QObject::tr("The game is already installed as an archive:\n%1")
                                .arg(PathText(*game)));
            }
            dest = *game;
        }
        if (fs::exists(dest, ec) &&
            !AskYesNo(parent, QObject::tr("Game already installed:\n%1\n\nWould you like to "
                                          "overwrite its sce_sys files?")
                                  .arg(PathText(dest)))) {
            return skip({});
        }
        return dest;
    }
    }
}

struct GameImage {
    PS5Pkg::KeyCheck keys;
    std::unique_ptr<PS5Pkg::InnerImage> image;
    QString note;
};

std::unique_ptr<GameImage> OpenGameImage(const PS5Pkg::PkgInfo& info) {
    auto game = std::make_unique<GameImage>();
    if (info.kind == PS5Pkg::PackageKind::Patch) {
        game->note = QObject::tr("Game files of update packages are not supported yet.");
        return game;
    }
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    game->keys = PS5Pkg::VerifyKeys(info);
    std::string why;
    if (game->keys.scheme.empty()) {
        game->note = QObject::tr("Game files not installed: %1").arg(S(game->keys.error));
    } else {
        game->image = std::make_unique<PS5Pkg::InnerImage>();
        if (!game->image->Open(info, game->keys, &why)) {
            game->image.reset();
            game->note = QObject::tr("Game files not installed: %1").arg(S(why));
        }
    }
    QGuiApplication::restoreOverrideCursor();
    return game;
}

u64 SpaceNeeded(const GameImage& game, const std::vector<PS5Pkg::SceSysItem>& items,
                const std::filesystem::path& dest) {
    namespace fs = std::filesystem;
    u64 needed = 0;
    auto add = [&](const fs::path& target, u64 size) {
        std::error_code ec;
        u64 have = fs::is_regular_file(target, ec) ? fs::file_size(target, ec) : 0;
        if (ec) {
            have = 0;
        }
        needed += size > have ? size - have : 0;
    };
    if (game.image) {
        for (const auto& f : game.image->Files()) {
            add(dest / Common::FS::PathFromQString(S(f.path)), f.size);
        }
    }
    for (const auto& item : items) {
        add(dest / "sce_sys" / Common::FS::PathFromQString(S(item.path)), item.entry.size);
    }
    return needed;
}

std::optional<u64> FreeSpace(std::filesystem::path dest) {
    std::error_code ec;
    while (!dest.empty() && !std::filesystem::exists(dest, ec)) {
        const auto parent = dest.parent_path();
        if (parent == dest) {
            break;
        }
        dest = parent;
    }
    const QStorageInfo storage(PathText(dest));
    if (!storage.isValid() || !storage.isReady()) {
        return std::nullopt;
    }
    return static_cast<u64>(storage.bytesAvailable());
}

} // namespace

// Extracts the game files (the inner image) into `dest`, in the background
// with a cancellable progress dialog. Returns false when the user cancelled.
static bool InstallGameFiles(QWidget* parent, GameImage& game, const std::filesystem::path& dest,
                             size_t number, size_t count, InstallOutcome& outcome) {
    if (!game.image) {
        outcome.game_note = game.note;
        return true;
    }
    const auto& image = game.image;
    const auto& files = image->Files();
    u64 total = 0;
    std::vector<std::filesystem::path> targets;
    for (const auto& f : files) {
        total += f.size;
        targets.push_back(dest / Common::FS::PathFromQString(S(f.path)));
    }

    std::atomic<u64> done{0};
    std::atomic<size_t> current{0};
    std::atomic<bool> cancel{false};
    QStringList problems;
    std::mutex problems_mutex;
    int written = 0;
    u64 written_bytes = 0;
    QFuture<void> work = QtConcurrent::run([&] {
        u64 base = 0;
        for (size_t i = 0; i < files.size() && !cancel; i++) {
            current = i;
            std::string err;
            const bool ok = image->ExtractFile(
                files[i], targets[i],
                [&](u64 bytes) {
                    done = base + bytes;
                    return !cancel.load();
                },
                &err);
            base += files[i].size;
            done = base;
            if (ok) {
                written++;
                written_bytes += files[i].size;
            } else {
                // Do not leave a half-written file that looks complete.
                std::error_code remove_error;
                std::filesystem::remove(targets[i], remove_error);
                if (!cancel) {
                    std::lock_guard lock(problems_mutex);
                    problems << QStringLiteral("%1: %2").arg(S(files[i].path), S(err));
                }
            }
        }
    });

    QProgressDialog progress(parent);
    progress.setWindowTitle(QObject::tr("PKG Installation"));
    progress.setRange(0, 1000);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setMinimumWidth(520);
    QElapsedTimer clock;
    clock.start();
    auto update = [&] {
        if (progress.wasCanceled()) {
            cancel = true;
        }
        const u64 d = done.load();
        const size_t i = std::min(current.load(), files.size() - 1);
        QString eta;
        const double secs = clock.elapsed() / 1000.0;
        if (d > 0 && secs > 2) {
            const double rate = d / secs;
            const int left = static_cast<int>((total - d) / rate);
            eta = QObject::tr(" - %1/s, about %2 min left")
                      .arg(QLocale().formattedDataSize(static_cast<qint64>(rate)))
                      .arg(left / 60 + 1);
        }
        progress.setLabelText(QObject::tr("Installing PKG %1/%2: game files\n%3 of %4%5\n%6")
                                  .arg(number)
                                  .arg(count)
                                  .arg(QLocale().formattedDataSize(static_cast<qint64>(d)),
                                       QLocale().formattedDataSize(static_cast<qint64>(total)), eta,
                                       QFontMetrics(progress.font())
                                           .elidedText(S(files[i].path), Qt::ElideMiddle, 480)));
        progress.setValue(total ? static_cast<int>(d * 1000 / total) : 0);
    };
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &progress, update);
    QObject::connect(&progress, &QProgressDialog::canceled, &progress, [&] { cancel = true; });
    QFutureWatcher<void> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(work);
    update();
    progress.show();
    timer.start(200);
    if (!work.isFinished()) {
        loop.exec();
    }
    timer.stop();

    outcome.game_files = written;
    outcome.game_bytes = written_bytes;
    outcome.problems << problems;
    if (cancel) {
        outcome.problems << QObject::tr("Cancelled after %n game file(s).", nullptr, written);
        return false;
    }
    return true;
}

std::vector<std::filesystem::path> PkgInstallDialog::PickPackages(QWidget* parent) {
    const QStringList names = QFileDialog::getOpenFileNames(
        parent, tr("Install PKG Files"), {}, tr("PS5 packages (*.pkg *.PKG);;All files (*)"));
    std::vector<std::filesystem::path> files;
    for (const QString& name : names) {
        files.push_back(Common::FS::PathFromQString(name));
    }
    return files;
}

bool PkgInstallDialog::InstallPackages(QWidget* parent,
                                       std::shared_ptr<EmulatorSettingsImpl> emu_settings,
                                       std::shared_ptr<GUISettings> gui_settings,
                                       const std::vector<std::filesystem::path>& files) {
    if (files.empty()) {
        return false;
    }
    std::vector<PS5Pkg::PkgInfo> packages;
    {
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        packages = ReadPackages(files);
        QGuiApplication::restoreOverrideCursor();
    }

    PkgInstallDialog dialog(emu_settings, gui_settings, std::move(packages), parent);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    const auto selected = dialog.SelectedPackages();
    const auto install_dir = dialog.InstallDirectory();
    const int depth =
        std::max(1, gui_settings->GetValue(GUI::general_directory_depth_scanning).toInt());

    std::vector<InstallOutcome> outcomes;
    bool wrote_any = false;
    bool cancelled = false;
    for (size_t n = 0; n < selected.size() && !cancelled; n++) {
        const PS5Pkg::PkgInfo& info = selected[n];
        InstallOutcome outcome;
        outcome.title = info.param && !info.param->title.empty() ? S(info.param->title)
                                                                 : S(PS5Pkg::PackageTitleId(info));
        outcome.kind = S(PS5Pkg::InstallKindName(PS5Pkg::GetInstallKind(info)));
        const auto dest =
            ResolveDestination(parent, *emu_settings, install_dir, depth, info, outcome);
        if (dest.empty()) {
            outcomes.push_back(std::move(outcome));
            continue;
        }
        outcome.destination = PathText(dest);

        // Nothing is written unless the whole package fits.
        const auto game = OpenGameImage(info);
        const auto items = PS5Pkg::ListSceSysItems(info);
        const u64 needed = SpaceNeeded(*game, items, dest);
        if (const auto free = FreeSpace(dest); free && *free < needed) {
            outcome.skipped = true;
            outcome.skip_reason =
                tr("Not installed: it needs %1 but only %2 is free on that drive.")
                    .arg(QLocale().formattedDataSize(static_cast<qint64>(needed)),
                         QLocale().formattedDataSize(static_cast<qint64>(*free)));
            outcomes.push_back(std::move(outcome));
            continue;
        }

        if (!InstallGameFiles(parent, *game, dest, n + 1, selected.size(), outcome)) {
            cancelled = true;
        }
        wrote_any |= outcome.game_files > 0;
        if (cancelled) {
            outcomes.push_back(std::move(outcome));
            break;
        }

        QProgressDialog progress(parent);
        progress.setWindowTitle(tr("PKG Installation"));
        progress.setLabelText(
            tr("Installing PKG %1/%2: %3").arg(n + 1).arg(selected.size()).arg(outcome.title));
        progress.setRange(0, static_cast<int>(items.size()));
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setAutoClose(false);
        progress.setAutoReset(false);
        progress.show();

        const std::filesystem::path sce_sys = dest / "sce_sys";
        for (size_t i = 0; i < items.size(); i++) {
            if (progress.wasCanceled()) {
                cancelled = true;
                outcome.problems << tr("Cancelled after %n file(s).", nullptr, static_cast<int>(i));
                break;
            }
            progress.setValue(static_cast<int>(i));
            QCoreApplication::processEvents();
            const auto result = PS5Pkg::ExtractSceSysItem(info, items[i], sce_sys);
            if (result.ok) {
                outcome.written++;
                outcome.decrypted += result.decrypted ? 1 : 0;
            } else {
                outcome.problems << QStringLiteral("%1: %2").arg(S(items[i].path),
                                                                 S(result.problem));
            }
        }
        progress.setValue(static_cast<int>(items.size()));
        wrote_any |= outcome.written > 0;
        outcomes.push_back(std::move(outcome));
    }

    // Summary.
    QStringList lines;
    for (const auto& o : outcomes) {
        QString line = QStringLiteral("<b>%1</b> (%2)<br>").arg(o.title.toHtmlEscaped(), o.kind);
        if (o.skipped) {
            line += o.skip_reason.isEmpty() ? tr("Skipped.") : o.skip_reason.toHtmlEscaped();
        } else {
            QString count;
            if (o.game_files > 0) {
                count = tr("%n game file(s) (%1)", nullptr, o.game_files)
                            .arg(QLocale().formattedDataSize(static_cast<qint64>(o.game_bytes))) +
                        QStringLiteral(", ");
            }
            count += tr("%n sce_sys file(s) written", nullptr, o.written);
            if (o.decrypted > 0) {
                count += tr(" (%n decrypted)", nullptr, o.decrypted);
            }
            if (!o.game_note.isEmpty()) {
                count += QStringLiteral("<br>") + o.game_note.toHtmlEscaped();
            }
            line += count + QStringLiteral("<br><span style=\"white-space:nowrap\">%1</span>")
                                .arg(o.destination.toHtmlEscaped());
            constexpr int kMaxShown = 8;
            for (int i = 0; i < o.problems.size() && i < kMaxShown; i++) {
                line += QStringLiteral("<br><span style=\"color:#d9534f\">%1</span>")
                            .arg(o.problems[i].toHtmlEscaped());
            }
            if (o.problems.size() > kMaxShown) {
                line += QStringLiteral("<br><span style=\"color:#d9534f\">%1</span>")
                            .arg(tr("... and %n more problem(s).", nullptr,
                                    static_cast<int>(o.problems.size() - kMaxShown)));
            }
        }
        lines << line;
    }
    QMessageBox box(parent);
    box.setWindowTitle(wrote_any ? tr("Installation Finished") : tr("Nothing Installed"));
    box.setIcon(wrote_any ? QMessageBox::Information : QMessageBox::Warning);
    box.setTextFormat(Qt::RichText);
    box.setText(lines.join(QStringLiteral("<br><br>")));
    box.exec();
    return wrote_any;
}
