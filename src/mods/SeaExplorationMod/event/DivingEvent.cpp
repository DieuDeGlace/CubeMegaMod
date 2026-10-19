#include "DivingEvent.h"
#include "../../../cwsdk-extension/helper/WaterPopulationRules.h"
#include <algorithm>
#include "../../../cwsdk-extension/helper/NativeWaterBlock.h"
#include "../../../cwsdk-extension/helper/WaterSpawnSearch.h"
#include "common/math.h"

// ===========================================================================
// Native queries and world helpers (aquatic population)
// ===========================================================================

// RVAs in the supported Steam cubeworld.exe.
constexpr size_t LOCATION_AT_RVA = 0x35ce0; // void (World*, x, y, NativeLakeLocation* out)
constexpr size_t REGION_AT_RVA = 0x2ada20;  // void (World*, IntVector2* out, x, y)
constexpr size_t BIOME_AT_RVA = 0x2b9080;   // int (World*, regionX, regionY)
constexpr int LAKE_LOCATION_TYPE = 13;      // Native location type: Lake (14 is Lava Lake).
constexpr int OCEAN_BIOME = 1;
constexpr double HALF_PI = 1.570796326794897;

using BlockType = decltype(cube::Block{}.type);
using Residents = std::vector<cube::Creature *>;

// Verified native location descriptor and query: location type 13 is Lake,
// type 14 is Lava Lake. Query does not load or generate terrain.
struct NativeLakeLocation {
    int type;
    unsigned char ocean, pad[3];
    int subtype, radius, x, y;
    float distanceSquared, influence;
};
static_assert(sizeof(NativeLakeLocation) == 32, "Native location layout");
static NativeLakeLocation LakeLocation(cube::World *world, long long x, long long y) {
    NativeLakeLocation result{};
    if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX)
        return result;
    reinterpret_cast<void (*)(cube::World *, int, int, NativeLakeLocation *)>(
        (char *)CWBase() + LOCATION_AT_RVA)(world, (int)x, (int)y, &result);
    return result;
}

// True if the zone containing block column (x, y) is loaded.
static bool BlockLoaded(cube::World *world, long long x, long long y) {
    return world->GetZone(cube::Zone::ZoneCoordsFromBlocks(x, y)) != nullptr;
}

// Ground a lake creature can stand on.
static bool IsLakeSupport(BlockType type) {
    return type == cube::Block::Solid || type == cube::Block::Grass ||
           type == cube::Block::Ground || type == cube::Block::Snow || type == cube::Block::Path;
}

// True if there is water within a few blocks of (x, y, z), in loaded zones only.
static bool NearLakeWater(cube::World *world, long long x, long long y, long long z) {
    for (int dx = -4; dx <= 4; dx += 2)
        for (int dy = -4; dy <= 4; dy += 2) {
            if (!BlockLoaded(world, x + dx, y + dy))
                continue;
            for (int dz = -3; dz <= 1; dz++)
                if (NativeWaterBlock(world, LongVector3(x + dx, y + dy, z + dz)).type ==
                    cube::Block::Water)
                    return true;
        }
    return false;
}

// Ground creatures use dry shoreline/support rather than fish swim positions.
// A fixed 17-column budget and loaded-zone checks bound every search.
// On success *position becomes the centre of a free spot on the shore of `lake`.
static bool LakeGround(cube::World *world, LongVector3 *position, const NativeLakeLocation &lake) {
    long long bx = pydiv(position->x, cube::DOTS_PER_BLOCK),
              by = pydiv(position->y, cube::DOTS_PER_BLOCK);
    for (int probe = 0; probe < 17; probe++) {
        // Probe 0 is the start column; then +x, -x, +y, -y at 4, 8, 12 and 16 blocks.
        int radius = probe == 0 ? 0 : 4 * ((probe - 1) / 4 + 1), side = (probe - 1) % 4;
        long long x = bx + (side == 0   ? radius
                            : side == 1 ? -radius
                                        : 0),
                  y = by + (side == 2   ? radius
                            : side == 3 ? -radius
                                        : 0);
        auto zone = world->GetZone(cube::Zone::ZoneCoordsFromBlocks(x, y));
        if (!zone)
            continue;
        auto location = LakeLocation(world, x, y);
        if (location.type != LAKE_LOCATION_TYPE || location.influence <= 0 ||
            location.x != lake.x || location.y != lake.y)
            continue;
        auto &field = zone->fields[(int)pymod(x, 64) * 64 + (int)pymod(y, 64)];
        if (!std::isfinite(field.field_8))
            continue;
        // Scan down from just above the surface for support with three blocks of air above.
        long long top = (long long)std::floor(field.field_8) + 8;
        for (long long z = top; z >= top - 32 && z >= field.base_z; --z) {
            if (!IsLakeSupport(NativeWaterBlock(world, LongVector3(x, y, z)).type))
                continue;
            if (NativeWaterBlock(world, LongVector3(x, y, z + 1)).type != cube::Block::Air ||
                NativeWaterBlock(world, LongVector3(x, y, z + 2)).type != cube::Block::Air ||
                NativeWaterBlock(world, LongVector3(x, y, z + 3)).type != cube::Block::Air)
                continue;
            // Require nearby lake water, not arbitrary hills inside the location radius.
            if (!NearLakeWater(world, x, y, z))
                continue;
            *position = LongVector3(x * cube::DOTS_PER_BLOCK + cube::DOTS_PER_BLOCK / 2,
                                    y * cube::DOTS_PER_BLOCK + cube::DOTS_PER_BLOCK / 2,
                                    (z + 1) * cube::DOTS_PER_BLOCK);
            return true;
        }
    }
    return false;
}

// True if the site lies in the ocean biome.
static bool OceanSite(cube::World *world, const LongVector3 &site) {
    auto x = pydiv(site.x, cube::DOTS_PER_BLOCK), y = pydiv(site.y, cube::DOTS_PER_BLOCK);
    if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX)
        return false;
    IntVector2 region{};
    ((void (*)(cube::World *, IntVector2 *, int, int))((char *)CWBase() + REGION_AT_RVA))(
        world, &region, (int)x, (int)y);
    return ((int (*)(cube::World *, int, int))((char *)CWBase() + BIOME_AT_RVA))(
               world, region.x, region.y) == OCEAN_BIOME;
}

// Appends one line to Mods\AquaticPopulation.log.
template <class... Args> static void AquaticLog(const char *format, Args... args) {
    FILE *file = nullptr;
    fopen_s(&file, "Mods\\AquaticPopulation.log", "a");
    if (file) {
        fprintf(file, format, args...);
        fclose(file);
    }
}

// --- Swimming geometry (SwimPoint is in blocks, LongVector3 in dots) ---------

