// Passthrough Lab - HDMI bitstream output through libSceAudioOut.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/ps5/bitstream_out.hpp"

#include "platform/ps5/system.hpp"

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>

// Not in the SDK headers, but the SDK's libSceAudioOut stub exports them, so
// they link as ordinary imports. (A sandboxed app cannot look them up with
// sceKernelDlsym: the lookup is refused, so link them instead.)
extern "C"
{
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
    if (api.open == nullptr || api.close == nullptr || api.configure == nullptr)
    {
        fail("libSceAudioOut does not export the Ex functions");
        return;
    }
    if (carrier.mode < 0)
    {
        fail("no carrier for this codec");
        return;
    }

    // 1. The port first ...
    const int handle = api.open(kSystemUser, carrier.mode);
    open_rc_.store(handle);
    sys::log("[PT] ExOpen(0xFF, %d) = 0x%08x", carrier.mode, static_cast<unsigned>(handle));
    if (handle < 0)
    {
        fail("sceAudioOutExOpen refused the port");
        return;
    }
    // 2. ... then HDMI goes to bitstream mode for this codec.
    const int config = api.configure(0, 0, carrier.mode, kTargetHdmi, 0);
    config_rc_.store(config);
    sys::log("[PT] ExConfigureOutput(mode %d, target %d) = 0x%08x", carrier.mode, kTargetHdmi,
             static_cast<unsigned>(config));
    if (config < 0)
    {
        api.close(handle);
        fail("sceAudioOutExConfigureOutput refused bitstream mode");
        return;
    }

    // 3. Bursts, one port grain at a time. sceAudioOutOutput blocks for each
    // grain, which paces the thread in real time.
    const std::size_t grain_bytes = static_cast<std::size_t>(carrier.grain_frames) * 4;
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
        if (sceAudioOutOutput(handle, pending.data()) < 0)
            output_errors_.fetch_add(1, std::memory_order_relaxed);
        port_frames_.fetch_add(static_cast<std::uint64_t>(carrier.grain_frames),
                               std::memory_order_relaxed);
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(grain_bytes));
    }

    // 4. Drain, close and give HDMI back to normal PCM.
    sceAudioOutOutput(handle, nullptr);
    const int close_rc = api.close(handle);
    const int reset_rc = api.configure(0, 0, kModeDefault, kModeDefault, 0);
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
