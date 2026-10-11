#include "SeaExplorationMod.h"

#include <fstream>

int SeaExplorationMod::OnChestInteraction(cube::Game *game, cube::Creature *creature, int type) {
    game->PrintMessage(L"You opened a treasure chest!\n", 255, 165, 0);

    cube::Creature *player = game->GetPlayer();

    switch (type) {
    case 1: {
        // Skull chest
        if (cube::Helper::RandomInt() % 20 == 0) {
            // Drop artifact (5% chance)
            cube::Helper::DropItem(player, cube::Helper::ItemGenerationType::Artifact);
        }

        // Drop gold (100% chance)
        cube::Helper::DropItem(player, cube::Helper::ItemGenerationType::Gold,
                               5 * (1 + cube::Helper::RandomInt() % 6));

        // Drop gear (50% chance - 1, 50% chance - 2)
        int cnt = 1 + cube::Helper::RandomInt() % 2;
        for (int i = 0; i < cnt; i++) {
            cube::Item item = cube::Helper::GenerateItem(cube::Helper::ItemGenerationType::Gear,
                                                         creature->entity_data.current_region);
            item.rarity = 3 + cube::Helper::RandomInt() % 5;
            cube::Helper::DropItem(player, item, 1);
        }
        break;
    }
    case 2:
        // Obsedian chest

        // Drop artifact (100% chance)
        cube::Helper::DropItem(player, cube::Helper::ItemGenerationType::Artifact);
        break;
    case 3:
        // Bone chest

        // Drop pet (100% chance)
        cube::Helper::DropItem(player, cube::Helper::ItemGenerationType::Pet);
        break;
    case 0:
    default:
        // Normal chest

        // Drop gold (100% chance)
        cube::Helper::DropItem(player, cube::Helper::ItemGenerationType::Gold, 10);

        if (cube::Helper::RandomInt() % 4 != 0) {
            // Drop gear (75% chance)
            cube::Helper::DropItem(player, cube::Helper::ItemGenerationType::Gear, 1);
        }

        if (cube::Helper::RandomInt() % 2 == 0) {
            // Drop consumable (50% chance)
            cube::Helper::DropItem(player, cube::Helper::ItemGenerationType::Consumable, 1);
        }
        break;
    }

    return 1;
}

void SeaExplorationMod::OnGameTick(cube::Game *game) {
    if (!game->host.running || !m_Enabled) {
        delete m_DivingEvent;
        m_DivingEvent = nullptr;
        m_EventPlayer = nullptr;
        m_EventWorld = nullptr;
        return;
    }
    if (cube::Helper::InGUI(game) || !game->GetPlayer()) {
        return;
    }

    // Keep the ambient enemy event alive above water as well as below it.
    if (m_DivingEvent && (m_EventPlayer != game->GetPlayer() || m_EventWorld != game->world)) {
        // The old world's entities have already been disposed by the game.
        m_DivingEvent->Abandon();
        delete m_DivingEvent;
        m_DivingEvent = nullptr;
    }
    if (m_DivingEvent == nullptr) {
        m_DivingEvent = new cube::DivingEvent(&m_Data.autoGoldUsage);
        m_EventPlayer = game->GetPlayer();
        m_EventWorld = game->world;
    }

    if (m_DivingEvent != nullptr) {
        m_DivingEvent->Update();
    }
}

int SeaExplorationMod::OnChat(std::wstring *message) {
    const wchar_t *msg = message->c_str();

    if (!wcscmp(msg, L"/enable autogoldusage")) {
        m_Data.autoGoldUsage = true;
        Save(&m_Data, sizeof(m_Data));
        return 1;
    }

    if (!wcscmp(msg, L"/disable autogoldusage")) {
        m_Data.autoGoldUsage = false;
        Save(&m_Data, sizeof(m_Data));
        return 1;
    }
    return 0;
}

void SeaExplorationMod::Initialize() {
    cube::Helper::DisableCreatureFloating();
}
