#pragma once
#include "cwsdk.h"
#include <climits>
// Use the native getter so terrain-mod water ceilings are respected.
// SDK GetBlockInterpolated independently assumes a global zero sea level.
inline cube::Block NativeWaterBlock(cube::World *world, const LongVector3 &p) {
    cube::Block out{};
    out.type = cube::Block::Air;
    if (!world || p.x < INT_MIN || p.x > INT_MAX || p.y < INT_MIN || p.y > INT_MAX ||
        p.z < INT_MIN || p.z > INT_MAX)
        return out;
    auto coords = cube::Zone::ZoneCoordsFromBlocks(p.x, p.y);
    auto it = world->zones.find(coords);
    if (it == world->zones.end() || !it->second)
        return out;
    ((cube::Block * (*)(cube::Zone *, cube::Block *, int, int, int))((char *)CWBase() + 0xeb90))(
        it->second, &out, (int)p.x, (int)p.y, (int)p.z);
    return out;
}
