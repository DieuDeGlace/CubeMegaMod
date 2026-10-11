#include "DivingEvent.h"
#include "../../../cwsdk-extension/helper/WaterPopulationRules.h"
#include <algorithm>
#include "../../../cwsdk-extension/helper/NativeWaterBlock.h"
#include "../../../cwsdk-extension/helper/WaterSpawnSearch.h"
#include "common/math.h"

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
        (char *)CWBase() + 0x35ce0)(world, (int)x, (int)y, &result);
    return result;
}
// Ground creatures use dry shoreline/support rather than fish swim positions.
// A fixed 17-column budget and loaded-zone checks bound every search.
static bool LakeGround(cube::World *world, LongVector3 *position, const NativeLakeLocation &lake) {
    long long bx = pydiv(position->x, cube::DOTS_PER_BLOCK),
              by = pydiv(position->y, cube::DOTS_PER_BLOCK);
    for (int probe = 0; probe < 17; probe++) {
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
        if (location.type != 13 || location.influence <= 0 || location.x != lake.x ||
            location.y != lake.y)
            continue;
        auto &field = zone->fields[(int)pymod(x, 64) * 64 + (int)pymod(y, 64)];
        if (!std::isfinite(field.field_8))
            continue;
        long long top = (long long)std::floor(field.field_8) + 8;
        for (long long z = top; z >= top - 32 && z >= field.base_z; --z) {
            auto type = NativeWaterBlock(world, LongVector3(x, y, z)).type;
            if (type != cube::Block::Solid && type != cube::Block::Grass &&
                type != cube::Block::Ground && type != cube::Block::Snow &&
                type != cube::Block::Path)
                continue;
            if (NativeWaterBlock(world, LongVector3(x, y, z + 1)).type != cube::Block::Air ||
                NativeWaterBlock(world, LongVector3(x, y, z + 2)).type != cube::Block::Air ||
                NativeWaterBlock(world, LongVector3(x, y, z + 3)).type != cube::Block::Air)
                continue;
            // Require nearby lake water, not arbitrary hills inside the location radius.
            bool water = false;
            for (int dx = -4; dx <= 4 && !water; dx += 2)
                for (int dy = -4; dy <= 4 && !water; dy += 2) {
                    if (!world->GetZone(cube::Zone::ZoneCoordsFromBlocks(x + dx, y + dy)))
                        continue;
                    for (int dz = -3; dz <= 1; dz++)
                        if (NativeWaterBlock(world, LongVector3(x + dx, y + dy, z + dz)).type ==
                            cube::Block::Water) {
                            water = true;
                            break;
                        }
                }
            if (!water)
                continue;
            *position = LongVector3(x * cube::DOTS_PER_BLOCK + cube::DOTS_PER_BLOCK / 2,
                                    y * cube::DOTS_PER_BLOCK + cube::DOTS_PER_BLOCK / 2,
                                    (z + 1) * cube::DOTS_PER_BLOCK);
            return true;
        }
    }
    return false;
}

static bool OceanSite(cube::World *world, const LongVector3 &site) {
    auto x = pydiv(site.x, cube::DOTS_PER_BLOCK), y = pydiv(site.y, cube::DOTS_PER_BLOCK);
    if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX)
        return false;
    IntVector2 region{};
    ((void (*)(cube::World *, IntVector2 *, int, int))((char *)CWBase() + 0x2ada20))(
        world, &region, (int)x, (int)y);
    return ((int (*)(cube::World *, int, int))((char *)CWBase() + 0x2b9080))(world, region.x,
                                                                             region.y) == 1;
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

