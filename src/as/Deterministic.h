#pragma once
// Deterministic mode for comparing renders between runs: Math.random and random() use
// a fixed seed, and getTimer()/intervals follow a clock that advances one frame
// duration per Player::tick instead of real time.

#include <cstdint>

namespace as {

void setDeterministic(uint32_t seed);
bool deterministic();
// Advances the deterministic clock (no effect otherwise).
void advanceClock(double ms);
// Current deterministic clock value, in ms.
double deterministicClock();

}  // namespace as
