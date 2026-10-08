// Ported from EVO Player (GPL-3.0), Surround Sound Studio (#106).
#include "surround/surround_test_service.hpp"

extern "C" {
int sceAudioOutInit(void);
int sceAudioOutOpen(int userId, int type, int index, unsigned int len, unsigned int freq, unsigned int param);
int sceAudioOutClose(int handle);
int sceAudioOutOutput(int handle, const void *ptr);
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unistd.h>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace evo {

SurroundTestService::SurroundTestService() {
    for (auto& g : m_fieldTarget) g.store(0.0f);
}

SurroundTestService::~SurroundTestService() {
    stop();
}

bool SurroundTestService::start() {
    if (m_running.load()) {
        return true;
    }

    sceAudioOutInit();

    // Open 8-channel AudioOut port (PS5 S16_8CH, param=2)
    m_audioHandle = sceAudioOutOpen(0xFF, 0, 0, AudioGrain, SampleRate, 2 /* S16_8CH */);
    if (m_audioHandle < 1) {
        m_audioHandle = -1;
        return false;
    }

    m_running.store(true);
    if (pthread_create(&m_thread, nullptr, AudioThreadEntry, this) != 0) {
        m_running.store(false);
        sceAudioOutClose(m_audioHandle);
        m_audioHandle = -1;
        return false;
    }

    return true;
}

void SurroundTestService::stop() {
    if (m_running.load()) {
        m_running.store(false);
        m_active.store(false);
        m_currentChannel.store(-1);
        m_pcmPending.store(false);
        m_fieldOn.store(false);
        if (m_thread != 0) {
            pthread_join(m_thread, nullptr);
            m_thread = 0;
        }
    }

    if (m_audioHandle >= 1) {
        sceAudioOutClose(m_audioHandle);
        m_audioHandle = -1;
    }
    m_pcmPlaying.store(false);
}

bool SurroundTestService::startField() {
    if (!m_running.load() && !start()) return false;
    if (!m_fieldOn.load()) {
        m_pendingMask.store(0);
        m_currentChannel.store(-1);
        m_fieldOn.store(true);
    }
    return true;
}

void SurroundTestService::stopField() {
    m_fieldOn.store(false);
}

void SurroundTestService::setFieldGains(const float gains[8], float master) {
    for (int ch = 0; ch < ChannelCount; ++ch)
        m_fieldTarget[ch].store(gains ? gains[ch] * master : 0.0f);
}

void SurroundTestService::emitFieldBlock(int handle) {
    float mains[AudioGrain], bass[AudioGrain];
    m_music.render(mains, bass, AudioGrain);
    m_musicUsed = true;

    int16_t block[AudioGrain * ChannelCount];
    for (int ch = 0; ch < ChannelCount; ++ch) {
        const float from = m_fieldCur[ch];
        const float to = m_fieldTarget[ch].load();
        m_fieldCur[ch] = to;
        const float* src = (ch == 3) ? bass : mains;
        /* the sub always carries some bass; nearness only adds to it */
        const float base = (ch == 3) ? 0.45f : 0.0f;
        for (int i = 0; i < AudioGrain; ++i) {
            const float g = from + (to - from) * (static_cast<float>(i) / AudioGrain);
            float x = src[i] * (ch == 3 ? base + 0.55f * g : g);
            /* gentle soft clip instead of a hard wall */
            x = x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
            if (x > 1.0f) x = 1.0f;
            if (x < -1.0f) x = -1.0f;
            block[i * ChannelCount + ch] = static_cast<int16_t>(x * 24000.0f);
        }
    }
    sceAudioOutOutput(handle, block);
}

void SurroundTestService::triggerTone(bool is51Layout, int channelIndex) {
    m_fieldOn.store(false);
    m_is51Layout = is51Layout;
    m_currentChannel.store(channelIndex);

    if (!m_running.load()) {
        if (!start()) {
            return;
        }
    }

    int mask = 0;
    if (channelIndex >= 0 && channelIndex < 8) {
        mask = (1 << channelIndex);
    } else if (channelIndex == 8) {
        // All channels
        mask = is51Layout ? 0x3F : 0xFF;
    }

    m_pendingMask.store(mask);
    m_active.store(mask != 0);
}

bool SurroundTestService::playPcm(const int16_t* interleaved8, size_t frames) {
    if (!interleaved8 || frames == 0) return false;
    if (!m_running.load() && !start()) return false;
    {
        std::lock_guard<std::mutex> lock(m_pcmLock);
        m_pcm.assign(interleaved8, interleaved8 + frames * ChannelCount);
    }
    m_fieldOn.store(false);
    m_pendingMask.store(0);
    m_currentChannel.store(-1);
    m_pcmPlaying.store(true);
    m_pcmPending.store(true);
    return true;
}

void SurroundTestService::emitPcm(int handle) {
    std::vector<int16_t> pcm;
    {
        std::lock_guard<std::mutex> lock(m_pcmLock);
        pcm.swap(m_pcm);
    }
    std::vector<int16_t> block(AudioGrain * ChannelCount, 0);
    const size_t frames = pcm.size() / ChannelCount;
    for (size_t at = 0; at < frames && m_running.load(); at += AudioGrain) {
        const size_t n = (frames - at < static_cast<size_t>(AudioGrain))
                           ? frames - at : static_cast<size_t>(AudioGrain);
        std::fill(block.begin(), block.end(), 0);
        std::memcpy(block.data(), pcm.data() + at * ChannelCount,
                    n * ChannelCount * sizeof(int16_t));
        if (sceAudioOutOutput(handle, block.data()) < 0) break;
    }
    std::fill(block.begin(), block.end(), 0);
    sceAudioOutOutput(handle, block.data());
}

void* SurroundTestService::AudioThreadEntry(void* arg) {
    auto* self = static_cast<SurroundTestService*>(arg);
    self->AudioLoop();
    return nullptr;
}

void SurroundTestService::AudioLoop() {
    while (m_running.load()) {
        if (m_pcmPending.exchange(false)) {
            if (m_audioHandle >= 1) emitPcm(m_audioHandle);
            m_pcmPlaying.store(false);
            continue;
        }

        if (m_fieldOn.load()) {
            if (m_audioHandle >= 1) emitFieldBlock(m_audioHandle);   /* blocks ~10.7 ms */
            continue;
        }
        /* field off: fade in from silence next time, restart the tune */
        if (m_musicUsed) {
            for (float& g : m_fieldCur) g = 0.0f;
            m_music.reset();
            m_musicUsed = false;
        }

        int mask = m_pendingMask.exchange(0);
        if (mask == 0) {
            usleep(10000);
            continue;
        }

        if (m_audioHandle < 1) {
            continue;
        }

        double frequency = (mask & (1 << 3)) ? 60.0 : 440.0; // LFE uses 60Hz, others 440Hz
        int blocks = (SampleRate * 1200 / 1000) / AudioGrain; // ~1.2 seconds duration

        emitTone(m_audioHandle, mask, frequency, blocks, AudioGrain);
        /* a tone that cut this one short has already marked itself active */
        if (m_pendingMask.load() == 0) m_active.store(false);
    }
}

void SurroundTestService::emitTone(int handle, int channelMask, double frequencyHz, int blocks, int grain) {
    std::vector<int16_t> buffer(grain * ChannelCount, 0);
    double phase = 0.0;
    double phaseInc = 2.0 * M_PI * frequencyHz / static_cast<double>(SampleRate);
    int totalSamples = blocks * grain;
    int currentSample = 0;

    for (int b = 0; b < blocks && m_running.load(); ++b) {
        /* Anything newer - the next tone of a sequence, the field, a PCM
         * run - cuts this tone off with a one-block fade instead of queueing
         * behind it. (Queueing let a 0.9 s sequence fall behind 1.2 s tones
         * and silently drop steps.) */
        const bool cut = b > 0 && (m_pendingMask.load() != 0 || m_fieldOn.load() || m_pcmPending.load());
        for (int i = 0; i < grain; ++i, ++currentSample) {
            double envelope = cut ? 1.0 - static_cast<double>(i) / grain : 1.0;
            // 20ms attack ramp
            int attackSamples = SampleRate * 20 / 1000;
            if (currentSample < attackSamples) {
                envelope *= static_cast<double>(currentSample) / static_cast<double>(attackSamples);
            }
            // 50ms decay ramp
            int decayStart = totalSamples - (SampleRate * 50 / 1000);
            if (currentSample > decayStart) {
                envelope *= static_cast<double>(totalSamples - currentSample) / static_cast<double>(SampleRate * 50 / 1000);
            }

            int16_t sampleValue = static_cast<int16_t>(std::sin(phase) * envelope * 20000.0);
            phase += phaseInc;
            if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;

            for (int ch = 0; ch < ChannelCount; ++ch) {
                if (channelMask & (1 << ch)) {
                    buffer[i * ChannelCount + ch] = sampleValue;
                } else {
                    buffer[i * ChannelCount + ch] = 0;
                }
            }
        }

        if (sceAudioOutOutput(handle, buffer.data()) < 0) {
            return;
        }
        if (cut) return;
    }

    // Output silence
    std::fill(buffer.begin(), buffer.end(), 0);
    sceAudioOutOutput(handle, buffer.data());
}

} // namespace evo
