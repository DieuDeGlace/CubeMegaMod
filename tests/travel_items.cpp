#include "../src/cwsdk-extension/helper/TravelItemRules.h"
#include <cassert>
#include <cstdio>
#include <vector>
struct Region { int x, y; };
struct Item { char category; int id; Region region; };
struct Stack { int quantity; Item item; };
int main() {
    std::vector<std::vector<Stack>> tabs(4);
    assert(!TravelItemOwned(tabs, 1));
    assert(TravelItemOwned(tabs, 4)); // native always-owned id
    // A hang glider bought in a far region counts here too.
    tabs[1].push_back({1, {24, 1, {-15680, -1938}}});
    assert(TravelItemOwned(tabs, 1));
    assert(!TravelItemOwned(tabs, 2) && !TravelItemOwned(tabs, 3));
    // An empty stack or another category with the same id does not.
    tabs[1].push_back({0, {24, 2, {0, 0}}});
    tabs[0].push_back({1, {7, 3, {0, 0}}});
    assert(!TravelItemOwned(tabs, 2) && !TravelItemOwned(tabs, 3));
    tabs[2].push_back({1, {24, 3, {5, 5}}});
    assert(TravelItemOwned(tabs, 3));
    puts("Travel items owned in any region: PASS");
}
