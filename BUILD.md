# AI-assisted aquatic edits

This fork contains edits made by DieuDeGlace with OpenAI ChatGPT/Codex assistance. Based on [gijsgroenewegen/CubeMegaMod](https://github.com/gijsgroenewegen/CubeMegaMod), commit `b5efbd5`, in the upstream fork network rooted at ToufouMaster/CubeMegaMod. The original MIT license and author attribution are retained.

## Changes

Ambient aquatic mobs spawn near loaded water even while the player is on land, from stable chunk sites with a population cap of 18, bounded activation, range cleanup and idle swimming. Small fish are passive; aquatic predators and bosses are hostile. Aquatic boss selection uses aquatic races. Lakes choose hostile crab, spitter or slime groups with a matching boss and controlled accumulation. No nonexistent ice-spitter race was added. Existing XP-compatibility and region-unlock edits are included. Native rarity remains separate from PyroProgression numeric level scaling.

## Build on Windows x64

Install Git, CMake, Ninja, and Visual Studio 2022 Build Tools with Desktop development with C++, Windows SDK, and LLVM/Clang tools. In an **x64 Native Tools Command Prompt for VS 2022**, make sure `clang-cl`, `cmake`, and `ninja` are on PATH.

```bat
git clone https://github.com/DieuDeGlace/CubeMegaMod.git
cd CubeMegaMod
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

Output: `build/CubeMegaMod-XPC-RU1-AmbientWater.dll`. Close Steam Cube World and replace your existing CubeMegaMod DLL with this build; the installed filename may be `CubeMegaMod-XPC-RU1.dll`. Install only one CubeMegaMod version. Requires compatible loader API 7.3 and the inspected Steam executable. The repository does not include game assets or saves.

The source test `tests/water_spawn_search.cpp` can be compiled with `clang-cl /std:c++14 /EHsc tests\water_spawn_search.cpp /Fewater_spawn_search.exe` and then run. Compile and source-level tests do not establish every gameplay or multiplayer case.

Water schools use a stable 4x4 candidate lattice per loaded chunk and a world-seeded species. One school of 2–3 is activated per water chunk (up to four per one-second update). Empty occupied sites repopulate after ten continuous minutes empty, using the same anchor and species; cleared sites retain their remaining cooldown across chunk unload within the current session. Unloaded world areas are not generated. Underwater bosses retain their separate random roll. Lake shoreline encounters retain their fixed dominant species and matching boss. Cooldowns reset when leaving the world or restarting the game.

Sea crabs and mermaids are ocean-only, including sea-crab boss selection. Normal lake crab encounters remain. Polar gnoll (an old incorrect aquatic factory entry) is excluded from ambient schools.

Aquatic density/residency update: 18 custom creatures maximum; inland sites use one in three chunks and oceans one in two, with school anchors at least 128/96 blocks apart respectively. Living fish retire when their origin/current chunk unloads, without an extra distance limit. Living populations can populate again on chunk reload; cleared sites retain a ten-minute session cooldown. Neutral fish retain passive behavior without an extra ally flag. Native terrain-aware water lookup and bounded depth recovery replace zero-sea-level assumptions. Movement and predator damage still require in-game verification after installing this build.


## Optional OverworldEvents integration

The `test/overworld-events` branch was edited with the aid of AI. It adds an optional bridge for the separate OverworldEvents DLL, using CubeMega's existing NPC talk and chest dispatch hooks. Handled event interactions bypass ordinary CubeMega rewards, including denial of a locked chest. When OverworldEvents is absent, existing dispatch remains unchanged.

The bridge exports native entity spawning/appearance with unique positive IDs, native loot delivery through the existing PyroProgression drop hook, gear generation, event-bound key grant/consumption, and chat dialogue. Keys are matched by category, ID, modifier and region; inventory mutation uses CubeMega's game allocator. The supported Steam executable is checked before native calls. No general renderer, ReShade, water movement, or world-generation behavior is changed by this bridge.

This is development integration. Native AI and cage/sack/key interactions still need gameplay verification. Build with the existing instructions above and retain the installed basename `CubeMegaMod-XPC-RU1.dll` for bridge discovery.
