#pragma once

namespace overworldBridge {
namespace loot {
// Equipment families emitted by Steam's native armor/weapon generators.
// Armor materials correspond to its class-bound plate, silk, linen and cotton;
// gems/crafting materials are never valid substitutes for these gear materials.
inline bool armorMaterial(int material, int classType) {
    switch (classType) {
    case 1:
        return material == 1 || material == 4 || material == 29;
    case 2:
        return material == 27;
    case 3:
        return material == 26;
    case 4:
        return material == 28;
    default:
        return false;
    }
}
inline bool weapon(int id, int material, int classType) {
    switch (classType) {
    case 1:
        return material == 1 && ((id >= 0 && id <= 2) || id == 13 || (id >= 15 && id <= 17));
    case 2:
        return material == 2 && id >= 6 && id <= 8;
    case 3:
        return (material == 2 && (id == 10 || id == 11)) ||
               ((material == 11 || material == 12) && id == 12);
    case 4:
        return material == 1 && (id == 3 || id == 4 || id == 5);
    default:
        return false;
    }
}
template <class Item> inline bool nativeGear(const Item &item, int classType) {
    if (classType < 1 || classType > 4 || item.rarity < 0 || item.rarity > 4 ||
        item.formula_category != 0 || item.num_spirits != 0)
        return false;
    if (item.category == 3)
        return weapon(item.id, int(item.material), classType);
    if (item.category >= 4 && item.category <= 7)
        return item.id == 0 && armorMaterial(int(item.material), classType);
    return (item.category == 8 || item.category == 9) && item.id == 0 &&
           (item.material == 11 || item.material == 12);
}
template <class Item> inline bool naturalGear(const Item &item) {
    for (int classType = 1; classType <= 4; ++classType)
        if (nativeGear(item, classType))
            return true;
    return false;
}
template <class Item> inline bool recipientGear(const Item &item, int classType) {
    if (classType == 5) {
        // CubeMega's Monk uses native fist weapons. Its cloth rewards remain
        // native cotton armor rather than a new material/category combination.
        return nativeGear(item, 4) && (item.category != 3 || item.id == 4);
    }
    return nativeGear(item, classType);
}
} // namespace loot
} // namespace overworldBridge
