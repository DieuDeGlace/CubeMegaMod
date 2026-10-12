#pragma once

// Finds the water height nearest `start` (alternating below, then above, up to
// 128 blocks away) and stores it in *result.
// Search a bounded vertical column. Both the body and the block above it must
// contain water. A predicate keeps this algorithm testable without the game.
template <typename IsWater>
bool FindWaterSpawnHeight(long long start, long long *result, IsWater isWater) {
    for (long long distance = 0; distance <= 128; ++distance) {
        long long below = start - distance;
        if (isWater(below) && isWater(below + 1)) {
            *result = below;
            return true;
        }
        if (distance != 0) {
            long long above = start + distance;
            if (isWater(above) && isWater(above + 1)) {
                *result = above;
                return true;
            }
        }
    }
    return false;
}
