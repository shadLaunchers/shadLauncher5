// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "background_music_player.h"

BackgroundMusicPlayer::BackgroundMusicPlayer(QObject* parent) : QObject(parent) {
    m_mediaPlayer = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_mediaPlayer->setAudioOutput(m_audioOutput);
}

void BackgroundMusicPlayer::SetVolume(int volume) {
    float linearVolume = QAudio::convertVolume(volume / 100.0f, QAudio::LogarithmicVolumeScale,
                                               QAudio::LinearVolumeScale);
    m_audioOutput->setVolume(linearVolume);
}

void BackgroundMusicPlayer::PlayMusic(const QString& snd0path, bool loops) {
    if (snd0path.isEmpty()) {
        StopMusic();
        return;
    }
    const auto newMusic = QUrl::fromLocalFile(snd0path);
    if (m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState &&
        m_currentMusic == newMusic) {
        // already playing the correct music
        return;
    }

    if (loops) {
        m_mediaPlayer->setLoops(QMediaPlayer::Infinite);
    } else {
        m_mediaPlayer->setLoops(1);
    }

    m_currentMusic = newMusic;
    m_mediaPlayer->setSource(newMusic);
    m_mediaPlayer->play();
}

void BackgroundMusicPlayer::StopMusic() {
    m_mediaPlayer->stop();
    m_mediaPlayer->setSource(QUrl(""));
}
