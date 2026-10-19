#pragma once

#include "cwsdk.h"
#include <utility>
#include <chrono>
#include "../../../cwsdk-extension/helper/AquaticRules.h"

#include "../src/cwsdk-extension.h"
#include "../../../cwsdk-extension/helper/WaterPopulationRules.h"

// Fish spawning constants
const static double FISH_SPAWN_INTERVAL = 1;
const static auto MAX_FISH_COUNT = WaterPopulationCap;
const static auto MAX_FISH_PER_ZONE = WaterSchoolMax;
const static double WATER_ZONE_RESPAWN_INTERVAL = 120;

// Treasure spawning constants
const static double TREASURE_SPAWN_INTERVAL = 60;
const static auto MAX_TREASURE_COUNT = 5;

// Boss spawning constants
const static double BOSS_SPAWN_INTERVAL = 600;
const static auto MAX_BOSS_COUNT = 1;

// Other constants
const static auto BOUNDS_CHECK_DIST_MULTIPLIER = 2;
const static long long CREATURE_SPAWN_RANGE = 5000000;
const static double BOUNDS_CHECK_INTERVAL = 5;

// A spawned entity, identified by both pointer and ID (see FindResidentWaterEntity),
// plus the swimming controller's per-creature state. Times are in swim seconds.
struct IdCreaturePair {
    __int64 id;
    cube::Creature *creature;
    bool retiring = false;    // Killed by us; slot kept until the engine releases it.
    bool hasRoamHome = false; // Bosses: roamHome is where they were first seen.
    LongVector3 roamHome;
    SwimPoint swimTarget;
    bool swimming = false; // Moving toward swimTarget.
    double nextWaterRepair = 0;
    bool waterRecoveryUsed = false; // Depth recovery is allowed once per creature.
    bool recoveringWater = false;   // Sinking back toward waterRecoveryTarget.
    SwimPoint waterRecoveryTarget;
    bool hasMotionSample = false;
    SwimPoint lastMotionPosition; // Position (blocks) at the previous update.
    // The controller drives this actor by velocity (as native AI does), so the
    // walk/swim animation plays; drivenVelocity is what it last reported.
    bool driving = false;
    SwimPoint drivenVelocity;
    unsigned int swimAttempt = 0;
    double nextSwimPlan = 0;
    int lastAbility = 0;
    int lastCombo = 0;
};

// A school of fish (or a lake encounter group) and the site it was spawned at.
struct SpawnMoment {
    LongVector3 position;
    int lakeRace = 0; // Non-zero: a lake encounter that uses native ground AI.
    std::vector<IdCreaturePair> creature_ids;
};

// Spawn state of one loaded water chunk (zone).
struct WaterZonePopulation {
    IntVector2 zone;
    unsigned int siteProbe = 0; // Next of the 16 lattice sites to try.
    bool populated = false;
    time_t emptySince = 0;  // When its population was last seen gone (0 = not empty).
    bool hasAnchor = false; // anchor: the validated school site, reused on respawn.
    LongVector3 anchor;
    bool openOcean = false; // The anchor is in deep ocean water (species/depth rules).
    time_t next_spawn = 0;
    bool boss_attempted = false;
};

static long long DistanceSquared(const LongVector3 &p1, const LongVector3 &p2);
struct NativeLakeLocation;

namespace cube {
class DivingEvent : public Event {
  private:
    cube::Timer *m_ItemEffectTimer;

    cube::Timer m_SpawnTimer;
    cube::Timer m_TreasureTimer;
    cube::Timer m_BossTimer;
    cube::Timer m_BoundsTimer;

    std::vector<SpawnMoment> m_SpawnedCreatures;
    std::vector<IdCreaturePair> m_SpawnedTreasures;
    std::vector<IdCreaturePair> m_SpawnedBosses;
    IntVector2 m_BossZone;
    std::vector<IntVector2> m_LakeBossVisits;
    std::vector<WaterZonePopulation> m_WaterZones;
    std::vector<WaterZonePopulation> m_DormantWaterZones; // Unloaded sites still cooling down.
    size_t m_NextWaterZone = 0;
    std::chrono::steady_clock::time_point m_LastSwimTick = std::chrono::steady_clock::now();
    double m_SwimTime = 0;
    double m_NextSwimCheck = 0;
    double m_NextSwimPlanBurst = 0;

    bool *autoGoldUsage;
    bool m_IsDiving = false;

  public:
    DivingEvent(bool *autoGoldUsage);
    ~DivingEvent();

    void Update() override;
    void Abandon();

  private:
    void UpdateIds();
    void SetDiving(bool diving);

    // Consumtion logic
    void HandleItemEffectTimer();
    void ConsumeItem();

    // Fish logic
    void HandleFishTimer();
    void UpdateSwimming();
    void SwimCreature(cube::World *world, const SpawnMoment &school, IdCreaturePair &pair,
                      cube::Creature *creature, double dt, bool check, unsigned &plans);
    void SpawnFishes(const LongVector3 &position);
    void RefreshWaterZones(const std::vector<IntVector2> &loaded);
    bool ZoneHasLivingFish(const IntVector2 &zone);
    bool ZoneOccupied(const IntVector2 &zone);
    bool SpawnLakeEncounter(const NativeLakeLocation &lake, LongVector3 spawn, int total,
                            unsigned int hash);
    bool SpawnSchool(WaterZonePopulation &state, LongVector3 spawn, long long blockX,
                     long long blockY, int total, unsigned int hash);
    void GetLocalEnemyProfile(const LongVector3 &position, IntVector2 *region, int *stars);
    bool IsOwnSpawn(const cube::Creature *creature) const;

    // Treasure logic
    void HandleTreasureTimer();
    void SpawnTreasures(const LongVector3 &position);
    int GenerateChestType();

    // Boss logic
    void HandleBossTimer();
    void SpawnBoss(const LongVector3 &position);

    // Deletion logic
    void HandleBoundsCheckTimer();
    void BoundCheckFishes(const LongVector3 &position);
    void BoundCheckTreasures(const LongVector3 &position);
    void BoundCheckBosses(const LongVector3 &position);
};
} // namespace cube
