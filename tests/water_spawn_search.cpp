#include "../src/cwsdk-extension/helper/WaterSpawnSearch.h"
#include "../src/cwsdk-extension/helper/WaterPopulationRules.h"
#include <cassert>
#include <cstdio>
#include <vector>

int main()
{
	long long z = 999;
	auto ocean = [](long long height) { return height >= -40 && height <= 0; };
	assert(FindWaterSpawnHeight(12, &z, ocean) && z == -1); // On shore/boat.
	assert(FindWaterSpawnHeight(-12, &z, ocean) && z == -12); // Already submerged.
	assert(FindWaterSpawnHeight(-50, &z, ocean) && z == -40); // Below seabed.
	int queries = 0;
	z = 999;
	assert(!FindWaterSpawnHeight(12, &z, [&](long long) { ++queries; return false; }));
	assert(z == 999 && queries <= 514); // Dry land, bounded work, untouched output.
	assert(!FindWaterSpawnHeight(0, &z, [](long long height) { return height == -5; }));
	assert(!FindWaterSpawnHeight(200, &z, ocean)); // Too far from water.
	assert(FindWaterSpawnHeight(127, &z, ocean) && z == -1); // Search boundary.
	assert(!FindWaterSpawnHeight(128, &z, ocean));
	assert(FindWaterSpawnHeight(0, &z, [](long long height) { return height >= 6 && height <= 9; }) && z == 6);
	assert(WaterPopulationCanSpawn(58, 0, 120, 120));
	assert(!WaterPopulationCanSpawn(60, 0, 120, 120));
	assert(!WaterPopulationCanSpawn(0, 2, 120, 120));
	assert(!WaterPopulationCanSpawn(0, 0, 119, 120));
	assert(WaterPopulationCanSpawn(0, 0, 120, 120));
	assert(!WaterPopulationCanSpawn(0, 1, 120, 120)); // Do not mix a new school into an occupied chunk.
	assert(!WaterPopulationCanSpawn(59, 0, 120, 120)); // No lone spawn at the cap boundary.
	assert(WaterSchoolSize(0, 0) == 2 && WaterSchoolSize(0, 1) == 3);
	assert(WaterSchoolSize(58, 1) == 2 && WaterSchoolSize(59, 0) == 0);
	for (int total = 0; total <= 60; ++total)
		for (unsigned int roll = 0; roll < 20; ++roll) {
			int count = WaterSchoolSize(total, roll);
			assert(count == 0 || (count >= 2 && count <= 3));
			assert(total + count <= 60);
		}
	assert(WaterPopulationHash(-2, 3, 42, 0) == WaterPopulationHash(-2, 3, 42, 0));
	assert(WaterPopulationHash(-2, 3, 42, 0) != WaterPopulationHash(-2, 3, 42, 1));
	assert(WaterPopulationHash(-2, 3, 42, 0) != WaterPopulationHash(-2, 3, 43, 0));
	for (unsigned int attempt = 0; attempt < 200; ++attempt) {
		unsigned int hash = WaterPopulationHash(-2, -3, 42, attempt);
		int x = 4 + hash % 56, y = 4 + (hash >> 16) % 56;
		assert(x >= 4 && x < 60 && y >= 4 && y < 60);
	}
	assert(WaterEntityLifecycle(true, true, true, true, false) == WaterEntityAction::Keep);
	assert(WaterEntityLifecycle(true, true, false, true, false) == WaterEntityAction::Retire); // Origin unloaded, migrated entity still loaded.
	assert(WaterEntityLifecycle(true, true, true, false, false) == WaterEntityAction::Retire);
	assert(WaterEntityLifecycle(true, false, true, true, false) == WaterEntityAction::Retire);
	assert(WaterEntityLifecycle(true, true, true, true, true) == WaterEntityAction::Retire); // Reload cannot revive a retired entity.
	assert(WaterEntityLifecycle(false, true, true, true, false) == WaterEntityAction::Forget);
	struct Entity { int id; } owned{10}, unrelated{10};
	std::vector<Entity*> residents{&owned, &unrelated};
	assert(FindResidentWaterEntity(residents, &owned, 10) == &owned); // Reused ID does not select unrelated entity.
	owned.id = 20;
	assert(FindResidentWaterEntity(residents, &owned, 10) == nullptr); // Reused memory with a new ID must not be retired.
	assert(FindResidentWaterEntity(residents, &owned, 20) == &owned);
	residents.erase(residents.begin());
	assert(FindResidentWaterEntity(residents, &owned, 20) == nullptr);
	int successes = 0;
	for (unsigned int roll = 0; roll < 100; ++roll) successes += WaterBossRoll(roll);
	assert(successes == 25);
	int tracked = 60;
	for (int tick = 0; tick < 1000; ++tick) {
		// Missing map/death/unload cannot free slots while the entities remain resident.
		assert(WaterEntityLifecycle(true, false, false, false, true) != WaterEntityAction::Forget);
		assert(!WaterPopulationCanSpawn(tracked, 0, tick, 0));
	}
	tracked -= 2; // Two freed slots allow a complete school.
	assert(WaterPopulationCanSpawn(tracked, 0, 1000, 0));
	std::puts("Water placement, caps, chunk retirement, identity retention and rare boss rolls passed.");
}
