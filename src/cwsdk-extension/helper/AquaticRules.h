#pragma once
#include <cmath>
#include <algorithm>
// IDs verified against the Steam executable's native name registration table.
inline bool AquaticPassive(int race) {
    return race == 145 || race == 146 || race == 147 || race == 152 || race == 155 || race == 295;
}
struct AquaticBoss {
    int race;
    float scale;
};
constexpr AquaticBoss AquaticBosses[] = {
    {75, 2.f}, {150, 2.f}, {153, 2.f}, {154, 2.5f}, {107, 2.f}};
inline double SwimClamp(double n, double lo, double hi) {
    return (std::max)(lo, (std::min)(hi, n));
}
struct SwimPoint {
    double x = 0, y = 0, z = 0;
};
inline double SwimDistance(SwimPoint a, SwimPoint b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                     (a.z - b.z) * (a.z - b.z));
}
inline SwimPoint SwimStep(SwimPoint a, SwimPoint b, double seconds, double speed) {
    double distance = SwimDistance(a, b);
    if (distance < 1e-8)
        return a;
    double t = (std::min)(1., SwimClamp(seconds, 0., .1) * speed / distance);
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}
template <class Wet> bool SwimPathClear(SwimPoint a, SwimPoint b, Wet wet) {
    double distance = SwimDistance(a, b);
    if (!std::isfinite(distance) || distance > 24)
        return false;
    int steps = (std::max)(1, (int)std::ceil(distance / .5));
    for (int i = 0; i <= steps; i++) {
        double t = (double)i / steps;
        if (!wet(SwimPoint{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}))
            return false;
    }
    return true;
}

// Native Steam IDs: normal crab, spitter, and four slime colors.
inline bool LakeEncounterRace(int race) {
    return race == 106 || race == 86 || (race >= 37 && race <= 40);
}
inline int LakeDominantRace(unsigned hash) {
    return hash % 3 == 0 ? 106 : hash % 3 == 1 ? 86 : 37 + (hash / 3) % 4;
}

// Recover a surfaced fish through air/water only, never through terrain.
template <class Passable, class Wet>
bool SwimRecoverDepth(SwimPoint current, SwimPoint *out, Passable passable, Wet wet) {
    for (int step = 0; step <= 20; step++) {
        auto p = current;
        p.z -= step * .5;
        if (!passable(p))
            return false;
        if (wet(p)) {
            *out = p;
            return true;
        }
    }
    return false;
}

// Fish model forward is the opposite of the former fallback heading.
inline double SwimFacingYaw(SwimPoint from, SwimPoint to) {
    return std::atan2(to.x - from.x, -(to.y - from.y)) * 180 / 3.141592653589793 + 180;
}
inline double SwimForwardAmount(double headingError) {
    double a = std::abs(headingError);
    if (a >= 60)
        return 0;
    double c = std::cos(a * 3.141592653589793 / 180);
    return c * c;
}

// Native relation sides (0x4ef80): hostile actors without the ally flag oppose
// every other behaviour. Passive fish and aquatic predators are opponents.
inline bool AquaticOpposed(int hostilityA, bool friendlyA, int hostilityB, bool friendlyB) {
    return (hostilityA != 1 || friendlyA) != (hostilityB != 1 || friendlyB);
}
// Native aquatic attacks target each other but never apply creature-to-creature
// damage. One hit lands per new ability or combo step.
inline bool AquaticAttackStarted(int previousAbility, int ability, int previousCombo, int combo) {
    return ability != 0 && (previousAbility == 0 || combo != previousCombo);
}
inline double AquaticReach(float attackerScale, float targetScale) {
    return 2. * (std::max)(1.f, attackerScale) + .5 * (std::max)(1.f, targetScale);
}
inline float AquaticHitDamage(float attack, float targetMaxHP) {
    if (!(attack > 0) || !(targetMaxHP > 0))
        return 0;
    // A single hit never removes more than 40% of the target's health.
    return (std::min)((std::max)(1.f, attack * .5f), targetMaxHP * .4f);
}