static SwimPoint ToSwimPoint(const LongVector3 &p) {
    return SwimPoint{p.x / (double)cube::DOTS_PER_BLOCK, p.y / (double)cube::DOTS_PER_BLOCK,
                     p.z / (double)cube::DOTS_PER_BLOCK};
}
static LongVector3 ToDots(const SwimPoint &p) {
    return LongVector3((long long)std::llround(p.x * cube::DOTS_PER_BLOCK),
                       (long long)std::llround(p.y * cube::DOTS_PER_BLOCK),
                       (long long)std::llround(p.z * cube::DOTS_PER_BLOCK));
}
static LongVector3 BlockAt(const SwimPoint &p) {
    return LongVector3((long long)std::floor(p.x), (long long)std::floor(p.y),
                       (long long)std::floor(p.z));
}

// True if a fish body at p is fully in water: checks a body-sized corridor and
// overhead water; never generates unloaded zones.
static bool SwimWater(cube::World *world, SwimPoint p) {
    for (auto offset : {SwimPoint{}, SwimPoint{.45, 0, 0}, SwimPoint{-.45, 0, 0},
                        SwimPoint{0, .45, 0}, SwimPoint{0, -.45, 0}, SwimPoint{0, 0, 1}}) {
        LongVector3 block = BlockAt(SwimPoint{p.x + offset.x, p.y + offset.y, p.z + offset.z});
        if (!BlockLoaded(world, block.x, block.y) ||
            NativeWaterBlock(world, block).type != cube::Block::Water)
            return false;
    }
    return true;
}

// True if the block at p is loaded air or water (a fish may pass through it).
static bool SwimPassable(cube::World *world, SwimPoint p) {
    auto key = BlockAt(p);
    if (!BlockLoaded(world, key.x, key.y))
        return false;
    auto type = NativeWaterBlock(world, key).type;
    return type == cube::Block::Air || type == cube::Block::Water;
}

// Picks a reachable target 8-16 blocks away, roughly along the current heading
// (sometimes turning back), and pulls it toward home if it strays beyond 32 blocks.
// Shared school heading with small member spacing, constrained to nearby water.
static void PlanSwimTarget(cube::World *world, const SwimPoint &home, const SwimPoint &current,
                           float yaw, IdCreaturePair &pair) {
    auto hash = WaterPopulationHash((int)std::floor(home.x / 64), (int)std::floor(home.y / 64),
                                    world->seed, pair.swimAttempt++);
    for (unsigned trial = 0; trial < 4 && !pair.swimming; trial++) {
        double angle = yaw * SWIM_PI / 180 + HALF_PI + ((int)(hash % 121) - 60) * SWIM_PI / 180;
        if (hash % 5 == 0)
            angle += SWIM_PI; // Occasional turn back.
        angle += trial * HALF_PI;
        double reach = 8 + (hash % 9);
        SwimPoint target{current.x + reach * std::cos(angle), current.y + reach * std::sin(angle),
                         current.z};
        if (SwimDistance(target, home) > 32) {
            double distance = SwimDistance(current, home);
            double t = (std::min)(1., 16. / (std::max)(distance, .001));
            target = {current.x + (home.x - current.x) * t, current.y + (home.y - current.y) * t,
                      current.z};
        }
        if (SwimPathClear(current, target, [&](SwimPoint p) { return SwimWater(world, p); })) {
            pair.swimTarget = target;
            pair.swimming = true;
        }
    }
}

// Updates a group's entity slot after a bounds check. Returns false if the
// entity is gone and its slot should be forgotten; retires it if it should go.
static bool ApplyLifecycle(cube::Game *game, IdCreaturePair &pair, bool originLoaded) {
    auto resident = FindResidentWaterEntity(game->host.world.creatures, pair.creature, pair.id);
    bool currentLoaded =
        resident &&
        game->world->GetZone(cube::Zone::ZoneCoordsFromDots(resident->entity_data.position));
    auto action =
        WaterEntityLifecycle(resident != nullptr, resident && resident->entity_data.HP > 0.f,
                             originLoaded, currentLoaded, pair.retiring);
    if (action == WaterEntityAction::Forget)
        return false;
    pair.id = resident->id;
    if (action == WaterEntityAction::Retire) {
        pair.retiring = true;
        resident->entity_data.HP = 0.f;
        // Leave ownership/ID removal to the engine; keep this slot reserved.
    }
    return true;
}

const static bool DEBUG_DIVING_EVENT = false;

cube::DivingEvent::DivingEvent(bool *autoGoldUsage) : Event(), m_ItemEffectTimer(nullptr) {
    this->autoGoldUsage = autoGoldUsage;
    eventType = cube::EventType::Diving;

    m_SpawnTimer = cube::Timer(FISH_SPAWN_INTERVAL, m_CurrentTime);
    m_TreasureTimer = cube::Timer(TREASURE_SPAWN_INTERVAL, m_CurrentTime);
    m_BossTimer = cube::Timer(BOSS_SPAWN_INTERVAL, m_CurrentTime);
    m_BoundsTimer = cube::Timer(BOUNDS_CHECK_INTERVAL, m_CurrentTime);
}

cube::DivingEvent::~DivingEvent() {
    SetDiving(false);
    cube::Game *game = cube::GetGame();
    if (game == nullptr || game->host.running == false) {
        return;
    }

    auto map = &game->host.world.id_to_creature_map;
    auto list = &game->host.world.creatures;
    if (m_ItemEffectTimer != nullptr) {
        delete m_ItemEffectTimer;
    }

    // Kill everything we spawned that is still resident.
    for (auto p : m_SpawnedCreatures) {
        for (auto pair : p.creature_ids) {
            if (auto resident = FindResidentWaterEntity(*list, pair.creature, pair.id))
                resident->entity_data.HP = 0.f;
        }
    }

    for (auto pair : m_SpawnedTreasures) {
        auto creature = map->find(pair.id);
        if (creature != map->end() && creature->second == pair.creature) {
            creature->second->entity_data.HP = 0;
            map->erase(pair.id);
        }
    }

    for (auto pair : m_SpawnedBosses)
        if (auto resident = FindResidentWaterEntity(*list, pair.creature, pair.id))
            resident->entity_data.HP = 0.f;
}

// Forgets all spawned entities without touching them (their world is already gone).
void cube::DivingEvent::Abandon() {
    m_SpawnedCreatures.clear();
    m_SpawnedTreasures.clear();
    m_SpawnedBosses.clear();
    m_WaterZones.clear();
    m_DormantWaterZones.clear();
    m_LakeBossVisits.clear();
    m_NextWaterZone = 0;
}

