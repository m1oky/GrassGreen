// =============================================================================
//  GrassGreen 4.0 — High-Performance Standard Audio Engine (!audio!)
//  Реализация процедурного синтеза волн и нативного воспроизведения WinMM / POSIX
// =============================================================================

#include "GrassAudio.h"
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <thread>
#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#else
#include <unistd.h>
#include <cstdlib>
#endif

namespace GrassAudio {

#if defined(_WIN32)
    static std::wstring utf8_to_wstr(const std::string& str) {
        if (str.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
        if (len <= 0) return L"";
        std::wstring wstr(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], len);
        if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back();
        return wstr;
    }
#endif

    void beep(double freq_hz, int duration_ms) {
#if defined(_WIN32)
        DWORD freq = (DWORD)std::max(37.0, std::min(32767.0, freq_hz));
        DWORD dur = (DWORD)std::max(1, duration_ms);
        Beep(freq, dur);
#else
        std::cout << "\a" << std::flush;
        if (duration_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
        }
#endif
    }

    void synth_play(const std::string& waveform, double freq_hz, int duration_ms, double volume) {
        if (duration_ms <= 0 || freq_hz <= 0.0) return;

        const int sample_rate = 44100;
        int num_samples = (int)((sample_rate * (long long)duration_ms) / 1000);
        if (num_samples <= 0) return;

        std::vector<int16_t> samples(num_samples);
        double amp = 32760.0 * (std::max(0.0, std::min(100.0, volume)) / 100.0);
        const double two_pi = 6.28318530717958647692;

        std::string wf = waveform;
        std::transform(wf.begin(), wf.end(), wf.begin(), ::tolower);

        int fade_samples = (std::min)(num_samples / 4, sample_rate * 5 / 1000); // 5 мс сглаживание клика

        for (int i = 0; i < num_samples; ++i) {
            double t = (double)i / (double)sample_rate;
            double phase = fmod(t * freq_hz, 1.0); // 0.0 .. 1.0
            double sample = 0.0;

            if (wf == "square") {
                sample = (phase < 0.5) ? 1.0 : -1.0;
            } else if (wf == "sawtooth" || wf == "saw") {
                sample = 2.0 * phase - 1.0;
            } else if (wf == "triangle") {
                sample = (phase < 0.5) ? (4.0 * phase - 1.0) : (3.0 - 4.0 * phase);
            } else if (wf == "noise") {
                sample = ((double)rand() / (double)RAND_MAX) * 2.0 - 1.0;
            } else { // "sine" по умолчанию
                sample = sin(phase * two_pi);
            }

            // Плавное нарастание и затухание для исключения щелчков
            double env = 1.0;
            if (i < fade_samples) {
                env = (double)i / (double)fade_samples;
            } else if (i > num_samples - fade_samples) {
                env = (double)(num_samples - i) / (double)fade_samples;
            }

            samples[i] = (int16_t)(sample * amp * env);
        }

#if defined(_WIN32)
        WAVEFORMATEX wfx = {};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = 1;
        wfx.nSamplesPerSec = sample_rate;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

        HWAVEOUT hWaveOut = NULL;
        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
            WAVEHDR hdr = {};
            hdr.lpData = (LPSTR)samples.data();
            hdr.dwBufferLength = (DWORD)(samples.size() * sizeof(int16_t));
            waveOutPrepareHeader(hWaveOut, &hdr, sizeof(WAVEHDR));
            waveOutWrite(hWaveOut, &hdr, sizeof(WAVEHDR));

            while (!(hdr.dwFlags & WHDR_DONE)) {
                Sleep(2);
            }

            waveOutUnprepareHeader(hWaveOut, &hdr, sizeof(WAVEHDR));
            waveOutClose(hWaveOut);
        } else {
            beep(freq_hz, duration_ms);
        }
#else
        beep(freq_hz, duration_ms);
#endif
    }

    void tone(double freq_hz, int duration_ms, double volume) {
        synth_play("sine", freq_hz, duration_ms, volume);
    }

    void sound_stop() {
#if defined(_WIN32)
        mciSendStringW(L"stop grass_audio", NULL, 0, NULL);
        mciSendStringW(L"close grass_audio", NULL, 0, NULL);
        PlaySoundW(NULL, NULL, 0);
#endif
    }

    bool sound_play(const std::string& filepath, bool loop) {
        sound_stop();
#if defined(_WIN32)
        std::wstring wpath = utf8_to_wstr(filepath);

        std::wstring openCmd = L"open \"" + wpath + L"\" alias grass_audio";
        MCIERROR err = mciSendStringW(openCmd.c_str(), NULL, 0, NULL);
        if (err != 0) {
            DWORD flags = SND_FILENAME | SND_ASYNC | (loop ? SND_LOOP : 0);
            if (PlaySoundW(wpath.c_str(), NULL, flags)) {
                return true;
            }
            return false;
        }

        std::wstring playCmd = loop ? L"play grass_audio repeat" : L"play grass_audio";
        return (mciSendStringW(playCmd.c_str(), NULL, 0, NULL) == 0);
#else
        std::string cmd = (loop ? "aplay -q \"" : "aplay -q \"") + filepath + "\" >/dev/null 2>&1 &";
        int ret = system(cmd.c_str());
        return (ret == 0);
#endif
    }

    void sound_pause() {
#if defined(_WIN32)
        mciSendStringW(L"pause grass_audio", NULL, 0, NULL);
#endif
    }

    void sound_resume() {
#if defined(_WIN32)
        mciSendStringW(L"resume grass_audio", NULL, 0, NULL);
#endif
    }

    void sound_volume(int volume_percent) {
#if defined(_WIN32)
        int vol = (std::max)(0, (std::min)(100, volume_percent)) * 10;
        std::wstring cmd = L"setaudio grass_audio volume to " + std::to_wstring(vol);
        mciSendStringW(cmd.c_str(), NULL, 0, NULL);
#endif
    }

    bool sound_is_playing() {
#if defined(_WIN32)
        wchar_t status[128] = {0};
        mciSendStringW(L"status grass_audio mode", status, 128, NULL);
        return (wcscmp(status, L"playing") == 0);
#else
        return false;
#endif
    }

    void cleanup() {
        sound_stop();
    }
}
