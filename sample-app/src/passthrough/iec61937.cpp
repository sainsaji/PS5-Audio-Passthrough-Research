// Passthrough Lab - IEC 61937 packing for AC-3, E-AC-3 and DTS.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "passthrough/iec61937.hpp"

namespace pt
{

namespace
{

constexpr std::uint16_t kPa = 0xF872;
constexpr std::uint16_t kPb = 0x4E1F;
// IEC 61937 data types
constexpr std::uint16_t kTypeAc3 = 0x01;
constexpr std::uint16_t kTypeDts1 = 0x0B; // 512 samples per frame
constexpr std::uint16_t kTypeDts2 = 0x0C; // 1024
constexpr std::uint16_t kTypeDts3 = 0x0D; // 2048
constexpr std::uint16_t kTypeDtshd = 0x11; // DTS-HD (Type IV)
constexpr std::uint16_t kTypeEac3 = 0x15;
constexpr std::uint16_t kTypeTruehd = 0x16; // Dolby TrueHD / MAT
constexpr std::uint16_t kTypeAac = 0x07; // MPEG-2 AAC, 1024 samples per frame

constexpr std::size_t kMatFrameSize = 61424;
constexpr std::size_t kMatBurstBytes = 61440;

constexpr std::uint8_t kMatStartCode[20] = {
    0x07, 0x9E, 0x00, 0x03, 0x84, 0x01, 0x01, 0x01, 0x80, 0x00,
    0x56, 0xA5, 0x3B, 0xF4, 0x81, 0x83, 0x49, 0x80, 0x77, 0xE0,
};
constexpr std::uint8_t kMatMiddleCode[12] = {
    0xC3, 0xC1, 0x42, 0x49, 0x3B, 0xFA, 0x82, 0x83, 0x49, 0x80, 0x77, 0xE0,
};
constexpr std::uint8_t kMatEndCode[16] = {
    0xC3, 0xC2, 0xC0, 0xC4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x97, 0x11,
};

constexpr std::uint8_t kDtshdStartCode[10] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0xFE
};

// AC-3 frame size in 16-bit words, by frmsizecod (0..37) and fscod (48, 44.1, 32 kHz).
constexpr std::uint16_t kAc3Words[38][3] = {
    {64, 69, 96},       {64, 70, 96},       {80, 87, 120},      {80, 88, 120},
    {96, 104, 144},     {96, 105, 144},     {112, 121, 168},    {112, 122, 168},
    {128, 139, 192},    {128, 140, 192},    {160, 174, 240},    {160, 175, 240},
    {192, 208, 288},    {192, 209, 288},    {224, 243, 336},    {224, 244, 336},
    {256, 278, 384},    {256, 279, 384},    {320, 348, 480},    {320, 349, 480},
    {384, 417, 576},    {384, 418, 576},    {448, 487, 672},    {448, 488, 672},
    {512, 557, 768},    {512, 558, 768},    {640, 696, 960},    {640, 697, 960},
    {768, 835, 1152},   {768, 836, 1152},   {896, 975, 1344},   {896, 976, 1344},
    {1024, 1114, 1536}, {1024, 1115, 1536}, {1152, 1253, 1728}, {1152, 1254, 1728},
    {1280, 1393, 1920}, {1280, 1394, 1920},
};

constexpr int kEac3Blocks[4] = {1, 2, 3, 6};

bool is_ac3_sync(std::span<const std::uint8_t> d, std::size_t at)
{
    return at + 6 <= d.size() && d[at] == 0x0B && d[at + 1] == 0x77;
}

bool is_dts_sync(std::span<const std::uint8_t> d, std::size_t at)
{
    return at + 10 <= d.size() && d[at] == 0x7F && d[at + 1] == 0xFE && d[at + 2] == 0x80 &&
           d[at + 3] == 0x01;
}

bool is_truehd_sync(std::span<const std::uint8_t> d, std::size_t at)
{
    if (at + 8 <= d.size() && d[at + 4] == 0xF8 && d[at + 5] == 0x72 &&
        d[at + 6] == 0x6F && (d[at + 7] == 0xBA || d[at + 7] == 0xBB))
        return true;
    if (at + 6 <= d.size() && d[at] == 0x72 && d[at + 1] == 0xF8 &&
        d[at + 2] == 0x1F && d[at + 3] == 0x4E && (d[at + 4] & 0xFF) == 0x16)
        return true;
    return false;
}

bool is_dtshd_sync(std::span<const std::uint8_t> d, std::size_t at)
{
    if (at + 6 <= d.size() && d[at] == 0x72 && d[at + 1] == 0xF8 &&
        d[at + 2] == 0x1F && d[at + 3] == 0x4E && (d[at + 4] & 0xFF) == 0x11)
        return true;
    if (at + 4 <= d.size() && d[at] == 0x64 && d[at + 1] == 0x58 &&
        d[at + 2] == 0x20 && d[at + 3] == 0x25)
        return true;
    return false;
}

bool is_adts_sync(std::span<const std::uint8_t> d, std::size_t at)
{
    return at + 7 <= d.size() && d[at] == 0xFF && (d[at + 1] & 0xF6) == 0xF0;
}

bool is_wav_sync(std::span<const std::uint8_t> d, std::size_t at)
{
    return at + 12 <= d.size() && d[at] == 'R' && d[at + 1] == 'I' &&
           d[at + 2] == 'F' && d[at + 3] == 'F' && d[at + 8] == 'W' &&
           d[at + 9] == 'A' && d[at + 10] == 'V' && d[at + 11] == 'E';
}

std::size_t find_wav_pcm_start(std::span<const std::uint8_t> d)
{
    if (is_wav_sync(d, 0))
    {
        std::size_t offset = 12;
        while (offset + 8 <= d.size())
        {
            if (d[offset] == 'd' && d[offset + 1] == 'a' &&
                d[offset + 2] == 't' && d[offset + 3] == 'a')
                return offset + 8;
            std::uint32_t chunk_size = static_cast<std::uint32_t>(d[offset + 4]) |
                                       (static_cast<std::uint32_t>(d[offset + 5]) << 8) |
                                       (static_cast<std::uint32_t>(d[offset + 6]) << 16) |
                                       (static_cast<std::uint32_t>(d[offset + 7]) << 24);
            offset += 8 + chunk_size + (chunk_size & 1);
        }
    }
    return 0;
}

} // namespace