/*
* Calculates the squared distance between two longvector3 points.
*
* @param	{const LongVector3&}	p1	Point one.
* @param	{const LongVector3&}	p2	Point two.
* @return	{long long}					Distance between the two points.
*/
static long long DistanceSquared(const LongVector3 &p1, const LongVector3 &p2) {
    auto diffx = (p2.x - p1.x);
    auto diffy = (p2.y - p1.y);
    auto diffz = (p2.z - p1.z);
    return diffx * diffx + diffy * diffy + diffz * diffz;
}

/*
* Is called every gametick to update the diving event.
* This includes handling all timers and their assigned actions.
*
* @return	{void}
*/
void cube::DivingEvent::Update() {
    Event::Update();
    unsigned int flags = cube::GetGame()->GetPlayer()->entity_data.flags;
    bool diving = (flags & (1 << (int)cube::Enums::CollisionFlags::Water)) &&
                  !(flags & (1 << (int)cube::Enums::CollisionFlags::Surfaced));
    if (diving != m_IsDiving) {
        SetDiving(diving);
        m_IsDiving = diving;
    }

    // Check for consumables
    //HandleItemEffectTimer();

    // Spawn treasures
    if (diving) {
        HandleTreasureTimer();
    } else {
        // Treasure rewards still require a full minute spent diving.
        m_TreasureTimer = cube::Timer(TREASURE_SPAWN_INTERVAL, m_CurrentTime);
    }

    // Spawn fishes
    HandleFishTimer();

    // Spawn bosses
    HandleBossTimer();

    // Check for fishes and treasures to be deleted
    HandleBoundsCheckTimer();

    UpdateIds();
    UpdateSwimming();
}

// ===========================================================================
// Swimming controller
// ===========================================================================

// Moves idle fish (and water bosses) around their school's home in water,
// recovers fish pushed out of the water, and leaves native AI in charge
// whenever the creature is moving by itself or fighting.
void cube::DivingEvent::UpdateSwimming() {
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - m_LastSwimTick).count();
    m_LastSwimTick = now;
    dt = (std::min)(.1, (std::max)(0., dt));
    m_SwimTime += dt;
    auto game = cube::GetGame();
    auto world = game->world;
    // Each water boss roams like a school of one around where it was first seen.
    // Work on copies, written back at the end.
    std::vector<SpawnMoment> bossGroups;
    std::vector<size_t> bossIndices;
    bossGroups.reserve(m_SpawnedBosses.size());
    for (size_t i = 0; i < m_SpawnedBosses.size(); i++) {
        auto &pair = m_SpawnedBosses[i];
        auto boss = FindResidentWaterEntity(game->host.world.creatures, pair.creature, pair.id);
        if (!boss || LakeEncounterRace(boss->entity_data.race))
            continue;
        if (!pair.hasRoamHome) {
            pair.roamHome = boss->entity_data.position;
            pair.hasRoamHome = true;
        }
        SpawnMoment group;
        group.position = pair.roamHome;
        group.creature_ids.push_back(pair);
        bossGroups.push_back(std::move(group));
        bossIndices.push_back(i);
    }
    std::vector<SpawnMoment *> movementGroups;
    for (auto &group : m_SpawnedCreatures)
        movementGroups.push_back(&group);
    for (auto &group : bossGroups)
        movementGroups.push_back(&group);
    // Snapshot which tracked creatures are still resident (one pass over the world list).
    std::vector<cube::Creature *> tracked;
    for (auto school : movementGroups)
        for (auto &pair : school->creature_ids)
            tracked.push_back(pair.creature);
    std::sort(tracked.begin(), tracked.end(), std::less<cube::Creature *>());
    Residents residents;
    for (auto creature : game->host.world.creatures)
        if (std::binary_search(tracked.begin(), tracked.end(), creature,
                               std::less<cube::Creature *>()))
            residents.push_back(creature);
    // Water checks and planning run at most every 0.1 s.
    bool check = m_SwimTime >= m_NextSwimCheck;
    if (check)
        m_NextSwimCheck = m_SwimTime + .1;
    // Planning has a fixed budget; ordinary movement does no path search.
    // Up to two plans per burst, one burst every 0.5 s.
    unsigned plans = check && m_SwimTime >= m_NextSwimPlanBurst ? 0 : 2;
    if (plans == 0)
        m_NextSwimPlanBurst = m_SwimTime + .5;
    for (auto group : movementGroups) {
        auto &school = *group;
        if (school.lakeRace)
            continue; // Native ground AI, no fish movement fallback.
        for (auto &pair : school.creature_ids) {
            auto creature = FindResidentWaterEntity(residents, pair.creature, pair.id);
            if (!creature || pair.retiring || creature->entity_data.HP <= 0)
                continue;
            SwimCreature(world, school, pair, creature, dt, check, plans);
        }
    }
    for (size_t i = 0; i < bossGroups.size(); i++)
        m_SpawnedBosses[bossIndices[i]] = bossGroups[i].creature_ids.front();
}

