cmake_minimum_required (VERSION 3.15)
project(project_CubeMegaMod)
add_subdirectory(cwmods)
add_library (CubeMegaMod-XPC-RU1-AmbientWater SHARED "main.cpp"
"src/cwsdk-extension.h"
"src/hooks.h"
"src/hooks.cpp"
"src/OverworldEventBridge.h"
"src/OverworldEventBridge.cpp"
"src/NativeEventDialogue.cpp"
"src/NativeEventLoot.cpp"
"src/hooks/drop_gold_on_hit.h"
"src/cwsdk-extension/events/Event.cpp"
"src/cwsdk-extension/events/AddGoldEvent.h"
"src/cwsdk-extension/events/AddGoldEvent.cpp"
"src/cwsdk-extension/Timer.h"
"src/mods/SeaExplorationMod/event/DivingEvent.h"
"src/mods/SeaExplorationMod/event/DivingEvent.cpp"
"src/cwsdk-extension/Inventory.h"
"src/cwsdk-extension/Inventory.cpp"
"src/cwsdk-extension/Item.h"
"src/cwsdk-extension/creature/Creature.h"
"src/cwsdk-extension/events/EventList.h"
"src/cwsdk-extension/events/EventList.cpp"
"src/cwsdk-extension/creature/CreatureFactory.h"
"src/cwsdk-extension/creature/CreatureFactory.cpp"
"src/hooks/chest_opening.h"
"src/cwsdk-extension/ability/Ability.h"
"src/cwsdk-extension/ability/Ability.cpp"
"src/cwsdk-extension/ability/HealAbility.h"
"src/cwsdk-extension/ability/HealAbility.cpp"
"src/cwsdk-extension/ability/FarJumpAbility.h"
"src/cwsdk-extension/ability/FarJumpAbility.cpp"
"src/cwsdk-extension/button/DButton.cpp"
"src/cwsdk-extension/button/DButton.h"
"src/cwsdk-extension/ability/ConvertMTSAbility.h"
"src/hooks/lore_increase.h"
"src/cwsdk-extension/helper/Helper.h"
"src/cwsdk-extension/helper/Helper.cpp"
"src/cwsdk-extension/helper/FileParser.h"
"src/cwsdk-extension/helper/FileParser.cpp"
"src/CubeMod.h"
"src/mods/SeaExplorationMod/SeaExplorationMod.h"
"src/mods/SeaExplorationMod/SeaExplorationMod.cpp"
"src/hooks/ChestInteractionHandler.h"
"src/mods/LoreInteractionMod/LoreInteractionMod.h"
"src/mods/LoreInteractionMod/LoreInteractionMod.cpp"
"src/mods/CombatUpdatesMod/CombatUpdateMod.h"
"src/mods/CombatUpdatesMod/CombatUpdateMod.cpp"
"src/mods/BeginnerModeMod/BeginnerModeMod.h"
"src/mods/BeginnerModeMod/BeginnerModeMod.cpp"
"src/mods/CreatureUpdatesMod/CreatureUpdatesMod.h"
"src/mods/CreatureUpdatesMod/CreatureUpdatesMod.cpp"
"src/hooks/ShopInteractionHandler.h"
"src/mods/ShopUpdateMod/ShopUpdateMod.h"
"src/mods/ShopUpdateMod/ShopUpdateMod.cpp"
"src/hooks/ItemPriceHandler.h"
"src/mods/WorldGenMod/WorldGenMod.cpp"
"src/mods/WorldGenMod/WorldGenMod.h"
"src/mods/WorldGenMod/WorldGenOverride/BiomeTypeOverwrite.h"
"src/mods/WorldGenMod/Noise/SimplexNoise.h"
"src/mods/WorldGenMod/Noise/SimplexNoise.cpp" "src/hooks/ItemDropPatch.h" "src/mods/RegionLockUpdateMod/RegionLockUpdateMod.h" "src/mods/RegionLockUpdateMod/RegionLockUpdateMod.cpp" "src/hooks/RegionLockUpdate.h" "src/mods/WeaponUpgradeMod/WeaponUpgradeMod.h" "src/mods/WeaponUpgradeMod/WeaponUpgradeMod.cpp" "src/mods/WeaponUpgradeMod/hooks/ModifyWeaponUpgrade.h"  "src/mods/WeaponUpgradeMod/hooks/SmithyInteraction.h" "src/mods/QuestMod/QuestMod.cpp" "src/mods/QuestMod/QuestMod.h" "src/memory_helper/memory_helper.h" "src/mods/QuestMod/hooks/ChangeQuestItemName.h" "src/mods/QuestMod/hooks/ChangeQuestDescription.h" "src/cwsdk-extension/quest/Quest.h" "src/cwsdk-extension/quest/Quest.cpp" "src/hooks/CreatureDeathHandler.h" "src/hooks/CreatureTalkHandler.h" "src/mods/PlayerUpdatesMod/PlayerUpdatesMod.h" "src/mods/PlayerUpdatesMod/PlayerUpdatesMod.cpp" "src/mods/PlayerUpdatesMod/hooks/CharacterCreationHandler.h" "src/mods/PlayerUpdatesMod/CharacterClass.h"  "src/mods/PlayerUpdatesMod/classes/MonkClass.h" "src/mods/PlayerUpdatesMod/classes/MonkClass.cpp" "src/mods/PlayerUpdatesMod/hooks/OnGetUltimateAbilityID.h" "src/mods/PlayerUpdatesMod/hooks/OnGetUltimateAbilityCooldown.h" "src/mods/PlayerUpdatesMod/hooks/OnCreatureCanEquipItem.h"     "src/mods/PlayerUpdatesMod/hooks/OnGetShiftAbilityID.h" "src/mods/PlayerUpdatesMod/hooks/OnCreaturePassiveMana.h" "src/mods/PlayerUpdatesMod/hooks/TreasureFixHandler.h" "src/mods/PlayerUpdatesMod/hooks/OnExecuteAbility.h" "src/mods/PlayerUpdatesMod/abilities/FullHealAbility.h" "src/mods/PlayerUpdatesMod/abilities/HealingPuddle.h" "src/mods/PlayerUpdatesMod/abilities/LavaPuddle.h" "src/mods/PlayerUpdatesMod/abilities/PoisonPuddle.h"    "src/mods/PlayerUpdatesMod/hooks/CharacterStyleWidget_UpdateCreature.h" "src/mods/PlayerUpdatesMod/hooks/OnGenerateCreatureAppearance.h" "src/mods/StackUpdatesMod/StackUpdatesMod.h" "src/mods/StackUpdatesMod/StackUpdatesMod.cpp" "src/mods/WorldGenMod/WorldGenOverride/BuildingTypeOverwrite.h")
target_link_libraries (CubeMegaMod-XPC-RU1-AmbientWater LINK_PUBLIC CWSDK)