const char *codec_name(Codec codec)
{
    switch (codec)
    {
    case Codec::ac3:
        return "Dolby Digital (AC-3)";
    case Codec::eac3:
        return "Dolby Digital Plus (E-AC-3)";
    case Codec::dts:
        return "DTS";
    case Codec::aac:
        return "AAC (ADTS)";
    case Codec::truehd:
        return "Dolby TrueHD";
    case Codec::dtshd:
        return "DTS-HD";
    case Codec::pcm2:
        return "Linear PCM 2.0";
    case Codec::pcm6:
        return "Linear PCM 5.1";
    case Codec::unknown:
        break;
    }
    return "Unknown";
}

Codec detect(std::span<const std::uint8_t> data)
{
    if (is_wav_sync(data, 0))
    {
        std::size_t offset = 12;
        while (offset + 8 <= data.size())
        {
            if (data[offset] == 'f' && data[offset + 1] == 'm' &&
                data[offset + 2] == 't' && data[offset + 3] == ' ')
            {
                if (offset + 14 <= data.size())
                {
                    int ch = data[offset + 10] | (data[offset + 11] << 8);
                    return (ch <= 2) ? Codec::pcm2 : Codec::pcm6;
                }
            }
            std::uint32_t chunk_size = static_cast<std::uint32_t>(data[offset + 4]) |
                                       (static_cast<std::uint32_t>(data[offset + 5]) << 8) |
                                       (static_cast<std::uint32_t>(data[offset + 6]) << 16) |
                                       (static_cast<std::uint32_t>(data[offset + 7]) << 24);
            offset += 8 + chunk_size + (chunk_size & 1);
        }
        return Codec::pcm2;
    }
    if (is_ac3_sync(data, 0))
        return (data[5] >> 3) > 10 ? Codec::eac3 : Codec::ac3; // bsid 16 is E-AC-3
    if (is_truehd_sync(data, 0))
        return Codec::truehd;
    if (is_dtshd_sync(data, 0))
        return Codec::dtshd;
    if (is_dts_sync(data, 0))
    {
        const int fsize = (((data[5] & 3) << 12) | (data[6] << 4) | (data[7] >> 4)) + 1;
        if (static_cast<std::size_t>(fsize + 4) <= data.size() &&
            is_dtshd_sync(data, static_cast<std::size_t>(fsize)))
            return Codec::dtshd;
        return Codec::dts;
    }
    if (is_adts_sync(data, 0))
        return Codec::aac;
    return Codec::unknown;
}

