// Passthrough Lab - what the screen needs from a bitstream player.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The console implementation is src/platform/ps5/bitstream_out.cpp; the PC
// preview uses host/bitstream_host.cpp, which only pretends.

#pragma once

#include "passthrough/iec61937.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pt
{

// One format the TV or receiver lists in its HDMI EDID.
struct SinkFormat
{
    int coding = 0;   // CEA-861 audio coding type: 1 PCM, 2 AC-3, 7 DTS, 10 E-AC-3, ...
    int channels = 0;
};

struct Sink
{
    bool ok = false;
    int rc = 0;       // what sceAudioOutSysGetHdmiMonitorInfo returned
    std::string name; // monitor name from the EDID
    std::vector<SinkFormat> formats;

    bool supports(int coding) const;
};

// CEA-861 coding type for a codec, to compare with Sink::formats.
int coding_for(Codec codec);
const char *coding_name(int coding);

enum class PlayState : std::uint8_t
{
    idle,
    playing,
    done,
    failed,
};

class Player
{
  public:
    virtual ~Player() = default;

    // What the HDMI device says it can decode. Cheap; repeat after a hot-plug.
    virtual Sink query_sink() = 0;

    // Plays a whole elementary stream on a thread. False if one is playing.
    virtual bool start(std::vector<std::uint8_t> stream, Codec codec, bool loop) = 0;
    // Stops, waits for the thread, restores normal HDMI audio.
    virtual void stop() = 0;

    virtual PlayState state() const = 0;
    virtual bool busy() const = 0;
    virtual std::uint64_t bursts() const = 0;
    virtual std::uint64_t output_errors() const = 0;
    virtual double seconds() const = 0;
    virtual int open_rc() const = 0;
    virtual int config_rc() const = 0;
    // Why the last run failed. Read it only when state() is failed.
    virtual const std::string &error() const = 0;
};

std::unique_ptr<Player> make_player();

} // namespace pt
