// Ported from EVO Player (GPL-3.0), Surround Sound Studio (#106).
#include "surround/speaker_calibration.hpp"
#include "surround/spatial_field.hpp"
#include "platform/ps5/system.hpp"

#define evo_boot_log(...) hui::sys::log("[STUDIO] " __VA_ARGS__)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

/* libSceAudioIn has no stub in the payload SDK; the Makefile builds one
 * (stubs/libSceAudioIn.c). Signatures from EVO Player, DualSense capture
 * validated there. */
extern "C" {
int32_t sceAudioInOpen(int32_t user_id, int32_t type, int32_t index,
                       uint32_t grain, uint32_t frequency, uint32_t format);
int32_t sceAudioInInput(int32_t handle, void* destination);
int32_t sceAudioInGetSilentState(int32_t handle);
int32_t sceAudioInClose(int32_t handle);
int sceUserServiceGetInitialUser(int* user);
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace evo {

namespace {

constexpr int32_t kAudioInPurposeGeneral = 1;   /* recording / analysis */
constexpr int32_t kAudioInFormatS16Mono  = 0;

/* A speaker is "detected" when its matched-filter peak clears the same filter
 * run over room noise by this much. */
constexpr float kDetectSnrDb = 12.0f;
/* Output + Bluetooth-mic clocks differ by tens of ppm; anything this large is a
 * mis-detected reference chirp, not drift. */
constexpr float kMaxDriftPpm = 2000.0f;

struct Sweep { double f0, f1, dur; };

Sweep sweepFor(int ch) {
    /* The LFE gets a slow sub-bass sweep; everything else a short mid sweep
     * the DualSense mic hears flat enough to compare levels. Both stay below
     * the 8 kHz capture Nyquist. */
    return (ch == 3) ? Sweep{ 35.0, 160.0, 0.40 } : Sweep{ 300.0, 6000.0, 0.15 };
}

/* Exponential sine sweep with a 5 ms raised-cosine fade at each end. The same
 * analytic function builds the output (48 kHz) and the matched filter (16 kHz),
 * so they agree sample-for-sample in time. */
double sweepSample(const Sweep& s, double t) {
    if (t < 0.0 || t >= s.dur) return 0.0;
    const double k = std::log(s.f1 / s.f0);
    const double phase = 2.0 * M_PI * s.f0 * s.dur / k * (std::exp(t / s.dur * k) - 1.0);
    const double fade = 0.005;
    double w = 1.0;
    if (t < fade)               w = 0.5 - 0.5 * std::cos(M_PI * t / fade);
    else if (t > s.dur - fade)  w = 0.5 - 0.5 * std::cos(M_PI * (s.dur - t) / fade);
    return std::sin(phase) * w;
}

std::vector<float> sweepTemplate(const Sweep& s, int rate) {
    std::vector<float> t(static_cast<size_t>(s.dur * rate));
    for (size_t i = 0; i < t.size(); ++i)
        t[i] = static_cast<float>(sweepSample(s, static_cast<double>(i) / rate));
    return t;
}

float correlateAt(const std::vector<float>& x, size_t n, const std::vector<float>& t) {
    float acc = 0.0f;
    const float* xp = x.data() + n;
    for (size_t m = 0; m < t.size(); ++m) acc += xp[m] * t[m];
    return acc;
}

/* Largest |correlation| of `t` over x[begin, end). */
float peakOver(const std::vector<float>& x, size_t begin, size_t end,
               const std::vector<float>& t) {
    float peak = 0.0f;
    for (size_t n = begin; n + t.size() <= end && n + t.size() <= x.size(); ++n)
        peak = std::max(peak, std::fabs(correlateAt(x, n, t)));
    return peak;
}

struct Arrival { bool ok = false; double at = 0.0; float peak = 0.0f; };

/* Direct-path arrival of `t` in x[begin, end): the FIRST local maximum of
 * |correlation| reaching half the window's peak - later, taller peaks are
 * usually room reflections stacking up - refined to a fraction of a sample
 * with a parabola through its neighbours. */
Arrival findArrival(const std::vector<float>& x, long begin, long end,
                    const std::vector<float>& t) {
    Arrival a;
    const long len = static_cast<long>(t.size());
    begin = std::max(0L, begin);
    end = std::min(end, static_cast<long>(x.size()) - len);
    if (end - begin < 3) return a;

    std::vector<float> c(static_cast<size_t>(end - begin));
    float peak = 0.0f;
    for (long n = begin; n < end; ++n) {
        c[static_cast<size_t>(n - begin)] = std::fabs(correlateAt(x, static_cast<size_t>(n), t));
        peak = std::max(peak, c[static_cast<size_t>(n - begin)]);
    }
    if (peak <= 0.0f) return a;

    size_t i = 0;
    while (i < c.size() && c[i] < 0.5f * peak) ++i;
    while (i + 1 < c.size() && c[i + 1] >= c[i]) ++i;

    double frac = 0.0;
    if (i > 0 && i + 1 < c.size()) {
        const double l = c[i - 1], m = c[i], r = c[i + 1];
        const double den = l - 2.0 * m + r;
        if (den != 0.0) frac = std::max(-0.5, std::min(0.5, 0.5 * (l - r) / den));
    }
    a.ok = true;
    a.at = static_cast<double>(begin) + static_cast<double>(i) + frac;
    a.peak = c[i];
    return a;
}

float toDb(float v) { return v > 1e-9f ? 20.0f * std::log10(v) : -180.0f; }

} // namespace

SpeakerCalibrationService::~SpeakerCalibrationService() {
    cancel();
}

bool SpeakerCalibrationService::start(ISurroundTestService* output, bool is51) {
    if (!output) return false;
    cancel();

    m_output = output;
    m_is51 = is51;
    m_orderCount = spatial::physicalOrder(is51, m_order);
    m_cancel.store(false);

    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_snap = Snapshot();
        m_snap.is51 = is51;
        m_snap.total = m_orderCount;
        for (int i = 0; i < m_orderCount; ++i) m_snap.results[m_order[i]].measured = true;
    }

    m_running.store(true);
    if (pthread_create(&m_thread, nullptr, ThreadEntry, this) != 0) {
        m_thread = 0;
        m_running.store(false);
        fail("COULD NOT START THE CALIBRATION THREAD");
        return false;
    }
    return true;
}

void SpeakerCalibrationService::cancel() {
    m_cancel.store(true);
    if (m_thread != 0) {
        pthread_join(m_thread, nullptr);
        m_thread = 0;
    }
    m_running.store(false);
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_snap.phase == Phase::MicCheck || m_snap.phase == Phase::Measuring ||
        m_snap.phase == Phase::Analyzing) {
        m_snap.phase = Phase::Idle;
        m_snap.channel = -1;
        std::snprintf(m_snap.message, sizeof(m_snap.message), "CALIBRATION CANCELLED");
    }
}

