// Passthrough Lab - HDMI bitstream output through libSceAudioOut.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/ps5/bitstream_out.hpp"

#include "platform/ps5/system.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <thread>

// Not in the SDK headers, but the SDK's libSceAudioOut stub exports them, so
// they link as ordinary imports. (A sandboxed app cannot look them up with
// sceKernelDlsym: the lookup is refused, so link them instead.)
extern "C"
{
    int sceAudioOutOpen(int user, int type, int index, unsigned int grain_frames,
                        unsigned int frequency, unsigned int format);
    int sceAudioOutClose(int handle);
    int sceAudioOutSysOpen(int user, int mode);   // MGkAS4ncQ90
    int sceAudioOutSysClose(int handle);          // pKY2S4K-6Mg
    int sceAudioOutSysConfigureOutput(int type, unsigned flags, int mode, int target,
                                      std::uint64_t opt); // ktdp5iauPQc
    int sceAudioOutOutput(int handle, const void *samples);
    int sceAudioOutExOpen(int user, int mode);                                        // 6X6dp+07h4U
    int sceAudioOutExClose(int handle);                                               // 0TfjSulCV2A
    int sceAudioOutExConfigureOutput(int zero, unsigned flags, int mode, int target,
                                     std::uint64_t opt);                              // VcE+gXSwFXI
    int sceAudioOutSysGetHdmiMonitorInfo(int type, void *out, unsigned size);         // Tf9-yOJwF-A
}

