#pragma once
// Pure population rules for the aquatic spawner in DivingEvent.cpp: caps,
// deterministic per-chunk spawn sites, and entity lifecycle decisions.
// Distances are in blocks. Tested by tests/aquatic_rules.cpp.
#include <cstdint>
constexpr int WaterPopulationCap = 24; // Tracked fish in total (the ocean holds most).
constexpr int WaterSchoolMax = 3;      // Fish per school.
constexpr int WaterSpawnRadius = 192;  // Chunks within this of the player are populated.
constexpr int WaterRetireRadius = 256; // Bosses beyond this are retired.
inline bool WaterWithinRadius(long long x, long long y, long long px, long long py, int radius) {
    long double dx = (long double)x - px, dy = (long double)y - py;
    return dx * dx + dy * dy <= (long double)radius * radius;
}

// Deterministic hash of a chunk/site, world seed and attempt number.
inline unsigned int WaterPopulationHash(int x, int y, int seed, unsigned int attempt) {
    uint32_t h = (uint32_t)x * 0x9e3779b9u ^ (uint32_t)y * 0x85ebca6bu ^ (uint32_t)seed ^
                 attempt * 0xc2b2ae35u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    return h;
}

inline bool WaterPopulationCanSpawn(int total, int local, long long now, long long next) {
    return total <= WaterPopulationCap - 2 && local == 0 && now >= next;
}

// 2 or 3 fish, limited by the remaining population budget (0 if under 2).
inline int WaterSchoolSize(int total, unsigned int random) {
    int remaining = WaterPopulationCap - total;
    if (remaining < 2)
        return 0;
    int wanted = 2 + random % 2;
    return wanted < remaining ? wanted : remaining;
}

// Keep retired/dead entities counted until the engine actually releases them.
enum class WaterEntityAction { Forget, Keep, Retire };
inline WaterEntityAction WaterEntityLifecycle(bool resident, bool alive, bool originLoaded,
                                              bool currentLoaded, bool retiring) {
    if (!resident)
        return WaterEntityAction::Forget;
    return alive && originLoaded && currentLoaded && !retiring ? WaterEntityAction::Keep
                                                               : WaterEntityAction::Retire;
}

// One in four boss rolls succeeds.
inline bool WaterBossRoll(unsigned int random) {
    return random % 4 == 0;
}

// Returns `tracked` if it is still in `residents` with the same ID, else null.
template <class List, class Pointer>
Pointer FindResidentWaterEntity(const List &residents, Pointer tracked, decltype(tracked->id) id) {
    // Never dereference an old pointer or trust an ID that another entity reused.
    for (auto resident : residents)
        if (resident == tracked && resident->id == id)
            return resident;
    return nullptr;
}

// Stable lattice anchors and species: independent of update order or visit count.
inline unsigned int WaterSiteSlot(unsigned int hash, unsigned int probe) {
    return (probe + hash % 16) % 16;
}
inline int WaterSiteCoordinate(unsigned int slot, bool y) {
    return 8 + (y ? slot / 4 : slot % 4) * 16;
}
inline int WaterSiteSpecies(unsigned int hash) {
    return (hash >> 8) % 13;
}

constexpr long long WaterSiteRespawnDelay = 600; // Seconds a cleared site stays empty.
inline bool WaterSiteCanRepopulate(long long now, long long emptySince) {
    return emptySince > 0 && now >= emptySince && now - emptySince >= WaterSiteRespawnDelay;
}

// Factory fish species for a site (CreatureFactory::SpawnFish index). Sea crabs
// (1) and mermaids (6) live only in the ocean, normal crabs (0) only in rivers
// and lakes; polar gnoll (5) never spawns.
inline int WaterSiteSpecies(unsigned int hash, bool ocean) {
    constexpr int sea[] = {1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12};
    constexpr int inland[] = {0, 2, 3, 4, 7, 8, 9, 10, 11, 12};
    return ocean ? sea[(hash >> 8) % 11] : inland[(hash >> 8) % 10];
}

// Every ocean chunk may host a school, only every third inland chunk.
inline bool WaterSiteEligible(int x, int y, int seed, bool ocean) {
    if (ocean)
        return true;
    long long n = (long long)x + y + seed;
    return (n % 3 + 3) % 3 == 0;
}
// Minimum horizontal distance between schools, in blocks.
inline int WaterSchoolSpacing(bool ocean) {
    return ocean ? 64 : 128;
}

// Open ocean: the ocean biome and water at least this deep. Rivers and coastal
// shallows inside an ocean region count as inland water.
constexpr long long OceanMinimumDepth = 6;
inline bool OceanWater(bool oceanBiome, long long columnDepth) {
    return oceanBiome && columnDepth >= OceanMinimumDepth;
}
// Blocks below the surface a school is anchored at. Inland and half of the
// ocean schools swim 2-4 blocks down; the other ocean half spreads from a
// quarter of the column down to two blocks above the floor.
inline long long WaterSchoolDepth(unsigned int hash, long long columnDepth, bool ocean) {
    long long shallow = 2 + hash % 3;
    if (!ocean || columnDepth < 8 || (hash >> 4) % 2 == 0)
        return shallow < columnDepth - 1 ? shallow : (columnDepth > 1 ? columnDepth - 1 : 0);
    long long top = columnDepth / 4, bottom = columnDepth - 2;
    if (bottom <= top)
        return top;
    return top + (long long)((hash >> 12) % (unsigned)(bottom - top + 1));
}
