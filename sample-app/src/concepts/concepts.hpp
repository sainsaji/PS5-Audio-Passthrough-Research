// Passthrough Lab - the app's designs (one).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Built on ps5-homebrew-ui, whose shell switches between designs with L1/R1.
// This app has a single design, so the switcher has nothing to switch to.

#pragma once

#include "app/concept.hpp"

#include <memory>

namespace hui::concepts
{

std::unique_ptr<app::Concept> make_passthrough(app::Context &context);

} // namespace hui::concepts
