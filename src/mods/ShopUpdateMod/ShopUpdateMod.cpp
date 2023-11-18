#include "ShopUpdateMod.h"

static void UpdateGemTraderShop(cube::Game* game, std::vector<std::vector<cube::ItemStack>>* itemVector, long long id);
static void UpdateItemVendorShop(cube::Game* game, std::vector<std::vector<cube::ItemStack>>* itemVector, long long id);

int ShopUpdateMod::OnShopInteraction(cube::Game* game, std::vector<std::vector<cube::ItemStack>>* itemVector, int classType, long long id)
{
	if (itemVector != nullptr)
	{
		if (itemVector->size() > 0)
		{
			switch (classType)
			{
			case (int)cube::Enums::ClassType::GemTrader:
				UpdateGemTraderShop(game, itemVector, id);
				return 1;
				break;
			case (int)cube::Enums::ClassType::ItemVendor:
				UpdateItemVendorShop(game, itemVector, id);
				break;
			default:
				break;
			}
		}
	}
	return 0;
}

static void UpdateGemTraderShop(cube::Game* game, std::vector<std::vector<cube::ItemStack>>* itemVector, long long id)
{
	std::srand(id);
	unsigned int chanceSeed = std::rand() % id;
	std::srand(chanceSeed);
	unsigned int chance = std::rand();
	std::srand(id);
	if (chance % 2 == 0)
	{
		std::srand(id);
		cube::Item item = cube::SpiritCube::Create((cube::SpiritCube::Type)((int)cube::SpiritCube::Type::FireSpirit + (std::rand() % 4)));

		std::srand(std::rand());		
		int count = 1 + std::rand() % 3;
		std::srand(id);
		int sold = cube::Helper::CWGetItemsSold(game->world, item, id);

		if (sold < count)
		{
			itemVector->at(0).push_back(cube::ItemStack(count - sold, item));
		}
	}

	
	for (int i = 0; i < 1; i++)
	{
		cube::Item item = cube::Item(11, 32);
		item.rarity = 1;

		int count =  1 + std::rand() % 4;
		int sold = cube::Helper::CWGetItemsSold(game->world, item, id);

		if (sold < count)
		{
			itemVector->at(0).push_back(cube::ItemStack(count - sold, item));
		}
	}
	
	for (int i = 0; i < 1; i++)
	{
		cube::Item item = cube::Item(11, 33);
		item.rarity = 2;

		int count =  1 + std::rand() % 4;
		int sold = cube::Helper::CWGetItemsSold(game->world, item, id);

		if (sold < count)
		{
			itemVector->at(0).push_back(cube::ItemStack(count - sold, item));
		}
	}
	
	for (int i = 0; i < 1; i++)
	{
		cube::Item item = cube::Item(11, 34);
		item.rarity = 3;

		int count =  1 + std::rand() % 4;
		int sold = cube::Helper::CWGetItemsSold(game->world, item, id);

		if (sold < count)
		{
			itemVector->at(0).push_back(cube::ItemStack(count - sold, item));
		}
	}
	
	for (int i = 0; i < 1; i++)
	{
		cube::Item item = cube::Item(11, 35);
		item.rarity = 4;

		int count =  1 + std::rand() % 4;
		int sold = cube::Helper::CWGetItemsSold(game->world, item, id);

		if (sold < count)
		{
			itemVector->at(0).push_back(cube::ItemStack(count - sold, item));
		}
	}

	// Add 1 artifact
	{
		// Set the random seed to the id of the creature to generate the same items for the same creature every time.
		cube::Item item = cube::Item(23, std::rand() % 7);
		item.region = game->GetPlayer()->entity_data.current_region;
		item.modifier = std::rand();
		item.rarity = 0;
		item.formula_category = std::rand();

		if (!cube::Helper::CWGetItemsSold(game->world, item, id))
		{
			itemVector->at(0).push_back(cube::ItemStack(1, item));
		}
	}
}
static void UpdateItemVendorShop(cube::Game* game, std::vector<std::vector<cube::ItemStack>>* itemVector, long long id)
{
	std::srand(id);
	const static int SPECIALS[] = {
		1, 2, 3, 18, 12,
	};

	int max = sizeof(SPECIALS) / sizeof(*SPECIALS);
	for (int i = 0; i < max; i++)
	{
		cube::Item item = cube::Item(24, SPECIALS[i]);
		item.rarity = 0;
		item.region = game->GetPlayer()->entity_data.current_region;

		if (!cube::Helper::CWGetItemsSold(game->world, item, id))
		{
			itemVector->at(0).push_back(cube::ItemStack(1, item));
		}
	}

	{
		std::srand(id + game->host.world.state.day);
		cube::Item item = cube::SpiritCube::Create((cube::SpiritCube::Type)((int)cube::SpiritCube::Type::FireSpirit + std::rand() % 4));

		std::srand(std::rand());
		int count = 1 + std::rand() % 3;
		int sold = cube::Helper::CWGetItemsSold(game->world, item, id);

		if (sold < count)
		{
			itemVector->at(0).push_back(cube::ItemStack(count - sold, item));
		}
	}
}
