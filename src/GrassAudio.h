#ifndef GRASS_AUDIO_H
#define GRASS_AUDIO_H

#include <string>
#include <cstdint>

// =============================================================================
//  GrassGreen 4.0 — High-Performance Standard Audio Engine (!audio!)
//  Поддержка низкоуровневого синтеза звука с нуля (PCM 44.1kHz) и файлов
// =============================================================================

namespace GrassAudio {
    // ── Низкоуровневый процедурный синтезатор звука (с нуля, без внешних файлов) ──
    void tone(double freq_hz, int duration_ms, double volume = 80.0);
    void beep(double freq_hz = 750.0, int duration_ms = 300);
    void synth_play(const std::string& waveform, double freq_hz, int duration_ms, double volume = 80.0);

    // ── Высокоуровневое воспроизведение аудиофайлов (.wav, .mp3, .ogg, .mid) ──
    bool sound_play(const std::string& filepath, bool loop = false);
    void sound_stop();
    void sound_pause();
    void sound_resume();
    void sound_volume(int volume_percent); // 0 .. 100
    bool sound_is_playing();

    // Очистка ресурсов аудио
    void cleanup();
}

#endif // GRASS_AUDIO_H
