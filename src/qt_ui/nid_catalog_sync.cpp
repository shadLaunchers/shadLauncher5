// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "nid_catalog_sync.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>
#include "common/path_util.h"
#include "core/file_format/nid_catalog.h"
#include "progress_dialog.h"

namespace {

constexpr char kCatalogUrl[] = "https://krvshlwmvzczpjvuizte.supabase.co/rest/v1/catalog_export"
                               "?select=nid,name,library,tag,source&order=nid";
constexpr char kPublishableKey[] = "sb_publishable_ClOIpIlMjajaStG_6RmBCA_O_dzcS9l";
constexpr int kPageSize = 1000;
constexpr int kMaxRetries = 3;

QNetworkRequest MakeRequest() {
    QNetworkRequest req{QUrl(QString::fromLatin1(kCatalogUrl))};
    req.setRawHeader("apikey", kPublishableKey);
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(60000);
    return req;
}

QString CsvField(QString v) {
    if (v.contains(QLatin1Char(',')) || v.contains(QLatin1Char('"')) ||
        v.contains(QLatin1Char('\n'))) {
        v.replace(QStringLiteral("\""), QStringLiteral("\"\""));
        v = QLatin1Char('"') + v + QLatin1Char('"');
    }
    return v;
}

QString CatalogDirQString() {
    QString dir;
    Common::FS::PathToQString(dir, Loader::ElfInfo::SyncedCatalogDir());
    return dir;
}

} // namespace

NidCatalogEvents* NidCatalogEvents::Instance() {
    static auto* instance = new NidCatalogEvents();
    return instance;
}

NidCatalogSync::NidCatalogSync(QObject* parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

void NidCatalogSync::LoadSyncedCatalogAsync() {
    auto* watcher = new QFutureWatcher<size_t>(NidCatalogEvents::Instance());
    QObject::connect(watcher, &QFutureWatcher<size_t>::finished, watcher, [watcher] {
        if (watcher->result() > 0) {
            NidCatalogEvents::Instance()->CatalogChanged(); // emit
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(
        [] { return Loader::ElfInfo::GetMutableDefaultNidCatalog().LoadSyncedCatalog(); }));
}

void NidCatalogSync::RunWithProgress(QWidget* parent) {
    static QPointer<NidCatalogSync> s_active;
    if (s_active) {
        return;
    }
    auto* sync = new NidCatalogSync(NidCatalogEvents::Instance());
    s_active = sync;

    auto* progress =
        new ProgressDialog(tr("Sync NID Catalog"), tr("Downloading the community NID catalog..."),
                           tr("Cancel"), 0, 0, /*delete_on_close=*/true, parent);
    progress->show();
    QPointer<ProgressDialog> guard(progress);
    QPointer<QWidget> parent_guard(parent);

    connect(progress, &QProgressDialog::canceled, sync, &NidCatalogSync::Cancel);
    connect(sync, &NidCatalogSync::Progress, sync, [guard](int done, int total) {
        if (!guard) {
            return;
        }
        if (total > 0) {
            guard->SetRange(0, total);
            guard->SetValue(std::min(done, total));
        }
        guard->setLabelText(tr("Downloading the community NID catalog... %1 entries").arg(done));
    });
    connect(sync, &NidCatalogSync::Finished, sync,
            [sync, guard, parent_guard](bool ok, const QString& message) {
                if (guard) {
                    guard->close();
                }
                sync->deleteLater();
                if (message.isEmpty()) {
                    return; // cancelled by the user
                }
                QWidget* box_parent = parent_guard.data();
                if (ok) {
                    QMessageBox::information(box_parent, tr("Sync NID Catalog"), message);
                } else {
                    QMessageBox::warning(box_parent, tr("Sync NID Catalog"), message);
                }
            });
    sync->Start();
}

QString NidCatalogSync::StatusText() {
    QFile meta(CatalogDirQString() + QStringLiteral("/metadata.json"));
    if (!meta.open(QIODevice::ReadOnly)) {
        return tr("The community NID catalog hasn't been downloaded yet.");
    }
    const QJsonObject o = QJsonDocument::fromJson(meta.readAll()).object();
    const QDateTime updated =
        QDateTime::fromString(o.value(QStringLiteral("updated")).toString(), Qt::ISODate);
    return tr("Last synced %1 - %2 entries.")
        .arg(updated.isValid() ? updated.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                               : tr("(unknown date)"))
        .arg(o.value(QStringLiteral("entries")).toInt());
}

void NidCatalogSync::Start() {
    if (m_running) {
        return;
    }
    m_running = true;
    m_cancelled = false;
    m_rows = QJsonArray();
    m_total = 0;
    m_attempts = 0;
    RequestTotal();
}

void NidCatalogSync::Cancel() {
    m_cancelled = true;
    if (m_reply) {
        m_reply->abort();
    }
}

void NidCatalogSync::RequestTotal() {
    // PostgREST reports the exact row count in Content-Range for a HEAD with
    // "Prefer: count=exact" - only used to drive the progress bar.
    QNetworkRequest req = MakeRequest();
    req.setRawHeader("Prefer", "count=exact");
    req.setRawHeader("Range", "0-0");
    m_reply = m_net->head(req);
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply.data()] {
        reply->deleteLater();
        if (m_cancelled) {
            Fail(QString()); // cancelled: no message
            return;
        }
        const QByteArray range = reply->rawHeader("Content-Range"); // e.g. "0-0/199274"
        const int slash = range.indexOf('/');
        if (slash >= 0) {
            m_total = range.mid(slash + 1).toInt();
        }
        emit Progress(0, m_total);
        RequestPage();
    });
}

void NidCatalogSync::RequestPage() {
    const int start = static_cast<int>(m_rows.size());
    QNetworkRequest req = MakeRequest();
    req.setRawHeader("Range",
                     QByteArray::number(start) + '-' + QByteArray::number(start + kPageSize - 1));
    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::finished, this,
            [this, reply = m_reply.data()] { OnPageFinished(reply); });
}

