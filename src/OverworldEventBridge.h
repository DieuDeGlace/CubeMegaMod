#pragma once

#include "cwsdk.h"

namespace overworldBridge {
// A handled interaction belongs entirely to OverworldEvents, including whether
// the object opens. CubeMega must not grant its ordinary chest reward afterward.
bool Talk(cube::Game *game, cube::Creature *creature);
bool ObjectInteraction(cube::Game *game, cube::Creature *creature);
void CreatureDeath(cube::Game *game, cube::Creature *victim, cube::Creature *attacker);
} // namespace overworldBridge

EXPORT unsigned OverworldBridgeVersion();
EXPORT cube::Creature *SpawnOverworldEntity(const LongVector3 *position, const IntVector2 *region,
                                            int race, int behaviour, int stars);
EXPORT void DropOverworldReward(cube::Game *game, cube::Creature *recipient, const cube::Item *item,
                                cube::Creature *source);
EXPORT int GenerateOverworldGear(const IntVector2 *region, std::uint64_t roll, cube::Item *out);
EXPORT int GrantOverworldKey(cube::Game *game, cube::Creature *recipient, const cube::Item *key);
EXPORT int ConsumeOverworldKey(cube::Game *game, cube::Creature *recipient, const cube::Item *key);
EXPORT void OverworldDialogue(cube::Game *game, cube::Creature *speaker, const wchar_t *text);
