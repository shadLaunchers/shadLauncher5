// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListView>
#include <QMap>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

class RepositoryConfig {
public:
    struct RepositoryInfo {
        QString name;
        QString cheatsIndexUrl;
        QString cheatsBaseUrl;
        QString patchesApiUrl;
        bool supportsPatches = false;
        bool supportsCheats = false;
    };

    RepositoryConfig() {
        initializeRepositories();
    }

    static RepositoryConfig& instance() {
        static RepositoryConfig instance;
        return instance;
    }

    RepositoryInfo getRepository(const QString& name) const {
        return m_repositories.value(name);
    }

    QStringList availableRepositories() const {
        return m_repositories.keys();
    }

    QStringList repositoriesWithCheats() const {
        QStringList result;
        for (const auto& repo : m_repositories) {
            if (repo.supportsCheats) {
                result << repo.name;
            }
        }
        return result;
    }

    QStringList repositoriesWithPatches() const {
        QStringList result;
        for (const auto& repo : m_repositories) {
            if (repo.supportsPatches) {
                result << repo.name;
            }
        }
        return result;
    }

private:
    void initializeRepositories() {
        // EtaHen Repository
        RepositoryInfo etahen;
        etahen.name = "EtaHen";
        etahen.cheatsIndexUrl =
            "https://raw.githubusercontent.com/etaHEN/PS5_Cheats/refs/heads/main/json.txt";
        etahen.cheatsBaseUrl =
            "https://raw.githubusercontent.com/etaHEN/PS5_Cheat_Repository/main/json/";
        etahen.patchesApiUrl =
            "https://raw.githubusercontent.com/illusionyy/PS-Game-Patch/main/patches/xml_prospero/";
        etahen.supportsCheats = true;
        etahen.supportsPatches = true;
        m_repositories[etahen.name] = etahen;

        // Future repositories can be added here
        // Example:
        // RepositoryInfo anotherRepo;
        // anotherRepo.name = "CustomRepo";
        // anotherRepo.supportsCheats = false; // Only patches
        // anotherRepo.patchesApiUrl = "...";
        // m_repositories[anotherRepo.name] = anotherRepo;
    }

    QMap<QString, RepositoryInfo> m_repositories;
};