// Surround Sound Studio - the app's pages.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Built on ps5-homebrew-ui, whose shell switches between designs ("pages"
// here) with L1 / R1.

#pragma once

#include "app/concept.hpp"

#include <memory>

namespace hui::concepts
{

// HDMI bitstream: Dolby, DTS and AAC decoded by the receiver.
std::unique_ptr<app::Concept> make_passthrough(app::Context &context);
// Speaker tests, the 3D sound field and DualSense mic calibration (from EVO Player).
std::unique_ptr<app::Concept> make_speaker_lab(app::Context &context);

} // namespace hui::concepts
