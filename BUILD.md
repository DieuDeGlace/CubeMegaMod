# AI-assisted aquatic edits

This fork contains edits made by DieuDeGlace with OpenAI ChatGPT/Codex assistance. Based on [gijsgroenewegen/CubeMegaMod](https://github.com/gijsgroenewegen/CubeMegaMod), commit `b5efbd5`, in the upstream fork network rooted at ToufouMaster/CubeMegaMod. The original MIT license and author attribution are retained.

## Changes

Ambient aquatic mobs spawn near loaded water even while the player is on land, with a population cap of 60, throttled scans, range cleanup and idle swimming. Small fish are passive; aquatic predators and bosses are hostile. Aquatic boss selection uses aquatic races. Lakes choose hostile crab, spitter or slime groups with a matching boss and controlled accumulation. No nonexistent ice-spitter race was added. Existing XP-compatibility and region-unlock edits are included. Native rarity remains separate from PyroProgression numeric level scaling.

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
