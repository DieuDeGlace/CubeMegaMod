#pragma once
#include <cstdint>
constexpr int WaterPopulationCap = 18;
constexpr int WaterSchoolMax = 3;
constexpr int WaterSpawnRadius = 192;
constexpr int WaterRetireRadius = 256;
inline bool WaterWithinRadius(long long x, long long y, long long px, long long py, int radius) {
    long double dx = (long double)x - px, dy = (long double)y - py;
    return dx * dx + dy * dy <= (long double)radius * radius;
}

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

inline bool WaterBossRoll(unsigned int random) {
    return random % 4 == 0;
}

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

constexpr long long WaterSiteRespawnDelay = 600;
inline bool WaterSiteCanRepopulate(long long now, long long emptySince) {
    return emptySince > 0 && now >= emptySince && now - emptySince >= WaterSiteRespawnDelay;
}

inline int WaterSiteSpecies(unsigned int hash, bool ocean) {
    constexpr int sea[] = {0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12};
    constexpr int inland[] = {2, 3, 4, 7, 8, 9, 10, 11, 12};
    return ocean ? sea[(hash >> 8) % 12] : inland[(hash >> 8) % 9];
}

inline bool WaterSiteEligible(int x, int y, int seed, bool ocean) {
    long long n = (long long)x + y + seed;
    int period = ocean ? 2 : 3;
    return (n % period + period) % period == 0;
}
