// Passthrough Lab - a pretend player for the PC preview.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The preview has no HDMI and no console audio. This stand-in reports the
// receiver from the hardware test (an LG UltraGear in front of a soundbar)
// and counts bursts as if it played, so every state of the screen can be
// drawn on a PC.

#include "passthrough/bitstream.hpp"

#include <memory>

namespace
{

class HostPlayer final : public pt::Player
{
  public:
    pt::Sink query_sink() override
    {
        pt::Sink sink;
        sink.ok = true;
        sink.name = "LG ULTRAGEAR";
        sink.formats = {{1, 2}, {1, 6}, {12, 8}, {10, 8}, {2, 6}, {6, 6}, {7, 6}, {11, 8}};
        return sink;
    }
    bool start(std::vector<std::uint8_t> stream, pt::Codec codec, bool) override
    {
        bursts_ = stream.size() / 1792;
        seconds_ = codec == pt::Codec::unknown ? 0.0 : 4.2;
        state_ = pt::PlayState::playing;
        return true;
    }
    void stop() override
    {
        state_ = pt::PlayState::done;
    }
    pt::PlayState state() const override
    {
        return state_;
    }
    bool busy() const override
    {
        return false;
    }
    std::uint64_t bursts() const override
    {
        return bursts_;
    }
    std::uint64_t output_errors() const override
    {
        return 0;
    }
    double seconds() const override
    {
        return seconds_;
    }
    int open_rc() const override
    {
        return 0x2006001f;
    }
    int config_rc() const override
    {
        return 0;
    }
    const std::string &error() const override
    {
        return error_;
    }

  private:
    pt::PlayState state_ = pt::PlayState::idle;
    std::uint64_t bursts_ = 0;
    double seconds_ = 0.0;
    std::string error_;
};

} // namespace

namespace pt
{

std::unique_ptr<Player> make_player()
{
    return std::make_unique<HostPlayer>();
}

} // namespace pt
