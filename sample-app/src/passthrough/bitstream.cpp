// Passthrough Lab - platform-neutral helpers of the player interface.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "passthrough/bitstream.hpp"

namespace pt
{

bool Sink::supports(int coding) const
{
    for (const SinkFormat &f : formats)
        if (f.coding == coding)
            return true;
    return false;
}

int coding_for(Codec codec)
{
    switch (codec)
    {
    case Codec::ac3:
        return 2;
    case Codec::dts:
        return 7;
    case Codec::aac:
        return 6;
    case Codec::eac3:
        return 10;
    case Codec::unknown:
        break;
    }
    return 0;
}

const char *coding_name(int coding)
{
    switch (coding)
    {
    case 1:
        return "PCM";
    case 2:
        return "Dolby Digital";
    case 6:
        return "AAC";
    case 7:
        return "DTS";
    case 10:
        return "Dolby Digital Plus";
    case 11:
        return "DTS-HD";
    case 12:
        return "Dolby TrueHD";
    default:
        return "Other";
    }
}

} // namespace pt
