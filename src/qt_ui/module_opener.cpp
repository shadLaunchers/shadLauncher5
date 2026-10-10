// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "module_opener.h"

#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <vector>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QPointer>
#include <QtConcurrent>
#include "core/file_format/elf_info.h"
#include "core/file_sys/game_backend.h"
#include "elf_info_dialog.h"
#include "progress_dialog.h"

namespace {

struct LoadResult {
    std::shared_ptr<const std::vector<u8>> data;
    std::optional<Loader::ElfInfo::ParsedInfo> info;
};

} // namespace

void OpenModuleInElfInfo(QWidget* parent, const ModuleLocation& location, const QString& game_title,
                         std::filesystem::path game_root, std::filesystem::path sys_modules_dir,
                         std::string select_export_nid) {
    const QString dialog_title = QObject::tr("ELF Info");
    auto* progress = new ProgressDialog(
        dialog_title, QObject::tr("Reading and parsing %1...").arg(location.display_path),
        QString(), 0, 0, /*delete_on_close=*/true, parent);
    progress->show();
    QPointer<ProgressDialog> progress_guard(progress);
    QPointer<QWidget> parent_guard(parent);

    auto* watcher = new QFutureWatcher<LoadResult>(progress);
    QObject::connect(
        watcher, &QFutureWatcher<LoadResult>::finished, progress,
        [watcher, progress_guard, parent_guard, location, game_title, game_root, sys_modules_dir,
         select_export_nid, dialog_title]() {
            LoadResult result = watcher->result();
            QWidget* owner = parent_guard.data();
            if (progress_guard) {
                progress_guard->close();
            }
            if (!result.data) {
                QMessageBox::warning(owner, dialog_title,
                                     QObject::tr("Could not read %1.").arg(location.display_path));
                return;
            }
            if (!result.info) {
                QMessageBox::warning(
                    owner, dialog_title,
                    QObject::tr("%1 is too small to contain a valid header (%2 bytes).")
                        .arg(location.display_path)
                        .arg(result.data->size()));
                return;
            }
            const QString tag = game_title.isEmpty()
                                    ? location.display_path
                                    : game_title + QStringLiteral(" - ") + location.display_path;
            const QString stem = QString::fromStdString(
                std::filesystem::path(location.display_path.toStdString()).stem().string());
            auto* dialog = new ElfInfoDialog(tag, *result.info, stem + QStringLiteral("_info.txt"),
                                             owner, result.data, tag);
            dialog->SetGameContext(game_root, sys_modules_dir, game_title);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
            if (!select_export_nid.empty()) {
                dialog->SelectSymbol(select_export_nid, /*exported=*/true);
            }
        });

    watcher->setFuture(QtConcurrent::run([location]() {
        LoadResult r;
        std::optional<std::vector<u8>> bytes;
        if (!location.host_path.empty()) {
            std::ifstream f(location.host_path, std::ios::binary);
            if (f) {
                bytes.emplace(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            }
        } else {
            bytes = Core::FileSys::ReadGameFile(location.game_root, location.rel_path);
        }
        if (!bytes) {
            return r;
        }
        r.info = Loader::ElfInfo::Parse(*bytes);
        r.data = std::make_shared<const std::vector<u8>>(std::move(*bytes));
        return r;
    }));
}
