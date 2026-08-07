// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QObject>

class BackgroundMusicPlayer : public QObject {
    Q_OBJECT

public:
    static BackgroundMusicPlayer& getInstance() {
        static BackgroundMusicPlayer instance;
        return instance;
    }

    void SetVolume(int volume);
    void PlayMusic(const QString& snd0path, bool loops = true);
    void StopMusic();

private:
    BackgroundMusicPlayer(QObject* parent = nullptr);

    QMediaPlayer* m_mediaPlayer;
    QAudioOutput* m_audioOutput;
    QUrl m_currentMusic;
};
