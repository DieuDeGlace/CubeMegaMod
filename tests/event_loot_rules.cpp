#include "../src/EventLootRules.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

struct Item {
    int category = 3, id = 0, material = 1, rarity = 0, formula_category = 0, num_spirits = 0;
};
int main() {
    using namespace overworldBridge::loot;
    Item item;
    assert(recipientGear(item, 1) && !recipientGear(item, 4));
    item.id = 3;
    assert(recipientGear(item, 4) && !recipientGear(item, 1));
    item.id = 5;
    assert(recipientGear(item, 4));
    item.id = 4;
    assert(recipientGear(item, 5));
    for (int classType = 1; classType <= 4; ++classType) {
        for (int category = 4; category <= 7; ++category) {
            item = Item{};
            item.category = category;
            item.material = classType == 1 ? 1 : classType == 2 ? 27 : classType == 3 ? 26 : 28;
            assert(recipientGear(item, classType));
            for (int other = 1; other <= 4; ++other)
                assert(recipientGear(item, other) == (other == classType));
            for (int material = 0; material <= 255; ++material) {
                item.material = material;
                bool allowed = classType == 1 ? (material == 1 || material == 4 || material == 29)
                                              : material == (classType == 2   ? 27
                                                             : classType == 3 ? 26
                                                                              : 28);
                assert(recipientGear(item, classType) == allowed);
            }
        }
    }
    for (int category : {8, 9}) {
        item = Item{};
        item.category = category;
        for (int material : {11, 12}) {
            item.material = material;
            for (int classType = 1; classType <= 5; ++classType)
                assert(recipientGear(item, classType));
        }
    }
    // The former random-material generator's diamond/fabric/weapon crossovers
    // cannot pass. Recipes, spirits and out-of-range IDs/stars are rejected too.
    item = Item{};
    item.category = 7;
    for (int material : {0, 2, 3, 8, 9, 20, 21, 22, 25, 36, 37, 44, 128, 255}) {
        item.material = material;
        assert(!naturalGear(item));
    }
    item = Item{};
    for (int id : {-1, 9, 14, 18, 1000}) {
        item.id = id;
        assert(!naturalGear(item));
    }
    item = Item{};
    item.formula_category = 3;
    assert(!naturalGear(item));
    item.formula_category = 0;
    item.num_spirits = 1;
    assert(!naturalGear(item));
    item.num_spirits = 0;
    for (int rarity = -1; rarity <= 6; ++rarity) {
        item.rarity = rarity;
        assert(naturalGear(item) == (rarity >= 0 && rarity <= 4));
    }
    puts("Native gear families, class/material bounds and invalid reward rejection: PASS");
}
