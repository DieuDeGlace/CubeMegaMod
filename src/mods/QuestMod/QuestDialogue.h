#pragma once
// What a villager says when handing out a Quest Mod bounty: one of five lines,
// at random, never the same one twice in a row. {race}, {count} and {gold}
// are the quest's creature, kill target and gold reward.
#include <cstddef>
#include <cstdint>
#include <string>

namespace questDialogue {
constexpr std::size_t Lines = 5;
// Race names are singular ("Cow", "Snout Beetle"), so lines use them that way.
inline const wchar_t *const offers[Lines] = {
    L"No {race} near town is safe to be around anymore. Defeat {count} of them and I'll pay "
    L"you {gold} gold. I've written it down for you.",
    L"You look like you can fight. Every {race} you take down helps us. Take down {count} and "
    L"come back {gold} gold richer.",
    L"I can't sleep with every {race} roaming outside town. Hunt {count} of them for me; {gold} "
    L"gold says you can do it.",
    L"Adventurer! There's a bounty out: {count} {race} kills, {gold} gold. Here's the notice.",
    L"The elders asked me to find a hunter. Bring down {count} of the {race} kind and {gold} "
    L"gold is yours. Take this, it has the details.",
};

// Replaces every {race}, {count} and {gold} in `line`.
inline std::wstring fill(const wchar_t *line, const std::wstring &race, int count, int gold) {
    std::wstring text(line);
    auto replace = [&](const wchar_t *token, const std::wstring &value) {
        std::wstring key(token);
        for (std::size_t at = text.find(key); at != std::wstring::npos;
             at = text.find(key, at + value.size()))
            text.replace(at, key.size(), value);
    };
    replace(L"{race}", race.empty() ? std::wstring(L"creature") : race);
    replace(L"{count}", std::to_wstring(count));
    replace(L"{gold}", std::to_wstring(gold));
    return text;
}

// A line index from a random value, never the previous one.
inline std::size_t pick(uint64_t random, std::size_t &previous) {
    std::size_t index = random % Lines;
    if (index == previous)
        index = (index + 1 + (random / Lines) % (Lines - 1)) % Lines;
    previous = index;
    return index;
}
} // namespace questDialogue
