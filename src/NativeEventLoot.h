#pragma once
#include "cwsdk.h"
#include <cstdint>

namespace overworldBridge {
bool GenerateNativeEventLoot(const IntVector2 &region, std::uint64_t roll, cube::Item &item);
}
