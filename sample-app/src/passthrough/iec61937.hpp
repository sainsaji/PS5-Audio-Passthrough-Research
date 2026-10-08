// Passthrough Lab - IEC 61937 packing for AC-3, E-AC-3 and DTS.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Compressed audio crosses HDMI disguised as 16-bit stereo PCM: each coded
// frame is wrapped in a "burst" (four header words, the payload as big-endian
// words, then zeros) that lasts exactly as long as the audio it carries. The
// receiver finds the header words, unwraps the payload and decodes it.
//
// This file is plain C++ with no PS5 calls, so it can be tested on a PC.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace pt
{

enum class Codec : std::uint8_t
{
    ac3,
    eac3,
    dts,
    aac, // ADTS (MPEG-2/4 AAC with a header on every frame)
    truehd,
    dtshd,
    pcm2,
    pcm6,
    unknown,
};

const char *codec_name(Codec codec);

// Guesses the codec from the first bytes of an elementary stream.
Codec detect(std::span<const std::uint8_t> data);

// What the console needs to know to carry one codec.
struct Carrier
{
    int mode = -1;            // sceAudioOutExOpen / ExConfigureOutput mode
    int grain_frames = 0;     // stereo frames per sceAudioOutOutput call
    int sample_rate = 0;      // the port's rate, 48 kHz or 192 kHz
};
Carrier carrier_for(Codec codec);

// One frame found in a stream.
struct Frame
{
    std::size_t offset = 0;
    std::size_t size = 0;
    int samples = 0;       // PCM samples per channel it decodes to (DTS, AC-3)
    int blocks = 0;        // E-AC-3: audio blocks (6 make one burst)
    bool dependent = false; // E-AC-3 dependent substream (7.1 extension)
    int bsmod = 0;          // AC-3 bitstream mode, goes into the burst header
};

// Size and kind of the frame at `at`, or size 0 if there is no valid frame.
Frame parse_frame(Codec codec, std::span<const std::uint8_t> data, std::size_t at);

// Splits a whole elementary stream into bursts, each exactly as long as the
// audio it holds, as little-endian S16 stereo samples ready for the port.
class Packer
{
  public:
    explicit Packer(Codec codec) : codec_(codec)
    {
    }

    // Appends the next burst built from `data` starting at *cursor to `out`
    // and advances *cursor. Returns false at the end of the stream or on a
    // frame it cannot carry (then *error says why).
    bool next_burst(std::span<const std::uint8_t> data, std::size_t *cursor,
                    std::vector<std::uint8_t> *out, const char **error);

  private:
    Codec codec_;
};

// Writes one burst: header words Pa Pb Pc Pd, the payload as big-endian
// words, zero padding to burst_bytes. Exposed for tests.
void write_burst(std::uint16_t data_type, std::uint16_t length_code,
                 std::span<const std::uint8_t> payload, std::size_t burst_bytes,
                 std::vector<std::uint8_t> *out);

} // namespace pt
