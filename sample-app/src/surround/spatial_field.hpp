// Ported from EVO Player (GPL-3.0), Surround Sound Studio (#106).
#ifndef EVO_AUDIO_SPATIAL_FIELD_HPP
#define EVO_AUDIO_SPATIAL_FIELD_HPP

/*
 * SpatialField - the Surround Sound Studio's room model (#106), kept apart
 * from the screen so the same maths can later drive real multichannel DSP:
 *
 *     source (x, y, z) -> speaker distance / direction -> gain + delay matrix
 *                      -> multichannel PCM
 *
 * Today the gain matrix feeds the soundstage's proximity glow, the per-speaker
 * VU meters and the SPEAKER LEVELS panel, and picks the channel a test tone
 * plays on. Nothing here touches audio or UI.
 *
 * Coordinates are metres around the listener: +x to the right, +y towards the
 * REAR of the room (so it maps straight onto screen-down), +z up. Speakers sit
 * at ear height (z = 0). Channel indices are the AudioOut S16_8CH interleave,
 * hardware-verified in media/src/evo_audio_resample.c:
 *
 *     0 FL  1 FR  2 FC  3 LFE  4 BL  5 BR  6 SL  7 SR
 *
 * In a 5.1 layout channels 4/5 are the surrounds (drawn at the side-rear) and
 * 6/7 do not exist.
 */

#include <cmath>

namespace evo {
namespace spatial {

constexpr int   kChannels     = 8;
constexpr float kDpPerMeter   = 100.0f;   /* soundstage scale, dp per metre */
constexpr float kSpeedOfSound = 343.0f;   /* m/s at ~20 C                   */
constexpr float kMaxRadiusM   = 3.4f;     /* how far the orb may roam       */
constexpr float kMaxHeightM   = 1.5f;     /* +/- elevation range            */

/* Test-tone peak: SurroundTestService writes sin() * 20000 into S16. */
constexpr float kToneDbfs = -4.3f;        /* 20*log10(20000/32767)          */

struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

inline bool present(bool is51, int ch) {
    return ch >= 0 && ch < kChannels && !(is51 && ch >= 6);
}

inline Vec3 speakerPosition(bool is51, int ch) {
    static const float k71[kChannels][2] = {
        { -2.3f, -2.0f },   /* FL  */
        {  2.3f, -2.0f },   /* FR  */
        {  0.0f, -2.5f },   /* FC  */
        {  0.0f,  2.6f },   /* LFE */
        { -2.2f,  2.1f },   /* BL  - surround back */
        {  2.2f,  2.1f },   /* BR  */
        { -3.3f,  0.1f },   /* SL  - surround side */
        {  3.3f,  0.1f },   /* SR  */
    };
    Vec3 p;
    if (ch < 0 || ch >= kChannels) return p;
    p.x = k71[ch][0];
    p.y = k71[ch][1];
    if (is51 && (ch == 4 || ch == 5)) {       /* 5.1 surrounds: side-rear */
        p.x = (ch == 4) ? -3.0f : 3.0f;
        p.y = 1.1f;
    }
    return p;
}

inline const char* speakerLabel(bool is51, int ch) {
    static const char* k71[kChannels] = { "FL", "FR", "C", "LFE", "SBL", "SBR", "SL", "SR" };
    static const char* k51[kChannels] = { "FL", "FR", "C", "LFE", "SL",  "SR",  "",   ""   };
    return (ch < 0 || ch >= kChannels) ? "" : (is51 ? k51[ch] : k71[ch]);
}

inline const char* speakerName(bool is51, int ch) {
    static const char* k71[kChannels] = {
        "FRONT LEFT", "FRONT RIGHT", "CENTER", "SUBWOOFER",
        "SURROUND BACK LEFT", "SURROUND BACK RIGHT", "SURROUND LEFT", "SURROUND RIGHT" };
    static const char* k51[kChannels] = {
        "FRONT LEFT", "FRONT RIGHT", "CENTER", "SUBWOOFER",
        "SURROUND LEFT", "SURROUND RIGHT", "", "" };
    return (ch < 0 || ch >= kChannels) ? "" : (is51 ? k51[ch] : k71[ch]);
}

/* Physical measuring / display order: FL C FR SL SR SBL SBR LFE. */
inline int physicalOrder(bool is51, int* out) {
    static const int k71[] = { 0, 2, 1, 6, 7, 4, 5, 3 };
    static const int k51[] = { 0, 2, 1, 4, 5, 3 };
    const int n = is51 ? 6 : 8;
    for (int i = 0; i < n; ++i) out[i] = is51 ? k51[i] : k71[i];
    return n;
}

inline float distance(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/* Clockwise from straight ahead, 0..360. */
inline float azimuthDeg(const Vec3& s) {
    float deg = std::atan2(s.x, -s.y) * 57.2957795f;
    return deg < 0.0f ? deg + 360.0f : deg;
}

inline float elevationDeg(const Vec3& s) {
    return std::atan2(s.z, std::sqrt(s.x * s.x + s.y * s.y)) * 57.2957795f;
}

inline float rangeM(const Vec3& s) {
    return std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
}

/*
 * Distance-based amplitude panning (DBAP, Lossius et al. 2009): each present
 * speaker is weighted by 1 / d^a with a 6 dB-per-doubling rolloff (a = 1) and
 * a small spatial blur so a source sitting on a speaker stays finite, then the
 * set is normalised to constant power (sum of g^2 = 1). The closest speaker
 * dominates while its neighbours fade in smoothly - never a binary switch.
 * Returns the index of the loudest speaker.
 */
inline int panGains(const Vec3& src, bool is51, float gains[kChannels]) {
    constexpr float kBlur = 0.35f;
    float sumSq = 0.0f;
    for (int ch = 0; ch < kChannels; ++ch) {
        gains[ch] = 0.0f;
        if (!present(is51, ch)) continue;
        const float d = distance(src, speakerPosition(is51, ch));
        const float w = 1.0f / std::sqrt(d * d + kBlur * kBlur);
        gains[ch] = w;
        sumSq += w * w;
    }
    int loudest = -1;
    const float norm = sumSq > 0.0f ? 1.0f / std::sqrt(sumSq) : 0.0f;
    for (int ch = 0; ch < kChannels; ++ch) {
        gains[ch] *= norm;
        if (gains[ch] > 0.0f && (loudest < 0 || gains[ch] > gains[loudest])) loudest = ch;
    }
    return loudest;
}

/* Time of flight from the source to one speaker - the delay half of the
 * matrix, for the future renderer. */
inline float arrivalDelayMs(const Vec3& src, bool is51, int ch) {
    return distance(src, speakerPosition(is51, ch)) / kSpeedOfSound * 1000.0f;
}

} // namespace spatial
} // namespace evo

#endif // EVO_AUDIO_SPATIAL_FIELD_HPP
