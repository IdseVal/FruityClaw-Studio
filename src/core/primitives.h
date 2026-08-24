// Primitive types of the Project data model.
// Contract: docs/specs/project-data-model.md section 3.0.
#pragma once

#include <cstdint>

namespace core {

// Musical time. Integer ticks at a fixed pulses-per-quarter, so deltas
// round-trip bit-for-bit (invariant I4); floating-point time cannot promise that.
using Ticks = std::int64_t;

// Fixed by the contract, not per-Project. 960 = 2^6 * 3 * 5, so triplets and
// quintuplets land on integers.
inline constexpr Ticks kPpq = 960;

// MIDI-native pitch. 60 = middle C.
using Pitch = std::uint8_t;

// MIDI-native velocity, 0-127.
using Velocity = std::uint8_t;

// Gain-like continuous parameter in [0, 1]. Never used for time, pitch or velocity.
using Unit = float;

// Authored presentation colour, 0xRRGGBB. Optional on the entities that carry it.
using Colour = std::uint32_t;

}  // namespace core