SpeakerCalibrationService::Snapshot SpeakerCalibrationService::snapshot() const {
    std::lock_guard<std::mutex> lock(m_lock);
    return m_snap;
}

bool SpeakerCalibrationService::buildProfile(evo_speaker_cal_t* out) const {
    if (!out) return false;
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_snap.phase != Phase::Complete) return false;
    std::memset(out, 0, sizeof(*out));
    out->enabled = 1;
    for (int ch = 0; ch < EVO_SPEAKER_CAL_CHANNELS; ++ch) {
        const ChannelResult& r = m_snap.results[ch];
        if (!r.detected) continue;
        out->gain_db[ch] = r.trimDb;
        out->delay_ms[ch] = r.delayMs;
    }
    return true;
}

void SpeakerCalibrationService::setPhase(Phase p, const char* msg) {
    std::lock_guard<std::mutex> lock(m_lock);
    m_snap.phase = p;
    if (msg) std::snprintf(m_snap.message, sizeof(m_snap.message), "%s", msg);
}

void SpeakerCalibrationService::fail(const char* msg) {
    evo_boot_log("calibration: FAILED - %s", msg);
    std::lock_guard<std::mutex> lock(m_lock);
    m_snap.phase = Phase::Error;
    m_snap.channel = -1;
    std::snprintf(m_snap.message, sizeof(m_snap.message), "%s", msg);
}

