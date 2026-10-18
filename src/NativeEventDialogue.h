#pragma once

namespace cube {
class Game;
class Creature;
} // namespace cube

namespace overworldBridge {
// Call from the game's existing interaction/update thread. Returns false if
// the guarded Steam speech entry or initialized GUI is unavailable.
bool ShowNativeEventDialogue(cube::Game *game, cube::Creature *speaker, const wchar_t *text);
} // namespace overworldBridge