void cube::DivingEvent::UpdateSwimming() {
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - m_LastSwimTick).count();
    m_LastSwimTick = now;
    dt = (std::min)(.1, (std::max)(0., dt));
    m_SwimTime += dt;
    auto game = cube::GetGame();
    auto world = game->world;
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
    std::vector<cube::Creature *> tracked;
    for (auto school : movementGroups)
        for (auto &pair : school->creature_ids)
            tracked.push_back(pair.creature);
    std::sort(tracked.begin(), tracked.end(), std::less<cube::Creature *>());
    std::vector<cube::Creature *> residents;
    for (auto creature : game->host.world.creatures)
        if (std::binary_search(tracked.begin(), tracked.end(), creature,
                               std::less<cube::Creature *>()))
            residents.push_back(creature);
    bool check = m_SwimTime >= m_NextSwimCheck;
    if (check)
        m_NextSwimCheck = m_SwimTime + .1;
    auto point = [](const LongVector3 &p) {
        return SwimPoint{p.x / (double)cube::DOTS_PER_BLOCK, p.y / (double)cube::DOTS_PER_BLOCK,
                         p.z / (double)cube::DOTS_PER_BLOCK};
    };
    auto wet = [&](SwimPoint p) {
        // Check a body-sized corridor and overhead water; never generate unloaded zones.
        for (auto offset : {SwimPoint{}, SwimPoint{.45, 0, 0}, SwimPoint{-.45, 0, 0},
                            SwimPoint{0, .45, 0}, SwimPoint{0, -.45, 0}, SwimPoint{0, 0, 1}}) {
            LongVector3 block((long long)std::floor(p.x + offset.x),
                              (long long)std::floor(p.y + offset.y),
                              (long long)std::floor(p.z + offset.z));
            if (!world->GetZone(cube::Zone::ZoneCoordsFromBlocks(block.x, block.y)) ||
                NativeWaterBlock(world, block).type != cube::Block::Water)
                return false;
        }
        return true;
    };
    // Planning has a fixed budget; ordinary movement does no path search.
    unsigned plans = check && m_SwimTime >= m_NextSwimPlanBurst ? 0 : 2;
    if (plans == 0)
        m_NextSwimPlanBurst = m_SwimTime + .5;
    for (auto group : movementGroups) {
        auto &school = *group;
        if (school.lakeRace)
            continue; // Native ground AI, no fish movement fallback.
        unsigned member = 0;
        for (auto &pair : school.creature_ids) {
            ++member;
            auto creature = FindResidentWaterEntity(residents, pair.creature, pair.id);
            if (!creature || pair.retiring || creature->entity_data.HP <= 0)
                continue;
            auto &e = creature->entity_data;
            auto current = point(e.position);
            bool nativeMoving =
                pair.hasMotionSample && std::hypot(current.x - pair.lastMotionPosition.x,
                                                   current.y - pair.lastMotionPosition.y) > .02;
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
                continue;
            }
            pair.lastMotionPosition = current;
            pair.hasMotionSample = true;
            if (check && !pair.waterRecoveryUsed && m_SwimTime >= pair.nextWaterRepair) {
                pair.nextWaterRepair = m_SwimTime + 2;
                if (!wet(current)) {
                    auto passable = [&](SwimPoint p) {
                        auto key =
                            LongVector3((long long)std::floor(p.x), (long long)std::floor(p.y),
                                        (long long)std::floor(p.z));
                        if (!world->GetZone(cube::Zone::ZoneCoordsFromBlocks(key.x, key.y)))
                            return false;
                        auto type = NativeWaterBlock(world, key).type;
                        return type == cube::Block::Air || type == cube::Block::Water;
                    };
                    if (SwimRecoverDepth(current, &pair.waterRecoveryTarget, passable, wet)) {
                        pair.waterRecoveryUsed = true;
                        pair.recoveringWater = true;
                        pair.swimming = false;
                    }
                }
            }
            if (pair.recoveringWater) {
                auto next = SwimStep(current, pair.waterRecoveryTarget, dt, .75);
                auto key = LongVector3((long long)std::floor(next.x), (long long)std::floor(next.y),
                                       (long long)std::floor(next.z));
                if (!world->GetZone(cube::Zone::ZoneCoordsFromBlocks(key.x, key.y))) {
                    pair.recoveringWater = false;
                    continue;
                }
                auto type = NativeWaterBlock(world, key).type;
                if (type != cube::Block::Air && type != cube::Block::Water) {
                    pair.recoveringWater = false;
                    continue;
                }
                e.position = LongVector3((long long)std::llround(next.x * cube::DOTS_PER_BLOCK),
                                         (long long)std::llround(next.y * cube::DOTS_PER_BLOCK),
                                         (long long)std::llround(next.z * cube::DOTS_PER_BLOCK));
                pair.lastMotionPosition = next;
                if (wet(next) || SwimDistance(next, pair.waterRecoveryTarget) < .1) {
                    pair.recoveringWater = false;
                    pair.nextSwimPlan = m_SwimTime + 1;
                }
                continue;
            }
            if (pair.swimming && SwimDistance(current, pair.swimTarget) < .2) {
                pair.swimming = false;
                pair.nextSwimPlan = m_SwimTime + .8 + (unsigned long long)pair.id % 17 * .1;
            }
            if (check && pair.swimming &&
                !SwimPathClear(current, SwimStep(current, pair.swimTarget, .1, 1.5), wet))
                pair.swimming = false;
            if (check && !pair.swimming && m_SwimTime >= pair.nextSwimPlan && plans < 2) {
                ++plans;
                pair.nextSwimPlan = m_SwimTime + 2;
                auto home = point(school.position);
                auto hash =
                    WaterPopulationHash((int)std::floor(home.x / 64), (int)std::floor(home.y / 64),
                                        world->seed, pair.swimAttempt++);
                // Shared school heading with small member spacing, constrained to nearby water.
                for (unsigned trial = 0; trial < 4 && !pair.swimming; trial++) {
                    double angle = e.yaw * 3.141592653589793 / 180 + 1.570796326794897 +
                                   ((int)(hash % 121) - 60) * 3.141592653589793 / 180;
                    if (hash % 5 == 0)
                        angle += 3.141592653589793; // Occasional turn back.
                    angle += trial * 1.570796326794897;
                    double reach = 8 + (hash % 9);
                    SwimPoint target{current.x + reach * std::cos(angle),
                                     current.y + reach * std::sin(angle), current.z};
                    if (SwimDistance(target, home) > 32) {
                        double distance = SwimDistance(current, home);
                        double t = (std::min)(1., 16. / (std::max)(distance, .001));
                        target = {current.x + (home.x - current.x) * t,
                                  current.y + (home.y - current.y) * t, current.z};
                    }
                    if (SwimPathClear(current, target, wet)) {
                        pair.swimTarget = target;
                        pair.swimming = true;
                    }
                }
            }
            if (!pair.swimming)
                continue;
            double wanted = SwimFacingYaw(current, pair.swimTarget);
            double turn = std::remainder(wanted - e.yaw, 360.);
            e.yaw += (float)SwimClamp(turn, -100 * dt, 100 * dt);
            double remaining = std::remainder(wanted - e.yaw, 360.);
            // Turn first instead of sliding backwards or sideways while rotating.
            auto next =
                SwimStep(current, pair.swimTarget, dt,
                         (AquaticPassive(e.race) ? 1.1 : 1.5) * SwimForwardAmount(remaining));
            pair.lastMotionPosition = next;
            e.position = LongVector3((long long)std::llround(next.x * cube::DOTS_PER_BLOCK),
                                     (long long)std::llround(next.y * cube::DOTS_PER_BLOCK),
                                     (long long)std::llround(next.z * cube::DOTS_PER_BLOCK));
        }
    }
    for (size_t i = 0; i < bossGroups.size(); i++)
        m_SpawnedBosses[bossIndices[i]] = bossGroups[i].creature_ids.front();
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