// One swimming-controller step for a single living creature.
void cube::DivingEvent::SwimCreature(cube::World *world, const SpawnMoment &school,
                                     IdCreaturePair &pair, cube::Creature *creature, double dt,
                                     bool check, unsigned &plans) {
    auto wet = [&](SwimPoint p) { return SwimWater(world, p); };
    auto &e = creature->entity_data;
    auto current = ToSwimPoint(e.position);
    // Native travel: away from where the controller left the actor, allowing for
    // one step of the velocity it reported (for the walk animation).
    auto offBy = [&](SwimPoint p) { return std::hypot(current.x - p.x, current.y - p.y) > .02; };
    SwimPoint drifted{pair.lastMotionPosition.x + pair.lastMotionDrift.x,
                      pair.lastMotionPosition.y + pair.lastMotionDrift.y,
                      pair.lastMotionPosition.z + pair.lastMotionDrift.z};
    bool nativeMoving = pair.hasMotionSample && offBy(pair.lastMotionPosition) && offBy(drifted);
    bool reportedMotion = pair.lastMotionDrift.x != 0 || pair.lastMotionDrift.y != 0 ||
                          pair.lastMotionDrift.z != 0;
    pair.lastMotionDrift = {};
    bool combat = e.current_ability != 0 || e.stun_time > 0;
    // Native idle vertical updates can push terrain-shifted aquatic actors
    // onto the water surface. Keep the controller's previous depth only during
    // idle swimming/recovery, never during horizontal native travel or combat.
    if (!combat && !nativeMoving && pair.hasMotionSample &&
        (pair.recoveringWater || pair.swimming || wet(pair.lastMotionPosition))) {
        current.z = pair.lastMotionPosition.z;
        e.position.z = (long long)std::llround(current.z * cube::DOTS_PER_BLOCK);
        e.velocity.z = 0;
        e.acceleration.z = 0;
    }
    // Native motion/combat owns the actor. Never correct depth during either.
    if (combat || nativeMoving) {
        pair.lastMotionPosition = current;
        pair.hasMotionSample = true;
        pair.swimming = false;
        pair.recoveringWater = false;
        pair.nextSwimPlan = m_SwimTime + 1;
        return;
    }
    pair.lastMotionPosition = current;
    pair.hasMotionSample = true;

    // Out of the water: once per creature, find water straight below and sink to it.
    if (check && !pair.waterRecoveryUsed && m_SwimTime >= pair.nextWaterRepair) {
        pair.nextWaterRepair = m_SwimTime + 2;
        if (!wet(current)) {
            auto passable = [&](SwimPoint p) { return SwimPassable(world, p); };
            if (SwimRecoverDepth(current, &pair.waterRecoveryTarget, passable, wet)) {
                pair.waterRecoveryUsed = true;
                pair.recoveringWater = true;
                pair.swimming = false;
            }
        }
    }
    if (pair.recoveringWater) {
        auto next = SwimStep(current, pair.waterRecoveryTarget, dt, .75);
        if (!SwimPassable(world, next)) {
            pair.recoveringWater = false;
            return;
        }
        e.position = ToDots(next);
        pair.lastMotionPosition = next;
        if (wet(next) || SwimDistance(next, pair.waterRecoveryTarget) < .1) {
            pair.recoveringWater = false;
            pair.nextSwimPlan = m_SwimTime + 1;
        }
        return;
    }

    // Arrived: pause briefly (staggered per creature) before the next leg.
    if (pair.swimming && SwimDistance(current, pair.swimTarget) < .2) {
        pair.swimming = false;
        pair.nextSwimPlan = m_SwimTime + .8 + (unsigned long long)pair.id % 17 * .1;
    }
    // Stop if the next 0.1 s of travel is no longer clear water.
    if (check && pair.swimming &&
        !SwimPathClear(current, SwimStep(current, pair.swimTarget, .1, 1.5), wet))
        pair.swimming = false;
    if (check && !pair.swimming && m_SwimTime >= pair.nextSwimPlan && plans < 2) {
        ++plans;
        pair.nextSwimPlan = m_SwimTime + 2;
        PlanSwimTarget(world, ToSwimPoint(school.position), current, e.yaw, pair);
    }
    if (!pair.swimming) {
        // A leg just ended: clear the motion it reported so the walk animation
        // stops. Only then, so native AI starting to move the actor is never undone.
        if (reportedMotion) {
            e.velocity = FloatVector3(0, 0, 0);
            e.acceleration = FloatVector3(0, 0, 0);
        }
        return;
    }

    // Turn toward the target (at most 100 degrees/s), then swim forward.
    double wanted = SwimFacingYaw(current, pair.swimTarget);
    double turn = std::remainder(wanted - e.yaw, 360.);
    e.yaw += (float)SwimClamp(turn, -100 * dt, 100 * dt);
    double remaining = std::remainder(wanted - e.yaw, 360.);
    // Turn first instead of sliding backwards or sideways while rotating.
    auto next = SwimStep(current, pair.swimTarget, dt,
                         (AquaticPassive(e.race) ? 1.1 : 1.5) * SwimForwardAmount(remaining));
    pair.lastMotionPosition = next;
    // The native walk/swim animation follows the actor's velocity and
    // acceleration, as when the AI chases a target; position-only steps left it
    // idle (sliding). The controller's position stays authoritative; this step's
    // motion is reported too, and one step of native integration of it is
    // expected (not mistaken for native travel next update).
    auto motion = SwimMotion(current, next, dt);
    e.velocity = FloatVector3((float)motion.x, (float)motion.y, (float)motion.z);
    e.acceleration = e.velocity;
    pair.lastMotionDrift = {motion.x * dt, motion.y * dt, motion.z * dt};
    e.position = ToDots(next);
}

void cube::DivingEvent::UpdateIds() {
    auto &residents = cube::GetGame()->host.world.creatures;
    // Missing map entries must not discard a still-resident entity.
    // Do not call CheckAndUpdateID here: it can re-register retired entities.
    // Fish identities are validated by the bounded swimming resident snapshot.
    // FindResidentWaterEntity requires the same ID, so assigning it back here
    // was a no-op that scanned the full creature list once for every fish/frame.
    for (auto &pair : m_SpawnedBosses)
        if (auto resident = FindResidentWaterEntity(residents, pair.creature, pair.id))
            pair.id = resident->id;
}

void cube::DivingEvent::SetDiving(bool diving) {
    if (diving) {
        // Stamina gain underwater.
        WriteByte((char *)CWBase() + 0x2E038D + 0x02, 0x58);
    } else {
        // Reset to stamina loss underwater
        WriteByte((char *)CWBase() + 0x2E038D + 0x02, 0x5C);
    }
}

/*
* Checks if the player still has the underwater breathing ability.
* If this is not the case, check if the player can gain the ability again.
*
* @return	{void}
*/
void cube::DivingEvent::HandleItemEffectTimer() {
    if (m_ItemEffectTimer != nullptr && m_ItemEffectTimer->IsTriggered(m_CurrentTime)) {
        delete m_ItemEffectTimer;
        m_ItemEffectTimer = nullptr;

        SetDiving(false);
    }

    if (m_ItemEffectTimer == nullptr) {
        ConsumeItem();
    }
}

/*
* Checks if the player can gain the underwater breathing ability for a defined amount of time.
*
* @return	{void}
*/
void cube::DivingEvent::ConsumeItem() {
    if (autoGoldUsage && !*autoGoldUsage) {
        return;
    }

    cube::Creature *player = cube::GetGame()->GetPlayer();
    if (player->gold >= 10) {
        player->gold -= 10;

        // Set timer
        m_ItemEffectTimer = new cube::Timer(10, m_CurrentTime);

        cube::GetGame()->PrintMessage(L"[Consumed] ", 100, 255, 0);
        cube::GetGame()->PrintMessage(L"10 Gold\n");
    }
}

// ===========================================================================
// Fish and lake-encounter population
// ===========================================================================

/*
* Handles the fish timer. If this timer is triggered, it will try to spawn fishes around the player.
*
* @return	{void}
*/
void cube::DivingEvent::HandleFishTimer() {
    LongVector3 pos = cube::GetGame()->GetPlayer()->entity_data.position;

    if (m_SpawnTimer.IsTriggered(m_CurrentTime) && m_SpawnedCreatures.size() < MAX_FISH_COUNT) {
        SpawnFishes(pos);
    }
}

