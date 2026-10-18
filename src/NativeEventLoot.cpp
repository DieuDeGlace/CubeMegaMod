#include "NativeEventLoot.h"
#include "EventLootRules.h"
#include <cstring>

namespace {
using Generate = cube::Item *(*)(cube::World *, cube::Item *, int, int, bool);
bool generatorsAvailable() {
    static const unsigned char entry[16] = {0x40, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
                                            0x41, 0x57, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec};
    return std::memcmp(CWOffset(0x2d0680), entry, sizeof(entry)) == 0 &&
           std::memcmp(CWOffset(0x2d15d0), entry, sizeof(entry)) == 0;
}
std::uint64_t mix(std::uint64_t value) {
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}
} // namespace

bool overworldBridge::GenerateNativeEventLoot(cube::Creature *player, const IntVector2 &region,
                                              std::uint64_t roll, cube::Item &item) {
    auto game = cube::GetGame();
    if (!player || !game || !game->world || !game->host.running || !generatorsAvailable())
        return false;
    // Generate for the creature that will receive the drop, never a stand-in.
    int recipient = player->entity_data.classType;
    if (recipient < 1 || recipient > 5)
        return false;
    int nativeClass = recipient == 5 ? 4 : recipient;
    int rarity = int((roll >> 32) % 5);
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        cube::Item candidate;
        // These are the same native generators used by chest rewards. Their
        // output families/materials are then checked independently, fail closed.
        bool weapon = ((roll >> (attempt % 8)) & 1) != 0;
        if (attempt == 7)
            weapon = false; // Native armor/jewelry remains valid for Monk too.
        auto generator = reinterpret_cast<Generate>(CWOffset(weapon ? 0x2d15d0 : 0x2d0680));
        if (generator(&game->host.world, &candidate, rarity, nativeClass, false) != &candidate ||
            !loot::recipientGear(candidate, recipient))
            continue;
        // The event cycle supplies a stable identity without reseeding native RNG.
        candidate.modifier = static_cast<unsigned>(mix(roll + 0x9e3779b97f4a7c15ull)) | 1u;
        candidate.region = region;
        item = candidate;
        return true;
    }
    return false;
}
