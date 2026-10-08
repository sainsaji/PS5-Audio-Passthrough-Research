// Passthrough Lab - HDMI bitstream output through libSceAudioOut.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sequence Sony's own media core (citroncore.elf) uses, found by reverse
// engineering and confirmed on a PS5 Pro, FW 12.70:
//
//   h = sceAudioOutExOpen(0xFF, mode);                 // open the carrier port first
//   sceAudioOutExConfigureOutput(0, 0, mode, 1, 0);    // then switch HDMI to bitstream
//   sceAudioOutOutput(h, burst...)                     // IEC 61937 bursts, S16 stereo
//   sceAudioOutOutput(h, NULL); sceAudioOutExClose(h);
//   sceAudioOutExConfigureOutput(0, 0, 0xFF, 0xFF, 0); // back to normal PCM
//
// None of these are in the SDK headers; they are resolved at runtime. While
// bitstream mode is on, the console mutes every PCM port of this process, so
// the UI's own sounds go quiet until playback stops.

#pragma once

#include "passthrough/bitstream.hpp"

#include <atomic>
#include <cstdint>
#include <pthread.h>
#include <string>
#include <vector>

namespace hui::ps5
{

class BitstreamPlayer final : public pt::Player
{
  public:
    BitstreamPlayer() = default;
    BitstreamPlayer(const BitstreamPlayer &) = delete;
    BitstreamPlayer &operator=(const BitstreamPlayer &) = delete;
    ~BitstreamPlayer() override;

    pt::Sink query_sink() override;
    bool start(std::vector<std::uint8_t> stream, pt::Codec codec, bool loop) override;
    void stop() override;

    pt::PlayState state() const override
    {
        return state_.load(std::memory_order_acquire);
    }
    bool busy() const override
    {
        return running_;
    }
    std::uint64_t bursts() const override
    {
        return bursts_.load(std::memory_order_relaxed);
    }
    std::uint64_t output_errors() const override
    {
        return output_errors_.load(std::memory_order_relaxed);
    }
    double seconds() const override
    {
        return static_cast<double>(port_frames_.load(std::memory_order_relaxed)) /
               (sample_rate_ > 0 ? sample_rate_ : 48000);
    }
    int open_rc() const override
    {
        return open_rc_.load(std::memory_order_relaxed);
    }
    int config_rc() const override
    {
        return config_rc_.load(std::memory_order_relaxed);
    }
    const std::string &error() const override
    {
        return error_;
    }

  private:
    static void *thread_main(void *self);
    void run();
    void fail(const char *why);

    std::vector<std::uint8_t> stream_;
    pt::Codec codec_ = pt::Codec::unknown;
    bool loop_ = false;
    int sample_rate_ = 48000;
    pthread_t thread_{};
    bool running_ = false;
    std::string error_;
    std::atomic<bool> stop_{false};
    std::atomic<pt::PlayState> state_{pt::PlayState::idle};
    std::atomic<std::uint64_t> bursts_{0};
    std::atomic<std::uint64_t> output_errors_{0};
    std::atomic<std::uint64_t> port_frames_{0};
    std::atomic<int> open_rc_{0};
    std::atomic<int> config_rc_{0};
};

} // namespace hui::ps5
