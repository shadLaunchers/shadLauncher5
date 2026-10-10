// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QWidget;
class QNetworkReply;

class NidCatalogEvents : public QObject {
    Q_OBJECT
public:
    static NidCatalogEvents* Instance();

signals:
    void CatalogChanged();

private:
    using QObject::QObject;
};

class NidCatalogSync : public QObject {
    Q_OBJECT
public:
    explicit NidCatalogSync(QObject* parent = nullptr);

    void Start();
    void Cancel();
    static void LoadSyncedCatalogAsync();
    static void RunWithProgress(QWidget* parent);
    static QString StatusText();

signals:
    void Progress(int done_rows, int total_rows);
    void Finished(bool ok, const QString& message);

private:
    void RequestTotal();
    void RequestPage();
    void OnPageFinished(QNetworkReply* reply);
    void Complete();
    void Fail(const QString& message);

    QNetworkAccessManager* m_net = nullptr;
    QPointer<QNetworkReply> m_reply;
    QJsonArray m_rows;
    int m_total = 0;
    int m_attempts = 0;
    bool m_cancelled = false;
    bool m_running = false;
};