include(CTest)
if(BUILD_TESTING)
    add_executable(OverworldKeyInventoryTest tests/overworld_key_inventory.cpp)
    target_compile_options(OverworldKeyInventoryTest PRIVATE /UNDEBUG)
    target_link_libraries(OverworldKeyInventoryTest PRIVATE CWSDK)
    add_test(NAME OverworldKeyInventory COMMAND OverworldKeyInventoryTest)
    add_executable(EventLootRulesTest tests/event_loot_rules.cpp)
    target_compile_features(EventLootRulesTest PRIVATE cxx_std_17)
    target_compile_options(EventLootRulesTest PRIVATE /UNDEBUG)
    add_test(NAME EventLootRules COMMAND EventLootRulesTest)
    add_executable(QuestDialogueTest tests/quest_dialogue.cpp)
    target_compile_features(QuestDialogueTest PRIVATE cxx_std_17)
    target_compile_options(QuestDialogueTest PRIVATE /UNDEBUG)
    add_test(NAME QuestDialogue COMMAND QuestDialogueTest)
    add_executable(WaterSpawnSearchTest tests/water_spawn_search.cpp)
    target_compile_options(WaterSpawnSearchTest PRIVATE /UNDEBUG)
    add_test(NAME WaterSpawnSearch COMMAND WaterSpawnSearchTest)
    add_executable(AquaticRulesTest tests/aquatic_rules.cpp)
    target_compile_options(AquaticRulesTest PRIVATE /UNDEBUG)
    add_test(NAME AquaticRules COMMAND AquaticRulesTest)
endif()
