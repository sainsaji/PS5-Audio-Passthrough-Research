// Surround Sound Studio - the pages, in L1 / R1 order.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concepts/concepts.hpp"

namespace hui::app
{

std::span<const ConceptFactory> concept_registry()
{
    static constexpr ConceptFactory kFactories[] = {
        concepts::make_passthrough,
        concepts::make_speaker_lab,
    };
    return kFactories;
}

} // namespace hui::app
