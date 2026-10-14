#pragma once

namespace overworldBridge {
namespace loot {
// Native classes (entity classType) and the IDs below were checked against the
// inspected Steam executable: item names from its item dictionary, material
// names from its material-name switch at RVA 0x16b8a0.
enum Class { Warrior = 1, Ranger = 2, Mage = 3, Rogue = 4, Monk = 5 };
enum Material {
    Iron = 1, Wood = 2, Steel = 4, Gold = 11, Silver = 12,
    Silk = 26, Linen = 27, Cotton = 28, Bronze = 29
};
enum Category { Weapon = 3, Chest = 4, Gloves = 5, Boots = 6, Shoulder = 7, Amulet = 8, Ring = 9 };
enum WeaponId {
    Sword = 0, Axe = 1, Mace = 2, Dagger = 3, Fist = 4, Longsword = 5, Bow = 6, Crossbow = 7,
    Boomerang = 8, Staff = 10, Wand = 11, Bracelet = 12, Shield = 13, Greatsword = 15,
    Greataxe = 16, Greatmace = 17
};

// Armor: warriors wear plate (iron, steel, bronze), rangers linen, mages silk and
// rogues cotton. Gems and crafting materials are never valid gear materials.
inline bool armorMaterial(int material, int classType) {
    switch (classType) {
    case Warrior:
        return material == Iron || material == Steel || material == Bronze;
    case Ranger:
        return material == Linen;
    case Mage:
        return material == Silk;
    case Rogue:
        return material == Cotton;
    default:
        return false;
    }
}
inline bool weapon(int id, int material, int classType) {
    switch (classType) {
    case Warrior:
        return material == Iron && ((id >= Sword && id <= Mace) || id == Shield ||
                                    (id >= Greatsword && id <= Greatmace));
    case Ranger:
        return material == Wood && id >= Bow && id <= Boomerang;
    case Mage:
        return (material == Wood && (id == Staff || id == Wand)) ||
               ((material == Gold || material == Silver) && id == Bracelet);
    case Rogue:
        return material == Iron && (id == Dagger || id == Fist || id == Longsword);
    default:
        return false;
    }
}
inline bool jewelryMaterial(int material) {
    return material == Gold || material == Silver;
}
template <class Item> inline bool nativeGear(const Item &item, int classType) {
    if (classType < Warrior || classType > Rogue || item.rarity < 0 || item.rarity > 4 ||
        item.formula_category != 0 || item.num_spirits != 0)
        return false;
    if (item.category == Weapon)
        return weapon(item.id, int(item.material), classType);
    if (item.category >= Chest && item.category <= Shoulder)
        return item.id == 0 && armorMaterial(int(item.material), classType);
    return (item.category == Amulet || item.category == Ring) && item.id == 0 &&
           jewelryMaterial(int(item.material));
}
template <class Item> inline bool naturalGear(const Item &item) {
    for (int classType = Warrior; classType <= Rogue; ++classType)
        if (nativeGear(item, classType))
            return true;
    return false;
}
template <class Item> inline bool recipientGear(const Item &item, int classType) {
    if (classType == Monk) {
        // CubeMega's Monk uses native fist weapons. Its cloth rewards remain
        // native cotton armor rather than a new material/category combination.
        return nativeGear(item, Rogue) && (item.category != Weapon || item.id == Fist);
    }
    return nativeGear(item, classType);
}
} // namespace loot
} // namespace overworldBridge
