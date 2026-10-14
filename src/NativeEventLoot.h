#pragma once
#include "cwsdk.h"
#include <cstdint>

namespace overworldBridge {
bool GenerateNativeEventLoot(cube::Creature *recipient, const IntVector2 &region, std::uint64_t roll,
                             cube::Item &item);
}
