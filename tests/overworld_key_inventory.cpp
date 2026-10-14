#include "../src/OverworldEventBridge.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

// Item/ItemStack constructors are local SDK code. A test must never invoke
// game-address wrappers or the SDK's process-specific allocator.
void *CWOffset(size_t) {
    std::abort();
}
void *operator new(size_t size) {
    if (auto memory = std::malloc(size ? size : 1))
        return memory;
    throw std::bad_alloc();
}
void *operator new[](size_t size) {
    return ::operator new(size);
}
void operator delete(void *memory) noexcept {
    std::free(memory);
}
void operator delete[](void *memory) noexcept {
    std::free(memory);
}

int main() {
    namespace inventory = overworldBridge::inventory;
    static_assert(inventory::KeyTab == 1, "Native Steam category-22 routing");
    for (int id = 0; id < 20; ++id) {
        cube::Item key(22, id);
        key.modifier = 0x78563412;
        key.region = IntVector2(-17, 29);
        inventory::Tabs tabs(6);
        assert(inventory::Grant(tabs, key) == 2);
        for (size_t tab = 0; tab < tabs.size(); ++tab)
            assert(tabs[tab].size() == (tab == inventory::KeyTab ? 1 : 0));
        assert(tabs[inventory::KeyTab][0].quantity == 1);
        assert(inventory::Grant(tabs, key) == 1);
        assert(tabs[inventory::KeyTab].size() == 1);
        assert(inventory::Consume(tabs, key));
        assert(tabs[inventory::KeyTab].empty());
        assert(!inventory::Consume(tabs, key));
    }

    cube::Item key(22, 6);
    key.modifier = 0x1fedcba9;
    key.region = IntVector2(-8, 63);
    inventory::Tabs tooSmall(1);
    assert(inventory::Grant(tooSmall, key) == 0);
    for (const auto &invalid : {cube::Item(24, 13), cube::Item(22, -1), cube::Item(22, 20)}) {
        inventory::Tabs tabs(6);
        assert(inventory::Grant(tabs, invalid) == 0);
        assert(!inventory::Consume(tabs, invalid));
    }

    // A saved key can be in any tab. Near matches from another encounter,
    // another region, and another native key must all survive consumption.
    for (size_t heldTab = 0; heldTab < 6; ++heldTab) {
        inventory::Tabs tabs(6);
        for (int mismatch = 0; mismatch < 5; ++mismatch) {
            auto other = key;
            if (mismatch == 0)
                ++other.id;
            if (mismatch == 1)
                ++other.modifier;
            if (mismatch == 2)
                ++other.region.x;
            if (mismatch == 3)
                ++other.region.y;
            if (mismatch == 4)
                other.category = 24;
            tabs[(heldTab + 1) % 6].push_back(cube::ItemStack(3, other));
        }
        tabs[heldTab].push_back(cube::ItemStack(0, key));
        assert(!inventory::Consume(tabs, key));
        tabs[heldTab].push_back(cube::ItemStack(2, key));
        assert(inventory::Grant(tabs, key) == 1);
        assert(inventory::Consume(tabs, key));
        assert(tabs[heldTab].back().quantity == 1);
        assert(inventory::Consume(tabs, key));
        assert(tabs[heldTab].size() == 1 && tabs[heldTab][0].quantity == 0);
        assert(!inventory::Consume(tabs, key));
        for (const auto &stack : tabs[(heldTab + 1) % 6])
            assert(stack.quantity == 3);
    }
    puts(
        "Native key inventory mapping, all 20 key IDs, duplicate grants, exact encounter binding, and one-at-a-time consumption: PASS");
}
