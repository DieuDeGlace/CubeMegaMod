#include "OverworldEventBridge.h"
#include "NativeEventDialogue.h"
#include "NativeEventLoot.h"
#include "EventLootRules.h"

#include "cwsdk-extension/creature/CreatureFactory.h"
#include "cwsdk-extension/creature/Creature.h"
#include "cwsdk-extension/helper/Helper.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_set>

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
           nt->FileHeader.TimeDateStamp == 0x5d93ab0d &&
           (nt->OptionalHeader.SizeOfImage == 0x5b7000 || // PC (SteamStub .bind section)
            nt->OptionalHeader.SizeOfImage == 0x58a000);  // GameNative (section stripped)
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
    static std::uint64_t next = overworldBridge::FirstEntityId;
    for (unsigned attempt = 0; attempt < 4096; ++attempt) {
        auto id = next++;
        if (next >= overworldBridge::EntityIdLimit)
            next = overworldBridge::FirstEntityId;
        if (!HasId(game->host.world, id) &&
            (game->world == &game->host.world || !HasId(*game->world, id)))
            return id;
    }
    return 0;
}

// Exact IDs of every actor this bridge created. Event objects are identified
// by membership, never by ID range: native creatures and chests may use any ID.
struct SpawnedIds {
    std::mutex gate;
    std::unordered_set<std::uint64_t> ids;
};
SpawnedIds &Spawned() {
    static SpawnedIds value;
    return value;
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
    static const unsigned char predicate[] = {0x48, 0x8b, 0xcf, 0xe8, 0x75, 0x5c, 0xfc, 0xff,
                                              0x84, 0xc0, 0x0f, 0x84, 0xf5, 0x00, 0x00, 0x00};
    // The native predicate accepts races 181 through 184 and remains callable.
    static const unsigned char native[] = {0x8b, 0x51, 0x64, 0x81, 0xea, 0xb5, 0x00, 0x00,
                                           0x00, 0x83, 0xfa, 0x03, 0x0f, 0x96, 0xc0, 0xc3};
    return std::memcmp(CWOffset(0x99193), predicate, sizeof(predicate)) == 0 &&
           std::memcmp(CWOffset(0x5ee10), native, sizeof(native)) == 0;
}

bool overworldBridge::CanExamine(cube::Creature *creature) {
    if (!creature)
        return false;
    auto id = static_cast<std::uint64_t>(creature->id);
    // Actors this bridge spawned never use the native chest predicate: an event
    // chest that is no longer tracked must not become an ordinary chest.
    if (!SpawnedEventEntity(id))
        return reinterpret_cast<NativeCanExamine>(CWOffset(0x5ee10))(creature);
    // Only event-owned noncombat examination objects may enter the chest path.
    // Its existing interaction bridge handles their reward/open state first.
    if (creature->entity_data.hostility_type !=
        static_cast<int>(cube::Enums::EntityBehaviour::ExamineObject))
        return false;
    static EntityOwnership callback = nullptr;
    auto function = Resolve(callback, "IsOverworldEventEntity");
    return function && function(id) != 0;
}

void overworldBridge::CreatureDeath(cube::Game *game, cube::Creature *victim,
                                    cube::Creature *attacker) {
    static DeathCallback callback = nullptr;
    auto function = Resolve(callback, "OverworldEventCreatureDeath");
    if (function && game && victim)
        function(game, victim, attacker);
}

bool overworldBridge::SpawnedEventEntity(std::uint64_t id) {
    auto &spawned = Spawned();
    std::lock_guard<std::mutex> guard(spawned.gate);
    return spawned.ids.count(id) != 0;
}

EXPORT unsigned OverworldBridgeVersion() {
    return overworldBridge::Version;
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
    {
        auto &spawned = Spawned();
        std::lock_guard<std::mutex> guard(spawned.gate);
        spawned.ids.insert(id);
    }
    creature->entity_data.position = *position;
    creature->entity_data.current_region = *region;
    creature->entity_data.yaw = cube::Helper::RandomZeroToOne() * 360.f;
    cube::CreatureFactory::SetAppearance(creature, race, behaviour, stars);
    if (behaviour == static_cast<int>(cube::Enums::EntityBehaviour::Hostile))
        creature->entity_data.appearance.flags2 &=
            ~(1u << static_cast<unsigned>(cube::Enums::AppearanceModifiers::IsFriendly));
    if (behaviour == static_cast<int>(cube::Enums::EntityBehaviour::ExamineObject)) {
        // Native examination actors have no combat equipment/inventory. Their
        // positive HP is only the engine's liveness marker, with no health HUD.
        std::memset(&creature->entity_data.equipment, 0, sizeof(creature->entity_data.equipment));
        creature->inventory_tabs.clear();
        creature->entity_data.appearance.flags2 &=
            ~((1u << 9) | (1u << 14) | (1u << 15) | (1u << 17) | (1u << 18) | (1u << 22));
    }
    auto hp = creature->GetMaxHP();
    creature->entity_data.HP = behaviour == 8 ? 1.f : (std::isfinite(hp) && hp > 0.f ? hp : 1.f);
    for (auto &tab : creature->inventory_tabs)
        for (auto &stack : tab)
            stack.item.region = *region;
    cube::CreatureFactory::AddCreatureToWorld(creature);
    return creature;
}

EXPORT int DropOverworldReward(cube::Game *game, cube::Creature *recipient, const cube::Item *item,
                               cube::Creature *source) {
    if (!SupportedSteam() || !game || !game->world || !game->host.running || !recipient || !item)
        return 0;
    // Checked again at delivery: the caller only claims a reward this accepts.
    if (!overworldBridge::loot::recipientGear(*item, recipient->entity_data.classType))
        return 0;
    auto reward = *item;
    auto position = recipient->entity_data.position;
    // Call the native entry, retaining PyroProgression's installed drop hook and
    // the defeated enemy source for its chance of higher-level gear.
    reinterpret_cast<Drop>(CWOffset(0x2a5b70))(&game->host.world, &reward, &position, 0.f, 0.f, 1,
                                               source, static_cast<std::uint64_t>(recipient->id));
    return 1;
}

EXPORT int GenerateOverworldGear(cube::Creature *recipient, const IntVector2 *region,
                                 std::uint64_t roll, cube::Item *out) {
    if (!SupportedSteam() || !recipient || !region || !out)
        return 0;
    return overworldBridge::GenerateNativeEventLoot(recipient, *region, roll, *out) ? 1 : 0;
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
    if (!SupportedSteam() || !game || !speaker || !text)
        return;
    // Command/status feedback belongs to chat; event speakers use the normal
    // world-space speech widget that is anchored to their native entity ID.
    if (speaker != game->GetPlayer()) {
        if (!overworldBridge::ShowNativeEventDialogue(game, speaker, text))
            OutputDebugStringA("OverworldEvents: native speaker dialogue was unavailable.\n");
        return;
    }
    if (!game->gui.chat_widget)
        return;
    FloatRGBA color(235.f / 255.f, 235.f / 255.f, 210.f / 255.f, 1.f);
    game->PrintMessage(text, &color);
    game->PrintMessage(L"\n", &color);
}
