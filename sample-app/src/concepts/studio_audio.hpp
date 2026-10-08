// Surround Sound Studio - which page owns the console's audio output.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bitstream mode mutes every PCM port of the app, and the speaker tests hold
// an 8-channel PCM port, so the two pages must never run at once. Each page
// registers how to stop itself; entering a page stops the others.

#pragma once

namespace hui::concepts
{

enum class StudioPage : int
{
    passthrough,
    speaker_lab,
    count,
};

using StopFn = void (*)(void *self);

void studio_register(StudioPage page, StopFn stop, void *self);
void studio_unregister(StudioPage page);
// Stops every page but `page`.
void studio_enter(StudioPage page);

} // namespace hui::concepts
