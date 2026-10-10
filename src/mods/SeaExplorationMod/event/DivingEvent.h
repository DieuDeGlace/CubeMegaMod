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

struct IdCreaturePair
{
	__int64 id;
	cube::Creature* creature;
	bool retiring = false;
	SwimPoint swimTarget;
	bool swimming = false;
	unsigned int swimAttempt = 0;
	double nextSwimPlan = 0;
};

struct SpawnMoment
{
	LongVector3 position;
	int lakeRace = 0;
	std::vector<IdCreaturePair> creature_ids;
};

struct WaterZonePopulation
{
	IntVector2 zone;
	unsigned int siteProbe = 0;
	bool populated = false;
	time_t emptySince = 0;
	bool hasAnchor = false;
	LongVector3 anchor;
	time_t next_spawn = 0;
	bool boss_attempted = false;
};

static long long DistanceSquared(const LongVector3& p1, const LongVector3& p2);

namespace cube
{
	class DivingEvent : public Event
	{
	private:
		cube::Timer* m_ItemEffectTimer;

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
		std::vector<WaterZonePopulation> m_DormantWaterZones;
		size_t m_NextWaterZone = 0;
		std::chrono::steady_clock::time_point m_LastSwimTick = std::chrono::steady_clock::now();
		double m_SwimTime = 0;
		double m_NextSwimCheck = 0;
		double m_NextSwimPlanBurst = 0;

		bool* autoGoldUsage;
		bool m_IsDiving = false;

	public:
		DivingEvent(bool* autoGoldUsage);
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
		void SpawnFishes(const LongVector3& position);
		void GetLocalEnemyProfile(const LongVector3& position, IntVector2* region, int* stars);

		// Treasure logic
		void HandleTreasureTimer();
		void SpawnTreasures(const LongVector3& position);
		int GenerateChestType();

		// Boss logic
		void HandleBossTimer();
		void SpawnBoss(const LongVector3& position);

		// Deletion logic
		void HandleBoundsCheckTimer();
		void BoundCheckFishes(const LongVector3& position);
		void BoundCheckTreasures(const LongVector3& position);
		void BoundCheckBosses(const LongVector3& position);
	};
}