void* SpeakerCalibrationService::ThreadEntry(void* arg) {
    auto* self = static_cast<SpeakerCalibrationService*>(arg);
    self->run();
    self->m_running.store(false);
    return nullptr;
}

void SpeakerCalibrationService::run() {
    setPhase(Phase::MicCheck, "CHECKING THE DUALSENSE MICROPHONE");

    int g_ps5_user_id = 0xFF;
    sceUserServiceGetInitialUser(&g_ps5_user_id);
    const int32_t handle = sceAudioInOpen(g_ps5_user_id, kAudioInPurposeGeneral, 0,
                                          kCaptureGrain, kCaptureRate, kAudioInFormatS16Mono);
    evo_boot_log("calibration: sceAudioInOpen(user=0x%x) -> 0x%08x",
                 static_cast<unsigned>(g_ps5_user_id), static_cast<unsigned>(handle));
    if (handle < 0) {
        char msg[128];
        std::snprintf(msg, sizeof(msg), "MICROPHONE UNAVAILABLE (0x%08X) - IS A DUALSENSE CONNECTED?",
                      static_cast<unsigned>(handle));
        fail(msg);
        return;
    }

    struct Closer { int32_t h; ~Closer() { sceAudioInClose(h); } } closer{ handle };

    const int slot16 = static_cast<int>(kSlotSec * kCaptureRate);
    const int slots = m_orderCount + 1;              /* + the FRONT LEFT drift check */
    m_capture.clear();
    m_capture.reserve(static_cast<size_t>(kCaptureRate) * (slots + 3));

    int16_t block[kCaptureGrain];
    int silentBlocks = 0, blocks = 0;
    auto readBlock = [&]() -> bool {
        if (m_cancel.load()) return false;
        if (sceAudioInInput(handle, block) < 0) {
            fail("MICROPHONE STOPPED RESPONDING");
            return false;
        }
        ++blocks;
        if (sceAudioInGetSilentState(handle) != 0) ++silentBlocks;
        double sum = 0.0;
        for (int i = 0; i < kCaptureGrain; ++i) sum += static_cast<double>(block[i]) * block[i];
        const float rms = static_cast<float>(std::sqrt(sum / kCaptureGrain) / 32768.0);
        m_capture.insert(m_capture.end(), block, block + kCaptureGrain);
        std::lock_guard<std::mutex> lock(m_lock);
        /* 0..1 over -60..0 dBFS - a meter, not a measurement */
        m_snap.micLevel = std::max(0.0f, std::min(1.0f, (toDb(rms) + 60.0f) / 60.0f));
        return true;
    };

    /* ---- MIC CHECK: 0.8 s; skip the first 0.2 s of route settling. */
    while (m_capture.size() < static_cast<size_t>(kCaptureRate * 8 / 10))
        if (!readBlock()) return;
    m_noiseBegin = kCaptureRate / 5;
    m_noiseEnd = m_capture.size();

    bool anySignal = false;
    double noiseSum = 0.0;
    for (size_t i = m_noiseBegin; i < m_noiseEnd; ++i) {
        anySignal |= (m_capture[i] != 0);
        noiseSum += static_cast<double>(m_capture[i]) * m_capture[i];
    }
    const float noiseDb = toDb(static_cast<float>(
        std::sqrt(noiseSum / static_cast<double>(m_noiseEnd - m_noiseBegin)) / 32768.0));
    evo_boot_log("calibration: mic check noise=%.1f dBFS silent_blocks=%d/%d signal=%d",
                 noiseDb, silentBlocks, blocks, anySignal ? 1 : 0);
    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_snap.noiseDb = noiseDb;
    }
    if (!anySignal) {
        fail("MICROPHONE MUTED OR OFF - TURN OFF THE ORANGE MUTE LIGHT ON THE DUALSENSE");
        return;
    }
    if (noiseDb > -25.0f) {
        char msg[128];
        std::snprintf(msg, sizeof(msg), "ROOM TOO NOISY (%.0f dBFS) - QUIET THE ROOM AND REPEAT", noiseDb);
        fail(msg);
        return;
    }

    /* ---- MEASURING: one gap-free 8-channel stream, one sweep per 1 s slot. */
    int seq[EVO_SPEAKER_CAL_CHANNELS + 1];
    for (int i = 0; i < m_orderCount; ++i) seq[i] = m_order[i];
    seq[m_orderCount] = m_order[0];

    const size_t slot48 = static_cast<size_t>(kSlotSec * kOutputRate);
    const size_t frames48 = slot48 * slots + kOutputRate / 4;
    std::vector<int16_t> pcm(frames48 * EVO_SPEAKER_CAL_CHANNELS, 0);
    for (int k = 0; k < slots; ++k) {
        const Sweep s = sweepFor(seq[k]);
        const size_t n = static_cast<size_t>(s.dur * kOutputRate);
        for (size_t i = 0; i < n; ++i) {
            const double v = sweepSample(s, static_cast<double>(i) / kOutputRate) * 16000.0;
            pcm[(k * slot48 + i) * EVO_SPEAKER_CAL_CHANNELS + seq[k]] = static_cast<int16_t>(v);
        }
    }

    const size_t seqStart = m_capture.size();
    if (!m_output->playPcm(pcm.data(), frames48)) {
        fail("AUDIO OUTPUT UNAVAILABLE");
        return;
    }
    setPhase(Phase::Measuring, "MEASURING");

    /* Capture every slot plus 0.8 s for the output + mic latency. */
    const size_t seqEnd = seqStart + static_cast<size_t>(slot16) * slots + kCaptureRate * 8 / 10;
    while (m_capture.size() < seqEnd) {
        if (!readBlock()) return;
        const int idx = std::min(slots - 1,
            static_cast<int>((m_capture.size() - seqStart) / static_cast<size_t>(slot16)));
        std::lock_guard<std::mutex> lock(m_lock);
        m_snap.verifying = (idx == m_orderCount);
        m_snap.step = std::min(idx + 1, m_orderCount);
        m_snap.channel = seq[idx];
    }

    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_snap.channel = -1;
        m_snap.verifying = false;
        m_snap.micLevel = 0.0f;
        m_snap.phase = Phase::Analyzing;
        std::snprintf(m_snap.message, sizeof(m_snap.message), "CALCULATING LEVELS, DISTANCES AND DELAYS");
    }
    analyze(seqStart);
}