// True if `creature` is one of our fish, lake creatures or bosses.
bool cube::DivingEvent::IsOwnSpawn(const cube::Creature *creature) const {
    for (auto &group : m_SpawnedCreatures)
        for (auto &pair : group.creature_ids)
            if (pair.creature == creature)
                return true;
    for (auto &pair : m_SpawnedBosses)
        if (pair.creature == creature)
            return true;
    return false;
}

/*
* Picks the region and level (stars, 0-4) for new spawns: those of the nearest
* native hostile creature (or nearest other non-player creature if there is no
* hostile), falling back to the player's region and a random level.
*/
void cube::DivingEvent::GetLocalEnemyProfile(const LongVector3 &position, IntVector2 *region,
                                             int *stars) {
    auto game = cube::GetGame();
    *region = game->GetPlayer()->entity_data.current_region;
    *stars = Helper::RandomInt() % 5;
    long double best = -1;
    bool foundHostile = false;
    for (auto candidate : game->host.world.creatures) {
        if (!candidate || candidate == game->GetPlayer() || candidate->entity_data.HP <= 0.f)
            continue;
        int behaviour = candidate->entity_data.hostility_type;
        if (behaviour == (int)cube::Enums::EntityBehaviour::Player ||
            behaviour == (int)cube::Enums::EntityBehaviour::Pet ||
            behaviour == (int)cube::Enums::EntityBehaviour::ExamineObject)
            continue;
        if (IsOwnSpawn(candidate))
            continue;
        bool hostile = behaviour == (int)cube::Enums::EntityBehaviour::Hostile;
        if (foundHostile && !hostile)
            continue;
        long double dx = (long double)candidate->entity_data.position.x - position.x;
        long double dy = (long double)candidate->entity_data.position.y - position.y;
        long double distance = dx * dx + dy * dy;
        if (best < 0 || (hostile && !foundHostile) || distance < best) {
            best = distance;
            foundHostile = hostile;
            *region = candidate->entity_data.current_region;
            *stars = std::max<int>(0, std::min<int>(4, candidate->entity_data.level));
        }
    }
}

// True if any living, non-retiring member of a school that originated in `zone` remains.
bool cube::DivingEvent::ZoneHasLivingFish(const IntVector2 &zone) {
    auto game = cube::GetGame();
    for (auto &group : m_SpawnedCreatures)
        if (cube::Zone::ZoneCoordsFromDots(group.position) == zone)
            for (auto &pair : group.creature_ids) {
                auto resident =
                    FindResidentWaterEntity(game->host.world.creatures, pair.creature, pair.id);
                if (resident && resident->entity_data.HP > 0 && !pair.retiring)
                    return true;
            }
    return false;
}

// True if any resident tracked creature originated in, or is currently in, `zone`.
bool cube::DivingEvent::ZoneOccupied(const IntVector2 &zone) {
    auto game = cube::GetGame();
    for (auto &group : m_SpawnedCreatures)
        for (auto &pair : group.creature_ids) {
            auto resident =
                FindResidentWaterEntity(game->host.world.creatures, pair.creature, pair.id);
            if (resident &&
                (cube::Zone::ZoneCoordsFromDots(group.position) == zone ||
                 cube::Zone::ZoneCoordsFromDots(resident->entity_data.position) == zone))
                return true;
        }
    return false;
}

// Synchronises m_WaterZones with the currently loaded nearby chunks (sorted).
// Unloaded sites whose population was wiped out keep their respawn cooldown
// in m_DormantWaterZones until it expires.
void cube::DivingEvent::RefreshWaterZones(const std::vector<IntVector2> &loaded) {
    m_WaterZones.erase(std::remove_if(m_WaterZones.begin(), m_WaterZones.end(),
                                      [&](const WaterZonePopulation &state) {
                                          if (std::find(loaded.begin(), loaded.end(), state.zone) !=
                                              loaded.end())
                                              return false;
                                          // Living populations reload normally; cleared sites retain their cooldown.
                                          if (state.populated && !ZoneHasLivingFish(state.zone)) {
                                              auto dormant = state;
                                              if (!dormant.emptySince)
                                                  dormant.emptySince = m_CurrentTime;
                                              m_DormantWaterZones.push_back(dormant);
                                          }
                                          return true;
                                      }),
                       m_WaterZones.end());
    for (auto zone : loaded) {
        bool known = false;
        for (auto &state : m_WaterZones)
            if (state.zone == zone)
                known = true;
        if (!known) {
            auto saved = std::find_if(m_DormantWaterZones.begin(), m_DormantWaterZones.end(),
                                      [&](const WaterZonePopulation &s) { return s.zone == zone; });
            WaterZonePopulation state;
            state.zone = zone;
            if (saved != m_DormantWaterZones.end()) {
                state = *saved;
                m_DormantWaterZones.erase(saved);
            }
            m_WaterZones.push_back(state);
        }
    }
    // Expired unloaded cooldowns need no retained runtime state.
    m_DormantWaterZones.erase(std::remove_if(m_DormantWaterZones.begin(), m_DormantWaterZones.end(),
                                             [&](const WaterZonePopulation &s) {
                                                 return WaterSiteCanRepopulate(m_CurrentTime,
                                                                               s.emptySince);
                                             }),
                              m_DormantWaterZones.end());
}

