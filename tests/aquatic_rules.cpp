#include "../src/cwsdk-extension/helper/AquaticRules.h"
#include "../src/cwsdk-extension/helper/WaterPopulationRules.h"
#include <cassert>
#include <cstdio>
int main() {
    assert(std::abs(std::remainder(SwimFacingYaw({0, 0, 0}, {0, 1, 0}), 360.)) < 1e-8);
    assert(std::abs(SwimFacingYaw({0, 0, 0}, {1, 0, 0}) - 270) < 1e-8);
    assert(SwimForwardAmount(180) == 0 && SwimForwardAmount(90) == 0);
    assert(std::abs(SwimForwardAmount(0) - 1) < 1e-8);

    // Depth recovery advances at a bounded swim speed, never in a whole-depth jump.
    auto recovery = SwimStep(SwimPoint{0, 0, 8}, SwimPoint{0, 0, 0}, .1, .75);
    assert(std::abs(recovery.z - 7.925) < 1e-8);
    assert(SwimDistance(SwimPoint{0, 0, 8}, recovery) <= .07500001);

    for (unsigned h = 0; h < 600; h++) {
        int r = LakeDominantRace(h);
        assert(LakeEncounterRace(r));
        assert(!AquaticPassive(r));
        assert(r == LakeDominantRace(h));
    }
    assert(LakeDominantRace(0) == 106 && LakeDominantRace(1) == 86 && LakeDominantRace(2) == 37);
    for (int r : {75, 107, 145, 150, 31})
        assert(!LakeEncounterRace(r));
    for (int id : {145, 146, 147, 152, 155, 295})
        assert(AquaticPassive(id));
    for (int id : {75, 106, 107, 148, 149, 150, 153, 154})
        assert(!AquaticPassive(id));
    for (auto boss : AquaticBosses) {
        assert(!AquaticPassive(boss.race));
        assert(boss.race == 75 || boss.race == 150 || boss.race == 153 || boss.race == 154 ||
               boss.race == 107);
        assert(boss.scale <= 2.5);
    }
    SwimPoint a{-100000000.25, -17000000.75, 25}, b{a.x + 8, a.y + 6, 27};
    auto next = SwimStep(a, b, .016, 1.3);
    assert(SwimDistance(a, next) <= 1.3 * .016 + 1e-7);
    assert(SwimDistance(a, SwimStep(a, b, 10, 1.3)) <= .13 + 1e-7);
    assert(SwimDistance(b, SwimStep(a, b, .1, 1000)) < 1e-8);
    assert(SwimPathClear(a, b, [](SwimPoint) { return true; }));
    assert(!SwimPathClear(a, b, [&](SwimPoint p) { return std::abs(p.x - (a.x + 4)) > .3; }));
    assert(!SwimPathClear(a, {a.x + 30, a.y, a.z}, [](SwimPoint) { return true; }));
    assert(!SwimPathClear(a, b, [&](SwimPoint p) { return p.z < 26; }));
    assert(WaterPopulationCap == 24 && WaterSchoolMax == 3);
    assert(WaterWithinRadius(-1000000, -1000000, -1000000, -1000000, 192));
    assert(!WaterWithinRadius(-999000, -1000000, -1000000, -1000000, 192));
    assert(WaterWithinRadius(192, 0, 0, 0, 192) && !WaterWithinRadius(193, 0, 0, 0, 192));
    assert(WaterEntityLifecycle(true, true, false, true, false) == WaterEntityAction::Retire);
    assert(!WaterPopulationCanSpawn(60, 0, 100, 0));
    SwimPoint recovered{};
    assert(SwimRecoverDepth(
        {0, 0, 5}, &recovered, [](SwimPoint) { return true; },
        [](SwimPoint p) { return p.z <= 1; }));
    assert(recovered.z == 1);
    assert(!SwimRecoverDepth(
        {0, 0, 5}, &recovered, [](SwimPoint p) { return p.z > 2; },
        [](SwimPoint p) { return p.z <= 1; }));
    assert(!SwimRecoverDepth(
        {0, 0, 50}, &recovered, [](SwimPoint) { return true; },
        [](SwimPoint p) { return p.z <= 1; }));

    // Velocity drive: the game's drag is expected, a turn or speed-up is native AI.
    auto motion = SwimMotion({0, 0, 0}, {.03, -.04, .01}, .02);
    assert(std::abs(motion.x - 1.5) < 1e-9 && std::abs(motion.y + 2) < 1e-9);
    assert(!NativeTookOver({1.5, .01, 0}, {1.318, .01, 0}));   // logged drag
    assert(!NativeTookOver({.42, 1.44, 0}, {.366, 1.266, 0})); // logged drag
    assert(NativeTookOver({1.5, 0, 0}, {0, 1.4, 0}));          // turned toward a target
    assert(NativeTookOver({1.5, 0, 0}, {3, 0, 0}));            // charging faster
    assert(NativeTookOver({1.5, 0, 0}, {0, 0, 0}));            // stopped by the AI
    assert(!NativeTookOver({0, 0, 0}, {.01, 0, 0}) && NativeTookOver({0, 0, 0}, {1, 0, 0}));
    // Passive fish (2) and hostile predators (1) oppose each other; allies do not.
    assert(AquaticOpposed(1, false, 2, false) && AquaticOpposed(2, false, 1, false));
    assert(!AquaticOpposed(1, false, 1, false) && !AquaticOpposed(2, false, 2, false));
    assert(!AquaticOpposed(1, true, 2, false));
    // One hit per new ability or combo step, none while idle or mid-swing.
    assert(AquaticAttackStarted(0, 5, 0, 0) && AquaticAttackStarted(5, 5, 1, 2));
    assert(!AquaticAttackStarted(5, 5, 2, 2) && !AquaticAttackStarted(5, 0, 2, 3));
    assert(AquaticReach(2.5f, 1) > AquaticReach(1, 1) && AquaticReach(.5f, .5f) == 2.5);
    assert(AquaticHitDamage(10, 1000) == 5 && AquaticHitDamage(1000, 100) == 40);
    assert(AquaticHitDamage(.1f, 100) == 1 && AquaticHitDamage(0, 100) == 0 &&
           AquaticHitDamage(10, 0) == 0);
    puts(
        "Species temperament, aquatic-only hostile bosses, smooth bounded movement, wall/surface/unloaded-path rejection and population lifecycle: PASS");
}
