#include "../src/mods/QuestMod/QuestDialogue.h"
#include <cassert>
#include <cstdio>
#include <set>

int main() {
    using namespace questDialogue;
    std::set<std::wstring> distinct;
    for (auto line : offers) {
        distinct.insert(line);
        std::wstring text(line);
        // Every line names the creature, the count and the reward exactly once.
        for (auto token : {L"{race}", L"{count}", L"{gold}"}) {
            auto at = text.find(token);
            assert(at != std::wstring::npos && text.find(token, at + 1) == std::wstring::npos);
        }
        auto filled = fill(line, L"Snout Beetle", 50, 500);
        assert(filled.find(L'{') == std::wstring::npos && filled.find(L'}') == std::wstring::npos);
        assert(filled.find(L"Snout Beetle") != std::wstring::npos);
        assert(filled.find(L"50") != std::wstring::npos && filled.find(L"500") != std::wstring::npos);
        assert(filled.size() < 200);
    }
    assert(distinct.size() == Lines);
    // A missing race name still reads naturally.
    assert(fill(offers[0], L"", 10, 100).find(L"creature") != std::wstring::npos);
    // Random choice covers every line and never repeats back to back.
    std::size_t previous = Lines;
    std::set<std::size_t> seen;
    uint64_t random = 12345;
    for (int i = 0; i < 1000; ++i) {
        random = random * 6364136223846793005ull + 1442695040888963407ull;
        std::size_t before = previous;
        auto index = pick(random >> 17, previous);
        assert(index < Lines && index != before);
        seen.insert(index);
    }
    assert(seen.size() == Lines);
    std::size_t last = 2;
    assert(pick(2, last) != 2);
    puts("Quest dialogue PASS");
}