void SpeakerCalibrationService::analyze(size_t seqStart) {
    const int slot16 = static_cast<int>(kSlotSec * kCaptureRate);
    const int n = m_orderCount;

    std::vector<float> x(m_capture.size());
    for (size_t i = 0; i < x.size(); ++i) x[i] = m_capture[i] / 32768.0f;

    const std::vector<float> tMain = sweepTemplate(sweepFor(0), kCaptureRate);
    const std::vector<float> tLfe  = sweepTemplate(sweepFor(3), kCaptureRate);
    auto energy = [](const std::vector<float>& t) {
        float e = 0.0f;
        for (float v : t) e += v * v;
        return e;
    };
    const float noiseMain = peakOver(x, m_noiseBegin, m_noiseEnd, tMain);
    const float noiseLfe  = peakOver(x, m_noiseBegin, m_noiseEnd, tLfe);

    Arrival arr[EVO_SPEAKER_CAL_CHANNELS + 1];
    float snr[EVO_SPEAKER_CAL_CHANNELS + 1] = {0};
    for (int k = 0; k <= n && !m_cancel.load(); ++k) {
        const int ch = (k == n) ? m_order[0] : m_order[k];
        const std::vector<float>& t = (ch == 3) ? tLfe : tMain;
        long begin, end;
        if (k == 0) {
            /* The unknown output + mic latency: search the whole first slot. */
            begin = static_cast<long>(seqStart);
            end = begin + slot16 - static_cast<long>(t.size());
        } else {
            if (!arr[0].ok) break;
            const long centre = static_cast<long>(arr[0].at) + static_cast<long>(k) * slot16;
            begin = centre - kCaptureRate * 12 / 100;   /* +/- 120 ms: ~40 m of path */
            end = centre + kCaptureRate * 12 / 100;
        }
        arr[k] = findArrival(x, begin, end, t);
        const float noise = (ch == 3) ? noiseLfe : noiseMain;
        snr[k] = (arr[k].ok && noise > 0.0f) ? toDb(arr[k].peak / noise) : 0.0f;
        if (snr[k] < kDetectSnrDb) arr[k].ok = false;
        evo_boot_log("calibration: slot %d ch %d arrival=%.2f peak=%.4f snr=%.1f dB %s",
                     k, ch, arr[k].at, arr[k].peak, snr[k], arr[k].ok ? "ok" : "MISSED");
    }
    if (m_cancel.load()) return;

    if (!arr[0].ok) {
        fail("FRONT LEFT NOT HEARD - RAISE THE VOLUME AND KEEP THE CONTROLLER AT THE SEAT");
        return;
    }

    /* Clock drift between the output and the mic, from the two FRONT LEFT
     * sweeps n slots apart; spread linearly over the run. */
    double drift = 0.0;
    if (arr[n].ok) {
        drift = (arr[n].at - arr[0].at) / (static_cast<double>(n) * slot16) - 1.0;
        if (std::fabs(drift) * 1e6 > kMaxDriftPpm) drift = 0.0;
    }

    double rel[EVO_SPEAKER_CAL_CHANNELS] = {0};
    double minRel = 1e30, maxRel = -1e30;
    float mainsDb = 0.0f;
    int mains = 0, heard = 0;
    for (int k = 0; k < n; ++k) {
        if (!arr[k].ok) continue;
        rel[k] = arr[k].at - arr[0].at - static_cast<double>(k) * slot16 * (1.0 + drift);
        minRel = std::min(minRel, rel[k]);
        maxRel = std::max(maxRel, rel[k]);
        ++heard;
        if (m_order[k] != 3) {
            const std::vector<float>& t = tMain;
            mainsDb += toDb(arr[k].peak / energy(t));
            ++mains;
        }
    }
    if (mains == 0) {
        fail("NO SPEAKERS HEARD - CHECK THE VOLUME AND THE MICROPHONE");
        return;
    }
    const float refDb = mainsDb / mains;

    std::lock_guard<std::mutex> lock(m_lock);
    m_snap.driftPpm = static_cast<float>(drift * 1e6);
    for (int k = 0; k < n; ++k) {
        ChannelResult& r = m_snap.results[m_order[k]];
        r.measured = true;
        r.detected = arr[k].ok;
        r.snrDb = snr[k];
        if (!r.detected) continue;
        const std::vector<float>& t = (m_order[k] == 3) ? tLfe : tMain;
        r.levelDb = toDb(arr[k].peak / energy(t));
        /* The subwoofer's delay is trustworthy, its level is not: the
         * DualSense mic hears little below ~100 Hz, so the raw difference
         * pins at +12 dB (hardware, 2026-09-27). Leave its level alone. */
        r.trimDb = (m_order[k] == 3) ? 0.0f
                 : std::max(-EVO_SPEAKER_CAL_MAX_TRIM_DB,
                            std::min(EVO_SPEAKER_CAL_MAX_TRIM_DB, refDb - r.levelDb));
        r.pathM = static_cast<float>((rel[k] - minRel) / kCaptureRate * spatial::kSpeedOfSound);
        r.delayMs = std::min(EVO_SPEAKER_CAL_MAX_DELAY_MS,
                             static_cast<float>((maxRel - rel[k]) / kCaptureRate * 1000.0));
        evo_boot_log("calibration: %s trim=%+.1f dB path=+%.2f m delay=%.2f ms",
                     spatial::speakerLabel(m_is51, m_order[k]), r.trimDb, r.pathM, r.delayMs);
    }
    m_snap.phase = Phase::Complete;
    if (heard == n)
        std::snprintf(m_snap.message, sizeof(m_snap.message),
                      "CALIBRATION COMPLETE (DUALSENSE MIC @ SWEET SPOT)");
    else
        std::snprintf(m_snap.message, sizeof(m_snap.message),
                      "COMPLETE - %d OF %d SPEAKERS HEARD, THE REST ARE LEFT UNTRIMMED", heard, n);
    evo_boot_log("calibration: complete heard=%d/%d drift=%.0f ppm", heard, n, drift * 1e6);
}

} // namespace evo