/*
* Spawns fishes around the player.
*
* @param	{const LongVector3&}	position	Position of the player.
* @return	{void}
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
        bool ours = false;
        for (auto &group : m_SpawnedCreatures)
            for (auto &pair : group.creature_ids)
                if (pair.creature == candidate)
                    ours = true;
        for (auto &pair : m_SpawnedBosses)
            if (pair.creature == candidate)
                ours = true;
        if (ours)
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
    m_WaterZones.erase(
        std::remove_if(m_WaterZones.begin(), m_WaterZones.end(),
                       [&](const WaterZonePopulation &state) {
                           if (std::find(loaded.begin(), loaded.end(), state.zone) != loaded.end())
                               return false;
                           if (state.populated) {
                               bool alive = false;
                               for (auto &group : m_SpawnedCreatures)
                                   if (cube::Zone::ZoneCoordsFromDots(group.position) == state.zone)
                                       for (auto &pair : group.creature_ids) {
                                           auto resident = FindResidentWaterEntity(
                                               game->host.world.creatures, pair.creature, pair.id);
                                           if (resident && resident->entity_data.HP > 0 &&
                                               !pair.retiring)
                                               alive = true;
                                       }
                               // Living populations reload normally; cleared sites retain their cooldown.
                               if (!alive) {
                                   auto dormant = state;
                                   if (!dormant.emptySince)
                                       dormant.emptySince = m_CurrentTime;
                                   m_DormantWaterZones.push_back(dormant);
                               }
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
    if (m_WaterZones.empty())
        return;
    int total = 0;
    for (auto &group : m_SpawnedCreatures)
        total += (int)group.creature_ids.size();
    static unsigned spawnPass = 0;
    if (++spawnPass % 6 == 0) {
        FILE *file = nullptr;
        fopen_s(&file, "Mods\\AquaticPopulation.log", "a");
        if (file) {
            fprintf(file,
                    "Nearby water scan: loaded chunks=%zu tracked fish=%d/%d mode=fixed-sites\n",
                    m_WaterZones.size(), total, WaterPopulationCap);
            fclose(file);
        }
    }
    // Populate deterministic sites as chunks become available; no ambient spawn roll.
    int schoolsCreated = 0;
    for (size_t probe = 0; probe < std::min<size_t>(12, m_WaterZones.size()); ++probe) {
        m_NextWaterZone %= m_WaterZones.size();
        auto &state = m_WaterZones[m_NextWaterZone++];
        int local = 0;
        for (auto &group : m_SpawnedCreatures)
            for (auto &pair : group.creature_ids) {
                auto resident =
                    FindResidentWaterEntity(game->host.world.creatures, pair.creature, pair.id);
                if (resident &&
                    (cube::Zone::ZoneCoordsFromDots(group.position) == state.zone ||
                     cube::Zone::ZoneCoordsFromDots(resident->entity_data.position) == state.zone))
                    ++local;
            }
        if (local != 0) {
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
        bool crowded = false;
        for (auto &existing : m_SpawnedCreatures)
            if (!existing.creature_ids.empty() &&
                WaterWithinRadius(pydiv(existing.position.x, cube::DOTS_PER_BLOCK),
                                  pydiv(existing.position.y, cube::DOTS_PER_BLOCK), blockX, blockY,
                                  ocean ? 96 : 128)) {
                crowded = true;
                break;
            }
        if (crowded)
            continue;
        auto lake = LakeLocation(world, blockX, blockY);
        if (lake.type == 13 && lake.influence > 0) {
            int race = LakeDominantRace(WaterPopulationHash(lake.x, lake.y, world->seed, 0));
            if (!LakeGround(world, &spawn, lake))
                continue;
            IntVector2 region;
            int stars;
            GetLocalEnemyProfile(spawn, &region, &stars);
            SpawnMoment group;
            group.position = spawn;
            group.lakeRace = race;
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
                continue;
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
            FILE *file = nullptr;
            fopen_s(&file, "Mods\\AquaticPopulation.log", "a");
            if (file) {
                fprintf(file, "Lake encounter: center=(%d,%d) hostile race=%d members=%zu\n",
                        lake.x, lake.y, race, m_SpawnedCreatures.back().creature_ids.size());
                fclose(file);
            }
            state.populated = true;
            state.emptySince = 0;
            total += (int)m_SpawnedCreatures.back().creature_ids.size();
            if (++schoolsCreated >= 4)
                return;
            continue;
        }
        if (state.hasAnchor)
            spawn = state.anchor;
        else {
            // Anchor to the actual loaded water surface.
            long long height;
            auto zone = world->GetZone(state.zone);
            if (!zone)
                continue;
            auto &field = zone->fields[(int)pymod(blockX, 64) * 64 + (int)pymod(blockY, 64)];
            long long top = std::isfinite(field.field_8)
                                ? (long long)std::floor(field.field_8) + 4
                                : field.base_z + (long long)field.blocks.size();
            if (!FindWaterSpawnHeight(std::max<long long>(0, top), &height, [&](long long z) {
                    return z >= field.base_z &&
                           NativeWaterBlock(world, LongVector3(blockX, blockY, z)).type ==
                               cube::Block::Water;
                }))
                continue;
            long long depth = 2 + (hash % 3);
            if (NativeWaterBlock(world, LongVector3(blockX, blockY, height - depth)).type ==
                cube::Block::Water)
                height -= depth;
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
            continue;
        int species = WaterSiteSpecies(
            hash, OceanSite(world, spawn)); // Factory index, chosen once for the whole group.
        SpawnMoment school;
        school.position = spawn;
        for (const auto &member : positions) {
            auto creature = cube::CreatureFactory::SpawnFish(member, region, species, stars, 0);
            if (creature)
                school.creature_ids.push_back({creature->id, creature});
        }
        if (school.creature_ids.empty())
            continue;
        m_SpawnedCreatures.push_back(std::move(school));
        {
            FILE *file = nullptr;
            fopen_s(&file, "Mods\\AquaticPopulation.log", "a");
            if (file) {
                fprintf(file, "Spawned school: factory species=%d members=%zu near chunk=(%d,%d)\n",
                        species, m_SpawnedCreatures.back().creature_ids.size(), state.zone.x,
                        state.zone.y);
                fclose(file);
            }
        }
        state.populated = true;
        state.emptySince = 0;
        total += (int)m_SpawnedCreatures.back().creature_ids.size();
        if (++schoolsCreated >= 4)
            return; // Bound activation work without a spawn lottery.
    }
}

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

void cube::DivingEvent::HandleBossTimer() {
    if (m_BossTimer.IsTriggered(m_CurrentTime) && m_SpawnedBosses.size() < MAX_BOSS_COUNT) {
        LongVector3 pos = cube::GetGame()->GetPlayer()->entity_data.position;
        SpawnBoss(pos);
    }
}

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
        auto boss = cube::CreatureFactory::SpawnBoss(spawn, region, stars, -1,
                                                     OceanSite(game->world, spawn));
        if (!boss)
            return;
        m_BossZone = zone;
        m_SpawnedBosses.push_back({boss->id, boss});
        game->PrintMessage(L"An underwater boss appeared!\n", 255, 165, 0);
        return;
    }
}

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
* Checks if any spawned fishes are out of range of the player.
* If this is the case, said fishes are deleted.
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
            auto resident =
                FindResidentWaterEntity(game->host.world.creatures, pair->creature, pair->id);
            bool currentLoaded = resident && game->world->GetZone(cube::Zone::ZoneCoordsFromDots(
                                                 resident->entity_data.position));
            auto action = WaterEntityLifecycle(resident != nullptr,
                                               resident && resident->entity_data.HP > 0.f,
                                               originLoaded, currentLoaded, pair->retiring);
            if (action == WaterEntityAction::Forget) {
                pair = group->creature_ids.erase(pair);
                continue;
            }
            pair->id = resident->id;
            if (action == WaterEntityAction::Retire) {
                pair->retiring = true;
                resident->entity_data.HP = 0.f;
                // Leave ownership/ID removal to the engine; keep this slot reserved.
            }
            ++pair;
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

void cube::DivingEvent::BoundCheckBosses(const LongVector3 &position) {
    auto game = cube::GetGame();
    long long px = pydiv(position.x, cube::DOTS_PER_BLOCK),
              py = pydiv(position.y, cube::DOTS_PER_BLOCK);
    bool originLoaded =
        game->world->GetZone(m_BossZone) != nullptr &&
        WaterWithinRadius((long long)m_BossZone.x * 64 + 32, (long long)m_BossZone.y * 64 + 32, px,
                          py, WaterRetireRadius);
    for (auto pair = m_SpawnedBosses.begin(); pair != m_SpawnedBosses.end();) {
        auto resident =
            FindResidentWaterEntity(game->host.world.creatures, pair->creature, pair->id);
        bool currentLoaded =
            resident &&
            game->world->GetZone(cube::Zone::ZoneCoordsFromDots(resident->entity_data.position));
        auto action =
            WaterEntityLifecycle(resident != nullptr, resident && resident->entity_data.HP > 0.f,
                                 originLoaded, currentLoaded, pair->retiring);
        if (action == WaterEntityAction::Forget) {
            pair = m_SpawnedBosses.erase(pair);
            continue;
        }
        pair->id = resident->id;
        if (action == WaterEntityAction::Retire) {
            pair->retiring = true;
            resident->entity_data.HP = 0.f;
        }
        ++pair;
    }
}
