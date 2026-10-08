// Ported from EVO Player (GPL-3.0), Surround Sound Studio (#106).
#ifndef EVO_AUDIO_FIELD_MUSIC_HPP
#define EVO_AUDIO_FIELD_MUSIC_HPP

/*
 * FieldMusic - the Surround Studio's 3D SOUND FIELD source (#106): a soft
 * music-box arpeggio over a breathing pad, with a sub-bass line for the LFE.
 * A continuous, pleasant signal with clear note onsets - the onsets are what
 * let the ear place the source as it pans between speakers.
 *
 * Pure DSP, 48 kHz, no allocation. SurroundTestService pans the `mains`
 * output across the speakers with SpatialField's gain matrix and sends `bass`
 * to the LFE.
 *
 * I - vi - IV - V (C, Am, F, G), 2.4 s per chord, one arpeggio note every
 * 0.3 s, looping every 9.6 s.
 */

#include <cmath>

namespace evo {
namespace spatial {

class FieldMusic {
public:
    static constexpr int kRate = 48000;

    void reset() { *this = FieldMusic(); }

    /* Renders `frames` samples of each stream, roughly -1..1. */
    void render(float* mains, float* bass, int frames) {
        for (int n = 0; n < frames; ++n, ++m_sample) {
            const int chord = static_cast<int>((m_sample / kChordLen) % 4);
            const double inChord = static_cast<double>(m_sample % kChordLen) / kChordLen;

            if (m_sample % kStepLen == 0) {
                const int step = static_cast<int>((m_sample / kStepLen) % 8);
                static const int kPattern[8] = { 0, 1, 2, 3, 4, 3, 2, 1 };
                const int idx = kPattern[step];
                const int midi = (idx == 4) ? kChords[chord][0] + 24 : kChords[chord][idx] + 12;
                /* accent the downbeat, soften the rest */
                noteOn(midiHz(midi), step == 0 ? 0.42f : (step == 4 ? 0.34f : 0.26f));
            }

            float mel = 0.0f;
            for (Voice& v : m_voices) {
                if (v.amp <= 0.0f) continue;
                const double age = v.age / kRate;
                const double env = std::fmin(1.0, age / 0.004) * std::exp(-age / 0.9);
                if (env < 1e-4 && age > 0.1) { v.amp = 0.0f; continue; }
                const double p = v.phase;
                /* a mallet: the fundamental, plus overtones that die fast */
                const double s = std::sin(p) + 0.20 * std::sin(2.0 * p) * std::exp(-age / 0.12)
                               + 0.07 * std::sin(3.0 * p) * std::exp(-age / 0.06);
                mel += static_cast<float>(v.amp * env * s);
                v.phase += 2.0 * kPi * v.freq / kRate;
                if (v.phase > 2.0 * kPi) v.phase -= 2.0 * kPi;
                v.age += 1.0;
            }

            /* pad: the chord an octave down, each tone a slightly detuned
             * pair, swelling in and out across the chord */
            const double swell = 0.5 - 0.5 * std::cos(2.0 * kPi * inChord);
            double pad = 0.0;
            for (int t = 0; t < 3; ++t) {
                const double f = midiHz(kChords[chord][t]);
                pad += std::sin(m_padPhase[t][0]) + std::sin(m_padPhase[t][1]);
                m_padPhase[t][0] = std::fmod(m_padPhase[t][0] + 2.0 * kPi * f / kRate, 2.0 * kPi);
                m_padPhase[t][1] = std::fmod(m_padPhase[t][1] + 2.0 * kPi * f * 1.004 / kRate, 2.0 * kPi);
            }
            pad *= 0.035 * (0.35 + 0.65 * swell);

            /* sub: the chord root two octaves down */
            const double bf = midiHz(kChords[chord][0] - 24);
            const double b = std::sin(m_bassPhase) * (0.5 + 0.5 * swell);
            m_bassPhase = std::fmod(m_bassPhase + 2.0 * kPi * bf / kRate, 2.0 * kPi);

            mains[n] = mel * 0.8f + static_cast<float>(pad);
            bass[n] = static_cast<float>(0.45 * b);
        }
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr long long kStepLen = kRate * 3 / 10;   /* 0.3 s */
    static constexpr long long kChordLen = kStepLen * 8;    /* 2.4 s */
    static constexpr int kChords[4][4] = {
        { 60, 64, 67, 72 },   /* C  */
        { 57, 60, 64, 69 },   /* Am */
        { 53, 57, 60, 65 },   /* F  */
        { 55, 59, 62, 67 },   /* G  */
    };

    struct Voice { double phase = 0.0, freq = 0.0, age = 0.0; float amp = 0.0f; };

    static double midiHz(int m) { return 440.0 * std::pow(2.0, (m - 69) / 12.0); }

    void noteOn(double hz, float amp) {
        Voice& v = m_voices[m_next];
        m_next = (m_next + 1) % kVoices;
        v.phase = 0.0;
        v.freq = hz;
        v.age = 0.0;
        v.amp = amp;
    }

    static constexpr int kVoices = 6;
    Voice m_voices[kVoices];
    int m_next = 0;
    long long m_sample = 0;
    double m_padPhase[3][2] = {};
    double m_bassPhase = 0.0;
};

} // namespace spatial
} // namespace evo

#endif // EVO_AUDIO_FIELD_MUSIC_HPP