/*
* Populates nearby loaded water chunks: each eligible chunk gets one school of
* fish (or, inside a native lake, a group of hostile shore creatures and possibly
* a lake boss) at a deterministic site, and is repopulated only after its
* population has been gone for WaterSiteRespawnDelay.
*
* @param	{const LongVector3&}	position	Position of the player.
* @return	{void}
*/
void cube::DivingEvent::SpawnFishes(const LongVector3 &position) {
    auto game = cube::GetGame();
    auto world = game->world;
    std::vector<IntVector2> loaded;
    long long playerX = pydiv(position.x, cube::DOTS_PER_BLOCK),
              playerY = pydiv(position.y, cube::DOTS_PER_BLOCK);
    auto center = cube::Zone::ZoneCoordsFromDots(position);
    world->LockZones();
    for (int dx = -3; dx <= 3; dx++)
        for (int dy = -3; dy <= 3; dy++) {
            IntVector2 coords(center.x + dx, center.y + dy);
            auto entry = world->zones.find(coords);
            if (entry != world->zones.end() && entry->second &&
                WaterWithinRadius((long long)coords.x * 64 + 32, (long long)coords.y * 64 + 32,
                                  playerX, playerY, WaterSpawnRadius))
                loaded.push_back(coords);
        }
    world->UnlockZones();
    std::sort(loaded.begin(), loaded.end(), [](const IntVector2 &a, const IntVector2 &b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    RefreshWaterZones(loaded);
    if (m_WaterZones.empty())
        return;
    int total = 0;
    for (auto &group : m_SpawnedCreatures)
        total += (int)group.creature_ids.size();
    static unsigned spawnPass = 0;
    if (++spawnPass % 6 == 0)
        AquaticLog("Nearby water scan: loaded chunks=%zu tracked fish=%d/%d mode=fixed-sites\n",
                   m_WaterZones.size(), total, WaterPopulationCap);
    // Populate deterministic sites as chunks become available; no ambient spawn roll.
    // Round-robin over at most 12 chunks and create at most 4 groups per call.
    int schoolsCreated = 0;
    for (size_t probe = 0; probe < std::min<size_t>(12, m_WaterZones.size()); ++probe) {
        m_NextWaterZone %= m_WaterZones.size();
        auto &state = m_WaterZones[m_NextWaterZone++];
        if (ZoneOccupied(state.zone)) {
            state.emptySince = 0;
            continue;
        }
        if (state.populated) {
            if (!state.emptySince)
                state.emptySince = m_CurrentTime;
            if (!WaterSiteCanRepopulate(m_CurrentTime, state.emptySince))
                continue;
        }
        if (total > WaterPopulationCap - 2)
            continue;
        unsigned int hash = WaterPopulationHash(state.zone.x, state.zone.y, world->seed, 0);
        // Each chunk has a stable 4x4 lattice. Probe each site once, bounded per update.
        // A valid site is retained if the factory temporarily cannot create entities.
        if (!state.hasAnchor && state.siteProbe >= 16)
            continue;
        unsigned int siteProbe = state.hasAnchor ? 0 : state.siteProbe++;
        unsigned int slot = WaterSiteSlot(hash, siteProbe);
        long long blockX = state.hasAnchor
                               ? pydiv(state.anchor.x, cube::DOTS_PER_BLOCK)
                               : (long long)state.zone.x * 64 + WaterSiteCoordinate(slot, false);
        long long blockY = state.hasAnchor
                               ? pydiv(state.anchor.y, cube::DOTS_PER_BLOCK)
                               : (long long)state.zone.y * 64 + WaterSiteCoordinate(slot, true);
        LongVector3 spawn(blockX * cube::DOTS_PER_BLOCK + cube::DOTS_PER_BLOCK / 2,
                          blockY * cube::DOTS_PER_BLOCK + cube::DOTS_PER_BLOCK / 2, 0);

        bool ocean = OceanSite(world, spawn);
        if (!WaterSiteEligible(state.zone.x, state.zone.y, world->seed, ocean))
            continue;
        // Keep groups apart: 64 blocks at sea, 128 inland.
        bool crowded = false;
        for (auto &existing : m_SpawnedCreatures)
            if (!existing.creature_ids.empty() &&
                WaterWithinRadius(pydiv(existing.position.x, cube::DOTS_PER_BLOCK),
                                  pydiv(existing.position.y, cube::DOTS_PER_BLOCK), blockX, blockY,
                                  WaterSchoolSpacing(ocean))) {
                crowded = true;
                break;
            }
        if (crowded)
            continue;
        auto lake = LakeLocation(world, blockX, blockY);
        bool created = lake.type == LAKE_LOCATION_TYPE && lake.influence > 0
                           ? SpawnLakeEncounter(lake, spawn, total, hash)
                           : SpawnSchool(state, spawn, blockX, blockY, total, hash);
        if (!created)
            continue;
        state.populated = true;
        state.emptySince = 0;
        total += (int)m_SpawnedCreatures.back().creature_ids.size();
        if (++schoolsCreated >= 4)
            return; // Bound activation work without a spawn lottery.
    }
}

// Spawns a group of the lake's hostile shore race (plus, once per lake, a lake
// boss) on dry ground near `spawn`. Returns true if a group was created.
bool cube::DivingEvent::SpawnLakeEncounter(const NativeLakeLocation &lake, LongVector3 spawn,
                                           int total, unsigned int hash) {
    auto world = cube::GetGame()->world;
    int race = LakeDominantRace(WaterPopulationHash(lake.x, lake.y, world->seed, 0));
    if (!LakeGround(world, &spawn, lake))
        return false;
    IntVector2 region;
    int stars;
    GetLocalEnemyProfile(spawn, &region, &stars);
    SpawnMoment group;
    group.position = spawn;
    group.lakeRace = race;
    // Members stand 3 blocks apart along +x, each snapped to its own shore spot.
    std::vector<LongVector3> sites{spawn};
    for (int member = 1; member < WaterSchoolSize(total, hash); member++) {
        auto site = spawn;
        site.x += member * 3 * cube::DOTS_PER_BLOCK;
        if (!LakeGround(world, &site, lake))
            continue;
        if (std::find_if(sites.begin(), sites.end(), [&](const LongVector3 &p) {
                return p.x == site.x && p.y == site.y;
            }) == sites.end())
            sites.push_back(site);
    }
    for (auto site : sites) {
        auto creature = cube::CreatureFactory::SpawnCreature(
            site, region, race, (int)cube::Enums::EntityBehaviour::Hostile, stars);
        if (creature) {
            creature->entity_data.appearance.flags2 &=
                ~(1 << (int)cube::Enums::AppearanceModifiers::IsFriendly);
            creature->entity_data.HP = creature->GetMaxHP();
            group.creature_ids.push_back({creature->id, creature});
        }
    }
    if (group.creature_ids.empty())
        return false;
    m_SpawnedCreatures.push_back(std::move(group));
    IntVector2 key(lake.x, lake.y);
    if (m_SpawnedBosses.size() < MAX_BOSS_COUNT &&
        std::find(m_LakeBossVisits.begin(), m_LakeBossVisits.end(), key) ==
            m_LakeBossVisits.end()) {
        auto site = spawn;
        site.y += 6 * cube::DOTS_PER_BLOCK;
        if (LakeGround(world, &site, lake)) {
            auto boss = cube::CreatureFactory::SpawnBoss(site, region, stars, race);
            if (boss) {
                m_BossZone = cube::Zone::ZoneCoordsFromDots(site);
                m_SpawnedBosses.push_back({boss->id, boss});
                m_LakeBossVisits.push_back(key);
            }
        }
    }
    AquaticLog("Lake encounter: center=(%d,%d) hostile race=%d members=%zu\n", lake.x, lake.y, race,
               m_SpawnedCreatures.back().creature_ids.size());
    return true;
}

// Spawns a school of 2-3 fish of one species in the water column at
// (blockX, blockY), anchoring the site on first success. Returns true if created.
bool cube::DivingEvent::SpawnSchool(WaterZonePopulation &state, LongVector3 spawn, long long blockX,
                                    long long blockY, int total, unsigned int hash) {
    auto world = cube::GetGame()->world;
    if (state.hasAnchor)
        spawn = state.anchor;
    else {
        // Anchor to the actual loaded water surface.
        long long height;
        auto zone = world->GetZone(state.zone);
        if (!zone)
            return false;
        auto &field = zone->fields[(int)pymod(blockX, 64) * 64 + (int)pymod(blockY, 64)];
        long long top = std::isfinite(field.field_8)
                            ? (long long)std::floor(field.field_8) + 4
                            : field.base_z + (long long)field.blocks.size();
        if (!FindWaterSpawnHeight(std::max<long long>(0, top), &height, [&](long long z) {
                return z >= field.base_z &&
                       NativeWaterBlock(world, LongVector3(blockX, blockY, z)).type ==
                           cube::Block::Water;
            }))
            return false;
        // Water column below the surface (bounded); open ocean needs depth, so a
        // river inside an ocean region keeps inland species.
        long long column = 0;
        while (column < 128 &&
               NativeWaterBlock(world, LongVector3(blockX, blockY, height - column - 1)).type ==
                   cube::Block::Water)
            ++column;
        state.openOcean = OceanWater(OceanSite(world, spawn), column + 1);
        // Inland and half the ocean schools swim near the surface; the rest of
        // the ocean schools spread down through the column.
        height -= WaterSchoolDepth(hash, column + 1, state.openOcean);
        spawn.z = height * cube::DOTS_PER_BLOCK + cube::DOTS_PER_BLOCK / 2;
        state.anchor = spawn;
        state.hasAnchor = true;
    }
    IntVector2 region;
    int stars;
    GetLocalEnemyProfile(spawn, &region, &stars);
    int wanted = WaterSchoolSize(total, hash);
    std::vector<LongVector3> positions{spawn};
    auto isWater = [&](const LongVector3 &p) {
        return NativeWaterBlock(world, LongVector3(pydiv(p.x, cube::DOTS_PER_BLOCK),
                                                   pydiv(p.y, cube::DOTS_PER_BLOCK),
                                                   pydiv(p.z, cube::DOTS_PER_BLOCK)))
                   .type == cube::Block::Water;
    };
    // Validate a compact school before creating any entities. Keep every member
    // in the same loaded chunk, within three blocks horizontally and one vertically.
    for (unsigned int attempt = 0; attempt < 16 && positions.size() < wanted; ++attempt) {
        unsigned int offset = WaterPopulationHash(state.zone.x, state.zone.y, hash, attempt);
        int dx = (int)(offset % 7) - 3, dy = (int)((offset >> 8) % 7) - 3;
        if (dx == 0 && dy == 0)
            continue;
        LongVector3 member = spawn;
        member.x += dx * cube::DOTS_PER_BLOCK;
        member.y += dy * cube::DOTS_PER_BLOCK;
        member.z += ((int)((offset >> 16) % 3) - 1) * cube::DOTS_PER_BLOCK;
        if (!(cube::Zone::ZoneCoordsFromDots(member) == state.zone) ||
            !world->GetZone(state.zone) || !isWater(member))
            continue;
        LongVector3 above = member;
        above.z += cube::DOTS_PER_BLOCK;
        if (!isWater(above))
            continue;
        if (std::find_if(positions.begin(), positions.end(), [&](const LongVector3 &p) {
                return p.x == member.x && p.y == member.y;
            }) != positions.end())
            continue;
        positions.push_back(member);
    }
    if (positions.size() < 2)
        return false;
    int species = WaterSiteSpecies(hash, state.openOcean); // Factory index, once per group.
    SpawnMoment school;
    school.position = spawn;
    for (const auto &member : positions) {
        auto creature = cube::CreatureFactory::SpawnFish(member, region, species, stars, 0);
        if (creature)
            school.creature_ids.push_back({creature->id, creature});
    }
    if (school.creature_ids.empty())
        return false;
    m_SpawnedCreatures.push_back(std::move(school));
    AquaticLog("Spawned school: factory species=%d %s z=%lld members=%zu near chunk=(%d,%d)\n",
               species, state.openOcean ? "ocean" : "inland",
               (long long)pydiv(spawn.z, cube::DOTS_PER_BLOCK),
               m_SpawnedCreatures.back().creature_ids.size(), state.zone.x, state.zone.y);
    return true;
}

// ===========================================================================
// Treasure
// ===========================================================================

/*
* Handles the treasure timer. If this timer is triggered, spawn a treasure in the water.
*
* @return	{void}
*/
void cube::DivingEvent::HandleTreasureTimer() {
    if (m_TreasureTimer.IsTriggered(m_CurrentTime) &&
        m_SpawnedTreasures.size() < MAX_TREASURE_COUNT) {
        LongVector3 pos = cube::GetGame()->GetPlayer()->entity_data.position;
        SpawnTreasures(pos);
    }
}

/*
* Spawns a treasure somewhere depending on the player position.
*
* @param	{const LongVector3&}	position	Position of the player.
* @return	{void}
*/
void cube::DivingEvent::SpawnTreasures(const LongVector3 &position) {
    // Calculate spawn position
    long long distance = (2.f + 1.f * Helper::RandomZeroToOne()) * CREATURE_SPAWN_RANGE;
    LongVector3 offset = cube::CreatureFactory::GetRandomOffset(distance);
    offset.x += position.x;
    offset.y += position.y;
    offset.z = position.z - abs(offset.z);

    // Check if there is water in at the spawnposition
    if (!Helper::PositionContainsWater(offset)) {
        return;
    }

    int type = GenerateChestType();
    cube::Creature *creature = cube::CreatureFactory::SpawnChest(
        offset, cube::GetGame()->GetPlayer()->entity_data.current_region, type);

    if (creature != nullptr) {
        m_SpawnedTreasures.push_back({creature->id, creature});
        cube::GetGame()->PrintMessage(L"A treasure chest appeared!\n", 255, 165, 0);
    }
}

/*
* Generates a chest type to spawn. Certain chests are more likely to spawn than others.
*/
int cube::DivingEvent::GenerateChestType() {
    const static int NORMAL_CHANCE = 70;
    const static int SKULL_CHANCE = 20;
    const static int OBSEDIAN_CHANCE = 5;
    const static int BONE_CHANCE = 5;

    int random = rand() % 100;

    if (random < NORMAL_CHANCE) {
        return 0;
    } else if (random - NORMAL_CHANCE < SKULL_CHANCE) {
        return 1;
    } else if (random - NORMAL_CHANCE - SKULL_CHANCE < OBSEDIAN_CHANCE) {
        return 2;
    } else {
        return 3;
    }

    return 0;
}

// ===========================================================================
// Bosses
// ===========================================================================

void cube::DivingEvent::HandleBossTimer() {
    if (m_BossTimer.IsTriggered(m_CurrentTime) && m_SpawnedBosses.size() < MAX_BOSS_COUNT) {
        LongVector3 pos = cube::GetGame()->GetPlayer()->entity_data.position;
        SpawnBoss(pos);
    }
}

// Tries (once per boss interval, 1 in 4) to spawn a water boss near one of our
// fish schools, at most one attempt per water chunk.
void cube::DivingEvent::SpawnBoss(const LongVector3 &position) {
    if (m_SpawnedCreatures.empty())
        return;
    auto game = cube::GetGame();
    // Rare bosses use already populated water chunks, anywhere in loaded terrain.
    size_t start = Helper::RandomInt() % m_SpawnedCreatures.size();
    for (size_t probe = 0; probe < std::min<size_t>(8, m_SpawnedCreatures.size()); ++probe) {
        auto &group = m_SpawnedCreatures[(start + probe) % m_SpawnedCreatures.size()];
        if (group.lakeRace || group.creature_ids.empty() || group.creature_ids.front().retiring)
            continue;
        auto pair = group.creature_ids.front();
        auto entry = game->host.world.id_to_creature_map.find(pair.id);
        if (entry == game->host.world.id_to_creature_map.end() || entry->second != pair.creature)
            continue;
        LongVector3 spawn = entry->second->entity_data.position;
        LongVector3 offset = cube::CreatureFactory::GetRandomOffset(4 * cube::DOTS_PER_BLOCK);
        spawn.x += offset.x;
        spawn.y += offset.y;
        if (!Helper::FindWaterColumnSpawnPosition(&spawn))
            continue;
        auto zone = cube::Zone::ZoneCoordsFromDots(spawn);
        if (!game->world->GetZone(zone))
            continue;
        auto state = std::find_if(
            m_WaterZones.begin(), m_WaterZones.end(),
            [&](const WaterZonePopulation &candidate) { return candidate.zone == zone; });
        if (state == m_WaterZones.end() || state->boss_attempted)
            continue;
        state->boss_attempted = true;
        // Exactly one roll this interval; unsuccessful rolls cannot multiply across probes.
        if (!WaterBossRoll(Helper::RandomInt()))
            return;
        IntVector2 region;
        int stars;
        GetLocalEnemyProfile(spawn, &region, &stars);
        // Sea-crab bosses only in open ocean (deep water), never a river in an
        // ocean region: the chunk's anchored school already decided that.
        auto boss = cube::CreatureFactory::SpawnBoss(spawn, region, stars, -1,
                                                     state->hasAnchor && state->openOcean);
        if (!boss)
            return;
        m_BossZone = zone;
        m_SpawnedBosses.push_back({boss->id, boss});
        game->PrintMessage(L"An underwater boss appeared!\n", 255, 165, 0);
        return;
    }
}

// ===========================================================================
// Bounds checks (despawning)
// ===========================================================================

/*
* Handles the boundscheck timer. If this timer is triggered, it will remove all the treasures and fishes
* that are out of range of the player.
* This is done so that the amount of added creatures to the game stays minimal.
*
* @return	{void}
*/
void cube::DivingEvent::HandleBoundsCheckTimer() {
    if (m_BoundsTimer.IsTriggered(m_CurrentTime)) {
        LongVector3 pos = cube::GetGame()->GetPlayer()->entity_data.position;
        BoundCheckFishes(pos);
        BoundCheckTreasures(pos);
        BoundCheckBosses(pos);
    }
}

/*
* Retires fish whose school site or current chunk has unloaded, and forgets
* fish the engine has released. Empty schools are removed.
*
* @param	{const LongVector3&}	position	Position of the Player.
* @return	{void}
*/
void cube::DivingEvent::BoundCheckFishes(const LongVector3 &position) {
    auto game = cube::GetGame();
    for (auto group = m_SpawnedCreatures.begin(); group != m_SpawnedCreatures.end();) {
        bool originLoaded =
            game->world->GetZone(cube::Zone::ZoneCoordsFromDots(group->position)) != nullptr;
        for (auto pair = group->creature_ids.begin(); pair != group->creature_ids.end();) {
            if (ApplyLifecycle(game, *pair, originLoaded))
                ++pair;
            else
                pair = group->creature_ids.erase(pair);
        }
        if (group->creature_ids.empty())
            group = m_SpawnedCreatures.erase(group);
        else
            ++group;
    }
}

/*
* Checks if any spawned treasures are out of range of the player.
* If this is the case, said treasures are deleted.
*
* @param	{const LongVector3&}	position	Position of the Player.
* @return	{void}
*/
void cube::DivingEvent::BoundCheckTreasures(const LongVector3 &position) {
    const static auto distTreasure = (BOUNDS_CHECK_DIST_MULTIPLIER * 3 * CREATURE_SPAWN_RANGE) *
                                     (BOUNDS_CHECK_DIST_MULTIPLIER * 3 * CREATURE_SPAWN_RANGE);
    auto map = &cube::GetGame()->host.world.id_to_creature_map;

    std::vector<int> toBeErased;
    for (int i = 0; i < m_SpawnedTreasures.size(); i++) {
        auto pair = m_SpawnedTreasures.at(i);
        auto creature = map->find(pair.id);
        if (creature != map->end()) {
            if (DistanceSquared(creature->second->entity_data.position, position) < distTreasure) {
                continue;
            }
            creature->second->entity_data.HP = 0;
            map->erase(pair.id);

            toBeErased.push_back(i);
        } else {
            toBeErased.push_back(i);
        }
    }

    if (DEBUG_DIVING_EVENT && toBeErased.size() > 0) {
        wchar_t buffer[250];
        swprintf_s(buffer, 250, L"[Deleted treasure] %d\n", toBeErased.size());
        cube::GetGame()->PrintMessage(buffer, 255, 100, 175);
    }

    int cnt = 0;
    for (auto i : toBeErased) {
        m_SpawnedTreasures.erase(m_SpawnedTreasures.begin() + i - cnt);
        cnt++;
    }
}

// Retires bosses once their zone unloads or the player is more than
// WaterRetireRadius blocks from it; forgets bosses the engine has released.
void cube::DivingEvent::BoundCheckBosses(const LongVector3 &position) {
    auto game = cube::GetGame();
    long long px = pydiv(position.x, cube::DOTS_PER_BLOCK),
              py = pydiv(position.y, cube::DOTS_PER_BLOCK);
    bool originLoaded =
        game->world->GetZone(m_BossZone) != nullptr &&
        WaterWithinRadius((long long)m_BossZone.x * 64 + 32, (long long)m_BossZone.y * 64 + 32, px,
                          py, WaterRetireRadius);
    for (auto pair = m_SpawnedBosses.begin(); pair != m_SpawnedBosses.end();) {
        if (ApplyLifecycle(game, *pair, originLoaded))
            ++pair;
        else
            pair = m_SpawnedBosses.erase(pair);
    }
}
