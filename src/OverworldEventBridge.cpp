#include "OverworldEventBridge.h"

#include "cwsdk-extension/creature/CreatureFactory.h"
#include "cwsdk-extension/creature/Creature.h"
#include "cwsdk-extension/helper/Helper.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
using Interaction = int (*)(cube::Game *, cube::Creature *);
using EntityOwnership = int (*)(std::uint64_t);
using NativeCanExamine = bool (*)(cube::Creature *);
using DeathCallback = void (*)(cube::Game *, cube::Creature *, cube::Creature *);

template <typename Callback> Callback Resolve(Callback &cached, const char *name) {
    if (!cached) {
        // The loader can initialize CubeMega before OverworldEvents. Resolve on
        // interactions until it is available, then retain the callback.
        if (auto module = GetModuleHandleW(L"OverworldEvents.dll"))
            cached = reinterpret_cast<Callback>(GetProcAddress(module, name));
    }
    return cached;
}

bool SupportedSteam() {
    auto base = reinterpret_cast<const unsigned char *>(GetModuleHandleW(nullptr));
    if (!base)
        return false;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096)
        return false;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE &&
           nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
           nt->FileHeader.TimeDateStamp == 0x5d93ab0d && nt->OptionalHeader.SizeOfImage == 0x5b7000;
}

bool HasId(const cube::World &world, std::uint64_t id) {
    if (world.id_to_creature_map.find(id) != world.id_to_creature_map.end())
        return true;
    return std::any_of(world.creatures.begin(), world.creatures.end(), [id](auto creature) {
        return creature && static_cast<std::uint64_t>(creature->id) == id;
    });
}

std::uint64_t NewEntityId(cube::Game *game) {
    // Reserve positive IDs for these land actors instead of inheriting the
    // older factory's descending negative-ID convention.
    static std::uint64_t next = 0x6000000000000000ull;
    for (unsigned attempt = 0; attempt < 4096; ++attempt) {
        auto id = next++;
        if (next >= 0x7fffffffffffffffull)
            next = 0x6000000000000000ull;
        if (!HasId(game->host.world, id) &&
            (game->world == &game->host.world || !HasId(*game->world, id)))
            return id;
    }
    return 0;
}

using Drop = void (*)(cube::World *, cube::Item *, LongVector3 *, float, float, int,
                      cube::Creature *, std::uint64_t);

bool ValidInventory(cube::Game *game, cube::Creature *recipient, const cube::Item *key) {
    return SupportedSteam() && game && game->world && game->host.running && recipient && key &&
           overworldBridge::inventory::NativeKey(*key);
}
} // namespace

bool overworldBridge::Talk(cube::Game *game, cube::Creature *creature) {
    static Interaction callback = nullptr;
    auto function = Resolve(callback, "OverworldEventTalk");
    return function && game && creature && function(game, creature) != 0;
}

bool overworldBridge::ObjectInteraction(cube::Game *game, cube::Creature *creature) {
    static Interaction callback = nullptr;
    auto function = Resolve(callback, "OverworldEventObjectInteraction");
    return function && game && creature && function(game, creature) != 0;
}

bool overworldBridge::SupportsExaminationHook() {
    if (!SupportedSteam())
        return false;
    // Inspected Steam E-key block: mov rcx,rdi; call 0x5ee10; test al,al;
    // jz 0x99298. Do not extend it if another mod already replaced it.
    static const unsigned char predicate[] = {
        0x48, 0x8b, 0xcf, 0xe8, 0x75, 0x5c, 0xfc, 0xff,
        0x84, 0xc0, 0x0f, 0x84, 0xf5, 0x00, 0x00, 0x00};
    // The native predicate accepts races 181 through 184 and remains callable.
    static const unsigned char native[] = {
        0x8b, 0x51, 0x64, 0x81, 0xea, 0xb5, 0x00, 0x00,
        0x00, 0x83, 0xfa, 0x03, 0x0f, 0x96, 0xc0, 0xc3};
    return std::memcmp(CWOffset(0x99193), predicate, sizeof(predicate)) == 0 &&
           std::memcmp(CWOffset(0x5ee10), native, sizeof(native)) == 0;
}

bool overworldBridge::CanExamine(cube::Creature *creature) {
    if (!creature)
        return false;
    if (reinterpret_cast<NativeCanExamine>(CWOffset(0x5ee10))(creature))
        return true;
    // Only event-owned noncombat examination objects may enter the chest path.
    // Its existing interaction bridge handles their reward/open state first.
    if (creature->entity_data.hostility_type !=
        static_cast<int>(cube::Enums::EntityBehaviour::ExamineObject))
        return false;
    static EntityOwnership callback = nullptr;
    auto function = Resolve(callback, "IsOverworldEventEntity");
    return function && function(static_cast<std::uint64_t>(creature->id)) != 0;
}