Carrier carrier_for(Codec codec)
{
    // From sceAudioOutExOpen's own tables in libSceAudioOut (FW 12.70).
    switch (codec)
    {
    case Codec::ac3:
        return {0, 256, 48000};
    case Codec::aac:
        return {1, 256, 48000};
    case Codec::dts:
        return {2, 256, 48000};
    case Codec::eac3:
        return {3, 1024, 192000};
    case Codec::truehd:
        return {5, 1024, 768000, true, 16}; // SysOpen mode 5: the 768 kHz 8-channel port; SysConfigureOutput mode 5 is MAT
    case Codec::pcm2:
        return {5, 256, 48000};
    case Codec::pcm6:
        return {6, 256, 48000};
    case Codec::dtshd:
        return {4, 1024, 192000};
    case Codec::unknown:
        break;
    }
    return {};
}

Frame parse_frame(Codec codec, std::span<const std::uint8_t> d, std::size_t at)
{
    Frame f;
    f.offset = at;
    if (codec == Codec::ac3 || codec == Codec::eac3)
    {
        if (!is_ac3_sync(d, at))
            return f;
        const int bsid = d[at + 5] >> 3;
        if (bsid <= 10)
        {
            const int fscod = d[at + 4] >> 6;
            const int frmsizecod = d[at + 4] & 0x3F;
            if (fscod > 2 || frmsizecod > 37)
                return f;
            f.size = static_cast<std::size_t>(kAc3Words[frmsizecod][fscod]) * 2;
            f.samples = 1536;
            f.blocks = 6;
            f.bsmod = d[at + 5] & 7;
        }
        else
        {
            const int strmtyp = d[at + 2] >> 6;
            const int frmsiz = ((d[at + 2] & 7) << 8) | d[at + 3];
            const int fscod = d[at + 4] >> 6;
            const int numblkscod = (d[at + 4] >> 4) & 3;
            f.size = static_cast<std::size_t>(frmsiz + 1) * 2;
            f.blocks = fscod == 3 ? 6 : kEac3Blocks[numblkscod];
            f.samples = f.blocks * 256;
            f.dependent = strmtyp == 1;
        }
    }
    else if (codec == Codec::dts || codec == Codec::dtshd)
    {
        if (at + 8 <= d.size() && d[at] == 0x72 && d[at + 1] == 0xF8 &&
            d[at + 2] == 0x1F && d[at + 3] == 0x4E && (d[at + 4] & 0xFF) == 0x11)
        {
            f.size = 2048 * 4;
            f.samples = 512;
            return f;
        }
        if (!is_dts_sync(d, at))
            return f;
        const int nblks = ((d[at + 4] & 1) << 6) | (d[at + 5] >> 2);
        const int fsize = (((d[at + 5] & 3) << 12) | (d[at + 6] << 4) | (d[at + 7] >> 4)) + 1;
        f.samples = (nblks + 1) * 32;
        f.size = static_cast<std::size_t>(fsize);
        if (codec == Codec::dtshd && at + f.size + 4 <= d.size() &&
            d[at + f.size] == 0x64 && d[at + f.size + 1] == 0x58 &&
            d[at + f.size + 2] == 0x20 && d[at + f.size + 3] == 0x25)
        {
            const std::size_t ext = at + f.size;
            if (ext + 8 <= d.size())
            {
                const std::size_t extsize = (((static_cast<std::size_t>(d[ext + 4] & 3) << 12) |
                                              (static_cast<std::size_t>(d[ext + 5]) << 4) |
                                              (static_cast<std::size_t>(d[ext + 6]) >> 4)) + 1);
                f.size += extsize;
            }
        }
    }
    else if (codec == Codec::aac)
    {
        if (!is_adts_sync(d, at))
            return f;
        const int length = ((d[at + 3] & 3) << 11) | (d[at + 4] << 3) | (d[at + 5] >> 5);
        f.size = static_cast<std::size_t>(length);
        f.samples = ((d[at + 6] & 3) + 1) * 1024; // raw data blocks in the frame
    }
    else if (codec == Codec::truehd)
    {
        if (at + 8 <= d.size() && d[at] == 0x72 && d[at + 1] == 0xF8 &&
            d[at + 2] == 0x1F && d[at + 3] == 0x4E && (d[at + 4] & 0xFF) == 0x16)
        {
            f.size = kMatBurstBytes;
            f.samples = 960;
            return f;
        }
        if (at + 2 <= d.size())
        {
            f.size = ((static_cast<std::size_t>(d[at] & 0x0F) << 8) | d[at + 1]) * 2;
            f.samples = 40;
        }
    }
    else if (codec == Codec::pcm2)
    {
        f.size = 4;
        f.samples = 1;
    }
    else if (codec == Codec::pcm6)
    {
        f.size = 12;
        f.samples = 1;
    }
    if (at + f.size > d.size())
        f.size = 0;
    return f;
}

