#pragma once
// Travel items (category 24: 1 hang glider, 2 boat, 3 reins, ...) are owned in
// the native game only for the region they were bought in. With the region lock
// lifted, any owned copy counts in every region, like region-unlocked gear.
// Pure rule (tests/travel_items.cpp); tabs are vectors of item stacks.
constexpr int TRAVEL_ITEM_CATEGORY = 24;
constexpr int TRAVEL_ITEM_ALWAYS_OWNED = 4; // The native check treats id 4 as always owned.

template <class Tabs> bool TravelItemOwned(const Tabs &tabs, int id) {
    if (id == TRAVEL_ITEM_ALWAYS_OWNED)
        return true;
    for (const auto &tab : tabs)
        for (const auto &stack : tab)
            if (stack.quantity > 0 && stack.item.category == TRAVEL_ITEM_CATEGORY &&
                stack.item.id == id)
                return true;
    return false;
}
