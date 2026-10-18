#pragma once

#include "cwsdk.h"

namespace overworldBridge {
namespace inventory {
using Tabs = std::vector<std::vector<cube::ItemStack>>;
// Native inventory classifier 0x5f100 routes category 22 through 0x5f137,
// which returns tab 1. The loaded Steam item dictionary contains key IDs 0-19.
constexpr size_t KeyTab = 1;
inline bool NativeKey(const cube::Item &item) {
    return item.category == 22 && item.id >= 0 && item.id < 20;
}
inline bool Matches(const cube::Item &item, const cube::Item &key) {
    return item.category == key.category && item.id == key.id && item.modifier == key.modifier &&
           item.region == key.region;
}
// Return 0 for an unsupported inventory, 1 if already owned, 2 when granted.
inline int Grant(Tabs &tabs, const cube::Item &key) {
    if (!NativeKey(key) || tabs.size() <= KeyTab)
        return 0;
    for (const auto &tab : tabs)
        for (const auto &stack : tab)
            if (stack.quantity > 0 && Matches(stack.item, key))
                return 1;
    tabs[KeyTab].push_back(cube::ItemStack(1, key));
    return 2;
}
inline bool Consume(Tabs &tabs, const cube::Item &key) {
    if (!NativeKey(key))
        return false;
    for (auto &tab : tabs) {
        for (auto stack = tab.begin(); stack != tab.end(); ++stack) {
            if (stack->quantity <= 0 || !Matches(stack->item, key))
                continue;
            if (--stack->quantity == 0)
                tab.erase(stack);
            return true;
        }
    }
    return false;
}
} // namespace inventory
// Bridge version 2: gear generation takes the recipient, and reward delivery
// reports whether the item passed validation and reached the native drop.
constexpr unsigned Version = 2;
// SpawnOverworldEntity allocates IDs from this positive range. Native IDs are
// not known to avoid it, so the range alone never identifies an event actor.
constexpr std::uint64_t FirstEntityId = 0x6000000000000000ull;
constexpr std::uint64_t EntityIdLimit = 0x7fffffffffffffffull;
// True only for an ID this bridge spawned during the current game process.
bool SpawnedEventEntity(std::uint64_t id);
// A handled interaction belongs entirely to OverworldEvents, including whether
// the object opens. CubeMega must not grant its ordinary chest reward afterward.
bool Talk(cube::Game *game, cube::Creature *creature);
bool ObjectInteraction(cube::Game *game, cube::Creature *creature);
// Preflight the additional E-key predicate hook before replacing its block.
bool SupportsExaminationHook();
bool CanExamine(cube::Creature *creature);
void CreatureDeath(cube::Game *game, cube::Creature *victim, cube::Creature *attacker);
} // namespace overworldBridge

EXPORT unsigned OverworldBridgeVersion();
EXPORT cube::Creature *SpawnOverworldEntity(const LongVector3 *position, const IntVector2 *region,
                                            int race, int behaviour, int stars);
// Returns 1 only when the item is valid for the recipient's class and was dropped.
EXPORT int DropOverworldReward(cube::Game *game, cube::Creature *recipient, const cube::Item *item,
                               cube::Creature *source);
EXPORT int GenerateOverworldGear(cube::Creature *recipient, const IntVector2 *region,
                                 std::uint64_t roll, cube::Item *out);
EXPORT int GrantOverworldKey(cube::Game *game, cube::Creature *recipient, const cube::Item *key);
EXPORT int ConsumeOverworldKey(cube::Game *game, cube::Creature *recipient, const cube::Item *key);
EXPORT void OverworldDialogue(cube::Game *game, cube::Creature *speaker, const wchar_t *text);