void overworldBridge::CreatureDeath(cube::Game *game, cube::Creature *victim,
                                    cube::Creature *attacker) {
    static DeathCallback callback = nullptr;
    auto function = Resolve(callback, "OverworldEventCreatureDeath");
    if (function && game && victim)
        function(game, victim, attacker);
}

EXPORT unsigned OverworldBridgeVersion() {
    return 1;
}

EXPORT cube::Creature *SpawnOverworldEntity(const LongVector3 *position, const IntVector2 *region,
                                            int race, int behaviour, int stars) {
    if (!SupportedSteam() || !position || !region || race < 0 || stars < 0 || stars > 5 ||
        behaviour < static_cast<int>(cube::Enums::EntityBehaviour::Hostile) ||
        behaviour > static_cast<int>(cube::Enums::EntityBehaviour::ExamineObject))
        return nullptr;
    auto game = cube::GetGame();
    if (!game || !game->world || !game->host.running)
        return nullptr;
    auto id = NewEntityId(game);
    if (!id)
        return nullptr;
    auto creature = cube::Creature::Create(static_cast<__int64>(id));
    if (!creature)
        return nullptr;
    creature->entity_data.position = *position;
    creature->entity_data.current_region = *region;
    creature->entity_data.yaw = cube::Helper::RandomZeroToOne() * 360.f;
    cube::CreatureFactory::SetAppearance(creature, race, behaviour, stars);
    if (behaviour == static_cast<int>(cube::Enums::EntityBehaviour::Hostile))
        creature->entity_data.appearance.flags2 &=
            ~(1u << static_cast<unsigned>(cube::Enums::AppearanceModifiers::IsFriendly));
    auto hp = creature->GetMaxHP();
    creature->entity_data.HP = std::isfinite(hp) && hp > 0.f ? hp : 1.f;
    for (auto &tab : creature->inventory_tabs)
        for (auto &stack : tab)
            stack.item.region = *region;
    cube::CreatureFactory::AddCreatureToWorld(creature);
    return creature;
}

EXPORT void DropOverworldReward(cube::Game *game, cube::Creature *recipient, const cube::Item *item,
                                cube::Creature *source) {
    if (!SupportedSteam() || !game || !game->world || !game->host.running || !recipient || !item)
        return;
    auto reward = *item;
    auto position = recipient->entity_data.position;
    // Call the native entry, retaining PyroProgression's installed drop hook and
    // the defeated enemy source for its chance of higher-level gear.
    reinterpret_cast<Drop>(CWOffset(0x2a5b70))(&game->host.world, &reward, &position, 0.f, 0.f, 1,
                                               source, static_cast<std::uint64_t>(recipient->id));
}

EXPORT int GenerateOverworldGear(const IntVector2 *region, std::uint64_t roll, cube::Item *out) {
    if (!SupportedSteam() || !region || !out)
        return 0;
    auto item = cube::Helper::GenerateItem(cube::Helper::ItemGenerationType::Gear, *region);
    // Preserve the existing gear helper's random generation. Derive the reward
    // identity from the event roll without adding another shared RNG reseed.
    roll += 0x9e3779b97f4a7c15ull;
    roll = (roll ^ (roll >> 30)) * 0xbf58476d1ce4e5b9ull;
    roll = (roll ^ (roll >> 27)) * 0x94d049bb133111ebull;
    roll ^= roll >> 31;
    item.modifier = static_cast<unsigned>(roll);
    if (!item.modifier)
        item.modifier = 1;
    item.rarity = static_cast<char>((roll >> 32) % 5);
    *out = item;
    return 1;
}

EXPORT int GrantOverworldKey(cube::Game *game, cube::Creature *recipient, const cube::Item *key) {
    if (!ValidInventory(game, recipient, key))
        return 0;
    auto granted = *key;
    auto result = overworldBridge::inventory::Grant(recipient->inventory_tabs, granted);
    if (result == 2)
        cube::Helper::AnnounceItem(game, &granted, 1, recipient);
    return result != 0;
}

EXPORT int ConsumeOverworldKey(cube::Game *game, cube::Creature *recipient, const cube::Item *key) {
    if (!ValidInventory(game, recipient, key))
        return 0;
    return overworldBridge::inventory::Consume(recipient->inventory_tabs, *key);
}

EXPORT void OverworldDialogue(cube::Game *game, cube::Creature *speaker, const wchar_t *text) {
    if (!SupportedSteam() || !game || !speaker || !text || !game->gui.chat_widget)
        return;
    // The SDK does not expose a verified custom NPC speech-widget API. Keep this
    // fallback on the existing chat path rather than manipulating widget state.
    FloatRGBA color(235.f / 255.f, 235.f / 255.f, 210.f / 255.f, 1.f);
    game->PrintMessage(text, &color);
    game->PrintMessage(L"\n", &color);
}