namespace hui::ps5
{

namespace
{

constexpr int kSystemUser = 0xFF;
constexpr int kTargetHdmi = 1;     // what citroncore passes
constexpr int kModeDefault = 0xFF; // every field "don't care": normal PCM output
constexpr unsigned kMonitorInfoSize = 0x180;

// A receiver needs time to follow each HDMI format change. Switching formats
// back to back left it stuck on the previous one (a PCM clip labelled "AAC",
// and AAC silent until the receiver was restarted), so every bitstream is
// bracketed by IEC 61937 null-data bursts, and a new switch waits out a
// settle time after the last reset.
constexpr int kLeadInMs = 400;      // null bursts after the switch, before the first frame
constexpr int kLeadOutMs = 500;     // null bursts after the last frame, before the port closes
constexpr int kAfterCloseMs = 250;  // between closing the port and resetting HDMI
constexpr int kAfterResetMs = 400;  // after the reset, before the player reports done
constexpr int kBeforeOpenMs = 1000; // minimum gap since the last reset before a new switch
std::atomic<std::int64_t> g_last_reset_ms{-100000};

std::int64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void sleep_ms(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

struct Api
{
    decltype(&sceAudioOutExOpen) open = &sceAudioOutExOpen;
    decltype(&sceAudioOutExClose) close = &sceAudioOutExClose;
    decltype(&sceAudioOutExConfigureOutput) configure = &sceAudioOutExConfigureOutput;
    decltype(&sceAudioOutSysGetHdmiMonitorInfo) monitor = &sceAudioOutSysGetHdmiMonitorInfo;

    static const Api &get()
    {
        static const Api api;
        return api;
    }
};

} // namespace

BitstreamPlayer::~BitstreamPlayer()
{
    stop();
}

pt::Sink BitstreamPlayer::query_sink()
{
    pt::Sink info;
    const Api &api = Api::get();
    if (api.monitor == nullptr)
    {
        info.rc = -1;
        return info;
    }
    std::uint8_t buffer[kMonitorInfoSize] = {};
    info.rc = api.monitor(1 /* HDMI */, buffer, sizeof(buffer));
    sys::log("[PT] GetHdmiMonitorInfo rc=0x%08x", static_cast<unsigned>(info.rc));
    if (info.rc < 0)
        return info;
    info.ok = true;
    // Layout read from one dump on FW 12.70 (see docs/FINDINGS.md): the
    // monitor name at +0x0B, the number of short audio descriptors as a
    // 32-bit value at +0x94, then 8-byte descriptors from +0x98.
    char name[14] = {};
    std::memcpy(name, buffer + 0x0B, 13);
    for (char &c : name)
        if (c != 0 && (c < 0x20 || c > 0x7E))
            c = 0;
    info.name = name;
    while (!info.name.empty() && info.name.back() == ' ')
        info.name.pop_back();
    std::uint32_t count = 0;
    std::memcpy(&count, buffer + 0x94, sizeof(count));
    for (std::uint32_t i = 0; i < count && 0x98 + (i + 1) * 8 <= kMonitorInfoSize; ++i)
    {
        const std::uint8_t *entry = buffer + 0x98 + i * 8;
        if (entry[0] == 0 || entry[0] > 15)
            break;
        info.formats.push_back({entry[0], entry[1]});
        sys::log("[PT] sink format coding=%d channels=%d", entry[0], entry[1]);
    }
    return info;
}

bool BitstreamPlayer::start(std::vector<std::uint8_t> stream, pt::Codec codec, bool loop)
{
    if (running_)
        return false;
    stream_ = std::move(stream);
    codec_ = codec;
    loop_ = loop;
    sample_rate_ = pt::carrier_for(codec).sample_rate;
    error_.clear();
    stop_.store(false);
    bursts_.store(0);
    output_errors_.store(0);
    port_frames_.store(0);
    open_rc_.store(0);
    config_rc_.store(0);
    state_.store(pt::PlayState::playing, std::memory_order_release);
    running_ = pthread_create(&thread_, nullptr, &BitstreamPlayer::thread_main, this) == 0;
    if (!running_)
        fail("could not start the playback thread");
    return running_;
}

void BitstreamPlayer::stop()
{
    if (!running_)
        return;
    stop_.store(true);
    pthread_join(thread_, nullptr);
    running_ = false;
}

void *BitstreamPlayer::thread_main(void *self)
{
    static_cast<BitstreamPlayer *>(self)->run();
    return nullptr;
}

void BitstreamPlayer::fail(const char *why)
{
    error_ = why;
    sys::log("[PT] failed: %s", why);
    state_.store(pt::PlayState::failed, std::memory_order_release);
}

void BitstreamPlayer::run()
{
    const Api &api = Api::get();
    const pt::Carrier carrier = pt::carrier_for(codec_);
    if (carrier.mode < 0)
    {
        fail("no carrier for this codec");
        return;
    }

    const bool is_pcm = codec_ == pt::Codec::pcm2 || codec_ == pt::Codec::pcm6;
    const std::int64_t since_reset = now_ms() - g_last_reset_ms.load();
    if (since_reset < kBeforeOpenMs)
        sleep_ms(static_cast<int>(kBeforeOpenMs - since_reset));

    int handle = -1;
    if (codec_ == pt::Codec::pcm2)
    {
        handle = sceAudioOutOpen(kSystemUser, 0, 0, carrier.grain_frames, carrier.sample_rate, 1 /* S16 stereo */);
    }
    else if (codec_ == pt::Codec::pcm6)
    {
        handle = sceAudioOutOpen(kSystemUser, 0, 0, carrier.grain_frames, carrier.sample_rate, 2 /* S16 8CH */);
    }
    else if (carrier.sys)
    {
        // The system open: its own mode table, including the 768 kHz port
        // Sony's Blu-ray player uses for Dolby TrueHD.
        handle = sceAudioOutSysOpen(kSystemUser, carrier.mode);
    }
    else
    {
        if (api.open == nullptr || api.close == nullptr || api.configure == nullptr)
        {
            fail("libSceAudioOut does not export the Ex functions");
            return;
        }
        handle = api.open(kSystemUser, carrier.mode);
    }

    open_rc_.store(handle);
    sys::log("[PT] %sOpen(mode %d) = 0x%08x", carrier.sys ? "Sys" : "", carrier.mode,
             static_cast<unsigned>(handle));
    if (handle < 0)
    {
        fail("could not open audio port");
        return;
    }

    // 2. ... then HDMI goes to the configured mode.
    int config = 0;
    if (api.configure != nullptr)
    {
        config = carrier.sys ? sceAudioOutSysConfigureOutput(1 /* type: HDMI */, 0, carrier.mode, kTargetHdmi, 0)
                             : api.configure(0, 0, carrier.mode, kTargetHdmi, 0);
        config_rc_.store(config);
        sys::log("[PT] %sConfigureOutput(mode %d, target %d) = 0x%08x", carrier.sys ? "Sys" : "Ex",
                 carrier.mode, kTargetHdmi, static_cast<unsigned>(config));
        if (config < 0)
        {
            if (codec_ == pt::Codec::pcm2 || codec_ == pt::Codec::pcm6)
                sceAudioOutClose(handle);
            else if (carrier.sys)
                sceAudioOutSysClose(handle);
            else
                api.close(handle);
            fail("sceAudioOutExConfigureOutput refused output mode");
            return;
        }
    }

    // 3. Bursts, one port grain at a time. sceAudioOutOutput blocks for each
    // grain, which paces the thread in real time.
    const std::size_t grain_bytes = (codec_ == pt::Codec::pcm6)
                                        ? static_cast<std::size_t>(carrier.grain_frames) * 16
                                        : static_cast<std::size_t>(carrier.grain_frames) *
                                              static_cast<std::size_t>(carrier.frame_bytes);
    // One grain of IEC 61937 null data: preamble, data type 0, nothing else.
    const auto send_null = [&](int ms)
    {
        std::vector<std::uint8_t> grain(grain_bytes, 0);
        if (!is_pcm)
        {
            grain[0] = 0x72; // Pa 0xF872, little-endian
            grain[1] = 0xF8;
            grain[2] = 0x1F; // Pb 0x4E1F
            grain[3] = 0x4E;
        }
        const int grains = ms * carrier.sample_rate / 1000 / carrier.grain_frames;
        for (int i = 0; i < grains; ++i)
            sceAudioOutOutput(handle, grain.data());
    };
    if (!is_pcm)
        send_null(kLeadInMs);

    int timed_grains = 0;
    pt::Packer packer(codec_);
    std::vector<std::uint8_t> pending;
    std::size_t cursor = 0;
    const char *error = nullptr;
    const std::span<const std::uint8_t> data(stream_.data(), stream_.size());
    while (!stop_.load(std::memory_order_relaxed))
    {
        while (pending.size() < grain_bytes)
        {
            if (packer.next_burst(data, &cursor, &pending, &error))
            {
                bursts_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (error != nullptr || !loop_)
                break;
            cursor = 0; // loop from the top
        }
        if (pending.size() < grain_bytes)
            break; // end of the stream (or an error)
        const std::int64_t t0 = now_ms();
        if (sceAudioOutOutput(handle, pending.data()) < 0)
            output_errors_.fetch_add(1, std::memory_order_relaxed);
        if (timed_grains < 6) // how long one grain blocks tells the port's real rate
        {
            sys::log("[PT] grain %d: %zu bytes took %lld ms", timed_grains, grain_bytes,
                     static_cast<long long>(now_ms() - t0));
            ++timed_grains;
        }
        port_frames_.fetch_add(static_cast<std::uint64_t>(carrier.grain_frames),
                               std::memory_order_relaxed);
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(grain_bytes));
    }

    // 4. Drain, close and give HDMI back to normal PCM.
    if (!is_pcm)
        send_null(kLeadOutMs);
    sceAudioOutOutput(handle, nullptr);
    const int close_rc = is_pcm        ? sceAudioOutClose(handle)
                         : carrier.sys ? sceAudioOutSysClose(handle)
                                       : api.close(handle);
    sleep_ms(kAfterCloseMs);
    const int reset_rc = (api.configure != nullptr)
                             ? api.configure(0, 0, kModeDefault, kModeDefault, 0)
                             : 0;
    g_last_reset_ms.store(now_ms());
    sleep_ms(kAfterResetMs);
    sys::log("[PT] done bursts=%llu errors=%llu close=0x%08x reset=0x%08x",
             static_cast<unsigned long long>(bursts_.load()),
             static_cast<unsigned long long>(output_errors_.load()),
             static_cast<unsigned>(close_rc), static_cast<unsigned>(reset_rc));
    if (error != nullptr)
        fail(error);
    else
        state_.store(pt::PlayState::done, std::memory_order_release);
}

} // namespace hui::ps5

namespace pt
{

std::unique_ptr<Player> make_player()
{
    return std::make_unique<hui::ps5::BitstreamPlayer>();
}

} // namespace pt