void write_burst(std::uint16_t data_type, std::uint16_t length_code,
                 std::span<const std::uint8_t> payload, std::size_t burst_bytes,
                 std::vector<std::uint8_t> *out)
{
    const std::size_t start = out->size();
    out->resize(start + burst_bytes, 0);
    std::uint8_t *b = out->data() + start;
    const auto put = [&](std::size_t word, std::uint16_t v)
    {
        b[word * 2] = static_cast<std::uint8_t>(v & 0xFF); // samples are little-endian
        b[word * 2 + 1] = static_cast<std::uint8_t>(v >> 8);
    };
    put(0, kPa);
    put(1, kPb);
    put(2, data_type);
    put(3, length_code);
    // payload bytes a, b become the sample 0xaabb
    for (std::size_t i = 0; i < payload.size(); i += 2)
    {
        const std::uint8_t hi = payload[i];
        const std::uint8_t lo = i + 1 < payload.size() ? payload[i + 1] : 0;
        put(4 + i / 2, static_cast<std::uint16_t>((hi << 8) | lo));
    }
}

bool Packer::next_burst(std::span<const std::uint8_t> d, std::size_t *cursor,
                        std::vector<std::uint8_t> *out, const char **error)
{
    *error = nullptr;
    if (*cursor >= d.size())
        return false;
    const Frame first = parse_frame(codec_, d, *cursor);
    if (first.size == 0)
    {
        *error = "lost frame sync";
        return false;
    }

    if (codec_ == Codec::ac3)
    {
        constexpr std::size_t kBurst = 1536 * 4;
        if (first.size + 8 > kBurst)
        {
            *error = "AC-3 frame too large for one burst";
            return false;
        }
        write_burst(static_cast<std::uint16_t>(kTypeAc3 | (first.bsmod << 8)),
                    static_cast<std::uint16_t>(first.size * 8), d.subspan(first.offset, first.size),
                    kBurst, out);
        *cursor += first.size;
        return true;
    }

    if (codec_ == Codec::eac3)
    {
        // One burst carries 6 audio blocks (1536 samples) and lasts 4x as long
        // in port frames because the port runs at 192 kHz: 6144 stereo frames.
        constexpr std::size_t kBurst = 6144 * 4;
        std::size_t end = *cursor;
        int blocks = 0;
        for (;;)
        {
            const Frame f = parse_frame(codec_, d, end);
            if (f.size == 0)
                break;
            if (!f.dependent && blocks >= 6)
                break; // the next independent frame starts the next burst
            if (!f.dependent)
                blocks += f.blocks;
            end += f.size;
        }
        const std::size_t length = end - *cursor;
        if (length + 8 > kBurst)
        {
            *error = "E-AC-3 frames too large for one burst";
            return false;
        }
        write_burst(kTypeEac3, static_cast<std::uint16_t>(length), d.subspan(*cursor, length),
                    kBurst, out);
        *cursor = end;
        return true;
    }

    if (codec_ == Codec::aac)
    {
        // One ADTS frame, header included, per burst of 1024 samples.
        if (first.samples != 1024)
        {
            *error = "AAC frame is not 1024 samples (multi-block ADTS)";
            return false;
        }
        constexpr std::size_t kBurst = 1024 * 4;
        if (first.size + 8 > kBurst)
        {
            *error = "AAC frame too large for one burst";
            return false;
        }
        write_burst(kTypeAac, static_cast<std::uint16_t>(((first.size + 1) & ~std::size_t{1}) * 8), // Pd: bits, padded to a whole 16-bit word
                    d.subspan(first.offset, first.size), kBurst, out);
        *cursor += first.size;
        return true;
    }

    if (codec_ == Codec::dts)
    {
        std::uint16_t type = 0;
        switch (first.samples)
        {
        case 512:
            type = kTypeDts1;
            break;
        case 1024:
            type = kTypeDts2;
            break;
        case 2048:
            type = kTypeDts3;
            break;
        default:
            *error = "DTS frame length not 512, 1024 or 2048 samples";
            return false;
        }
        const std::size_t burst = static_cast<std::size_t>(first.samples) * 4;
        if (first.size + 8 > burst)
        {
            *error = "DTS frame too large for one burst (bitrate too high for type I-III)";
            return false;
        }
        write_burst(type, static_cast<std::uint16_t>(first.size * 8),
                    d.subspan(first.offset, first.size), burst, out);
        *cursor += first.size;
        return true;
    }

    if (codec_ == Codec::dtshd)
    {
        // If already an IEC 61937 DTS-HD burst
        if (*cursor + 8 <= d.size() && d[*cursor] == 0x72 && d[*cursor + 1] == 0xF8 &&
            d[*cursor + 2] == 0x1F && d[*cursor + 3] == 0x4E && (d[*cursor + 4] & 0xFF) == 0x11)
        {
            constexpr std::size_t kBurst = 2048 * 4;
            std::size_t burst = kBurst;
            if (*cursor + burst > d.size())
                burst = d.size() - *cursor;
            out->insert(out->end(), d.begin() + *cursor, d.begin() + *cursor + burst);
            *cursor += burst;
            return true;
        }

        constexpr std::size_t kBurst = 2048 * 4; // 8192 bytes
        const std::size_t pkt_size = first.size;
        const std::size_t payload_len = sizeof(kDtshdStartCode) + 2 + pkt_size;
        if (payload_len + 8 > kBurst)
        {
            *error = "DTS-HD frame too large for burst";
            return false;
        }
        std::vector<std::uint8_t> payload(payload_len, 0);
        std::memcpy(payload.data(), kDtshdStartCode, sizeof(kDtshdStartCode));
        payload[10] = static_cast<std::uint8_t>((pkt_size >> 8) & 0xFF);
        payload[11] = static_cast<std::uint8_t>(pkt_size & 0xFF);
        std::memcpy(payload.data() + 12, d.data() + first.offset, pkt_size);

        const std::uint16_t length_code = static_cast<std::uint16_t>(((payload_len + 8 + 15) & ~15) - 8);
        write_burst(static_cast<std::uint16_t>(kTypeDtshd | (0x02 << 8)), length_code, payload, kBurst, out);
        *cursor += pkt_size;
        return true;
    }

    if (codec_ == Codec::truehd)
    {
        // If already an IEC 61937 TrueHD burst
        if (*cursor + 8 <= d.size() && d[*cursor] == 0x72 && d[*cursor + 1] == 0xF8 &&
            d[*cursor + 2] == 0x1F && d[*cursor + 3] == 0x4E && (d[*cursor + 4] & 0xFF) == 0x16)
        {
            constexpr std::size_t kBurst = kMatBurstBytes;
            std::size_t burst = kBurst;
            if (*cursor + burst > d.size())
                burst = d.size() - *cursor;
            out->insert(out->end(), d.begin() + *cursor, d.begin() + *cursor + burst);
            *cursor += burst;
            return true;
        }

        std::vector<std::uint8_t> mat(kMatFrameSize, 0);
        std::memcpy(mat.data(), kMatStartCode, sizeof(kMatStartCode));
        std::memcpy(mat.data() + 30708, kMatMiddleCode, sizeof(kMatMiddleCode));
        std::memcpy(mat.data() + (kMatFrameSize - sizeof(kMatEndCode)), kMatEndCode, sizeof(kMatEndCode));

        std::size_t cur = *cursor;
        std::size_t pos = sizeof(kMatStartCode);
        int frames_packed = 0;
        while (cur < d.size() && frames_packed < 24)
        {
            if (cur + 2 > d.size())
                break;
            std::size_t au_size = ((static_cast<std::size_t>(d[cur] & 0x0F) << 8) | d[cur + 1]) * 2;
            if (au_size == 0 || cur + au_size > d.size())
                break;

            if (frames_packed >= 12 && pos < 30720)
                pos = 30720;

            std::size_t limit = (pos < 30708) ? 30708 : (kMatFrameSize - sizeof(kMatEndCode));
            if (pos + au_size > limit)
            {
                if (pos < 30708)
                {
                    pos = 30720;
                    limit = kMatFrameSize - sizeof(kMatEndCode);
                }
                if (pos + au_size > limit)
                    break;
            }

            std::memcpy(mat.data() + pos, d.data() + cur, au_size);
            pos += au_size;
            cur += au_size;
            ++frames_packed;
        }

        if (frames_packed == 0)
        {
            *error = "no valid TrueHD access units found";
            return false;
        }

        write_burst(kTypeTruehd, static_cast<std::uint16_t>(kMatFrameSize), mat, kMatBurstBytes, out);
        *cursor = cur;
        return true;
    }

    if (codec_ == Codec::pcm2)
    {
        if (*cursor == 0)
            *cursor = find_wav_pcm_start(d);
        if (*cursor >= d.size())
            return false;
        std::size_t chunk = std::min<std::size_t>(1024, d.size() - *cursor);
        chunk &= ~3u; // 4-byte stereo frame aligned
        if (chunk == 0)
            return false;
        out->insert(out->end(), d.begin() + *cursor, d.begin() + *cursor + chunk);
        *cursor += chunk;
        return true;
    }

    if (codec_ == Codec::pcm6)
    {
        if (*cursor == 0)
            *cursor = find_wav_pcm_start(d);
        if (*cursor >= d.size())
            return false;
        std::size_t frames = std::min<std::size_t>(256, (d.size() - *cursor) / 12);
        if (frames == 0)
            return false;
        std::size_t start_out = out->size();
        out->resize(start_out + frames * 16, 0);
        std::uint8_t *dst = out->data() + start_out;
        const std::uint8_t *src = d.data() + *cursor;
        for (std::size_t f = 0; f < frames; ++f)
            std::memcpy(dst + f * 16, src + f * 12, 12);
        *cursor += frames * 12;
        return true;
    }

    *error = "unsupported codec";
    return false;
}

} // namespace pt