void NidCatalogSync::OnPageFinished(QNetworkReply* reply) {
    reply->deleteLater();
    if (m_cancelled) {
        Fail(QString()); // cancelled: no message
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        if ((status >= 500 || status == 0) && m_attempts < kMaxRetries) {
            const int delay_ms = 1000 << m_attempts;
            m_attempts++;
            QTimer::singleShot(delay_ms, this, [this] {
                if (m_cancelled) {
                    Fail(QString()); // cancelled: no message
                } else {
                    RequestPage();
                }
            });
            return;
        }
        Fail(tr("Download failed (HTTP %1): %2").arg(status).arg(reply->errorString()));
        return;
    }
    m_attempts = 0;

    QJsonParseError parse_error{};
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !doc.isArray()) {
        Fail(tr("The catalog server sent an unexpected response."));
        return;
    }
    const QJsonArray page = doc.array();
    for (const auto& row : page) {
        m_rows.append(row);
    }
    emit Progress(static_cast<int>(m_rows.size()), m_total);

    if (page.size() < kPageSize) {
        Complete();
    } else {
        RequestPage();
    }
}

void NidCatalogSync::Complete() {
    QString csv = QStringLiteral("nid,name,library,tag,source\n");
    for (const auto& value : m_rows) {
        const QJsonObject o = value.toObject();
        csv += CsvField(o.value(QStringLiteral("nid")).toString()) + QLatin1Char(',') +
               CsvField(o.value(QStringLiteral("name")).toString()) + QLatin1Char(',') +
               CsvField(o.value(QStringLiteral("library")).toString()) + QLatin1Char(',') +
               CsvField(o.value(QStringLiteral("tag")).toString()) + QLatin1Char(',') +
               CsvField(o.value(QStringLiteral("source")).toString()) + QLatin1Char('\n');
    }
    const QByteArray csv_bytes = csv.toUtf8();
    const QString sha256 = QString::fromLatin1(
        QCryptographicHash::hash(csv_bytes, QCryptographicHash::Sha256).toHex());
    const int entries = static_cast<int>(m_rows.size());
    m_rows = QJsonArray();

    const QString dir = CatalogDirQString();
    QDir().mkpath(dir);
    const QString meta_path = dir + QStringLiteral("/metadata.json");
    const QString csv_path = dir + QStringLiteral("/nids.csv");

    int previous_entries = 0;
    {
        QFile meta(meta_path);
        if (meta.open(QIODevice::ReadOnly)) {
            const QJsonObject old = QJsonDocument::fromJson(meta.readAll()).object();
            previous_entries = old.value(QStringLiteral("entries")).toInt();
            if (old.value(QStringLiteral("sha256")).toString() == sha256 &&
                QFile::exists(csv_path)) {
                m_running = false;
                emit Finished(
                    true, tr("The NID catalog is already up to date (%1 entries).").arg(entries));
                return;
            }
        }
    }

    QSaveFile csv_file(csv_path);
    if (!csv_file.open(QIODevice::WriteOnly) || csv_file.write(csv_bytes) != csv_bytes.size() ||
        !csv_file.commit()) {
        Fail(tr("Could not write %1").arg(csv_path));
        return;
    }
    QSaveFile meta_file(meta_path);
    const QJsonObject meta{
        {QStringLiteral("sha256"), sha256},
        {QStringLiteral("entries"), entries},
        {QStringLiteral("updated"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
    };
    if (meta_file.open(QIODevice::WriteOnly)) {
        meta_file.write(QJsonDocument(meta).toJson());
        meta_file.commit();
    }

    auto* watcher = new QFutureWatcher<size_t>(this);
    connect(watcher, &QFutureWatcher<size_t>::finished, this,
            [this, watcher, entries, previous_entries] {
                const size_t loaded = watcher->result();
                watcher->deleteLater();
                m_running = false;
                NidCatalogEvents::Instance()->CatalogChanged(); // emit
                QString diff;
                if (previous_entries > 0 && entries != previous_entries) {
                    diff =
                        entries > previous_entries
                            ? tr(" (+%1 since the last sync)").arg(entries - previous_entries)
                            : tr(" (%1 fewer than the last sync)").arg(previous_entries - entries);
                }
                emit Finished(true, tr("Downloaded the community NID catalog: %1 entries%2. "
                                       "%3 names are now available for symbol resolution.")
                                        .arg(entries)
                                        .arg(diff)
                                        .arg(loaded));
            });
    watcher->setFuture(QtConcurrent::run(
        [] { return Loader::ElfInfo::GetMutableDefaultNidCatalog().LoadSyncedCatalog(); }));
}

void NidCatalogSync::Fail(const QString& message) {
    m_running = false;
    m_rows = QJsonArray();
    emit Finished(false, message);
}
