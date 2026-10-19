/*
* Hook for a new regionlock update.
*/

#pragma once
#include "cwsdk.h"
#include "../hooks.h"
#include "../cwsdk-extension/helper/TravelItemRules.h"
#include <cstring>

// Todo make function for cubemod.
extern "C" int OnGetEffectiveItemRarity(cube::Item *item, int distance) {
    // Reconstructed XPC-RU1 behavior: no equipment rarity loss across regions.
    return item->rarity;
}

__attribute__((naked)) void ASMEffectiveItemRarity() {
    asm(".intel_syntax \n"

        "push r15 \n" PUSH_ALL

        "mov rdx, rax \n"
        "mov rcx, r9 \n"

        PREPARE_STACK

        "call OnGetEffectiveItemRarity \n"

        RESTORE_STACK

        "mov [rsp], rax \n"

        POP_ALL

        "mov rax, r15 \n"
        "pop r15 \n"

        "retn \n"
        ".att_syntax \n");
}

// Native "owns travel item `id` for the current region" (RVA 0x5E890): counts
// inventory items equal to {category 24, id, current region}. Replaced so any
// owned copy works in every region (gliding, sailing, riding).
extern "C" bool OnTravelItemOwned(cube::Creature *creature, int id) {
    return creature && TravelItemOwned(creature->inventory_tabs, id);
}

// Patches only when the inspected Steam bytes are present (else that part of the
// unlock is skipped and the native behavior kept).
static bool NativeBytes(size_t rva, const unsigned char *bytes, size_t size) {
    return std::memcmp(CWOffset(rva), bytes, size) == 0;
}
static void RemoveRegionTest(size_t rva) {
    // `cmp [rcx],eax; jne ...`: drop the jne so a different item region still passes.
    static const unsigned char jne[] = {0x39, 0x01, 0x0F, 0x85};
    if (!NativeBytes(rva - 2, jne, sizeof(jne)))
        return;
    for (int i = 0; i < 6; i++)
        WriteByte(CWOffset(rva + i), (char)0x90);
}
static void SetupTravelItemRegionUnlock() {
    static const unsigned char entry[] = {0x40, 0x53, 0x56, 0x48, 0x81, 0xEC, 0xD8, 0x00, 0x00, 0x00};
    if (NativeBytes(0x5E890, entry, sizeof(entry)))
        WriteFarJMP(CWOffset(0x5E890), (void *)&OnTravelItemOwned);
    RemoveRegionTest(0xB7A99); // Using a travel item from the inventory.
    RemoveRegionTest(0xE46CA); // Its HUD icon.
}

void SetupRegionLockUpdates() {
    SetupTravelItemRegionUnlock();
    WriteFarJMP(CWOffset(0x10976D), (void *)&ASMEffectiveItemRarity);
    // Change jnz to jo, to always check for the distance.
    WriteByte(CWOffset(0x10974B), 0x70);
    WriteByte(CWOffset(0x109733), 0x70);
}
