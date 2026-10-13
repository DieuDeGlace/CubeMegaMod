/*
* Hook for opening the self made chests. Here, the chests are opened, 
* items are added to the player and the gain of the item is announced.
*/

#pragma once
#include "cwsdk.h"

#include "../cwsdk-extension/creature/Creature.h"
#include "../cwsdk-extension/helper/Helper.h"

#include "../CubeMod.h"
#include "../OverworldEventBridge.h"

int ChestInteractionHandler(cube::Game *game, cube::Creature *creature, int type) {
	for (CubeMod* mod : g_Mods) {
		if (mod->OnChestInteraction(game, creature, type)) {
			return 1;
		}
	}
	return 0;
}

extern "C" void OnChestInteraction(cube::Game* game, cube::Creature* creature) {
	if (overworldBridge::ObjectInteraction(game, creature))
	{
		return;
	}
	int type = creature->entity_data.race - 181;
	if (!ChestInteractionHandler(game, creature, type))
	{
		return;
	}

	// Check if the chest is visible on the map.
	if (creature->entity_data.binary_toggles >> (int)cube::Enums::StateFlags::VisibleOnMap & 1 == 1)
	{
		creature->entity_data.binary_toggles ^= 1 << (int)cube::Enums::StateFlags::VisibleOnMap;
	}

	// Open the chest
	creature->entity_data.interaction_state = 2;
}

extern "C" bool OnCanExamineOverworldObject(cube::Creature* creature) {
	return overworldBridge::CanExamine(creature);
}

GETTER_VAR(void*, ASMCanExamineOverworldObject_jmpback);
GETTER_VAR(void*, ASMCanExamineOverworldObject_bail);
__attribute__((naked)) void ASMCanExamineOverworldObject() {
	asm(".intel_syntax \n"
		PUSH_ALL
		// Preserve volatile vector registers around the new ownership callback.
		// The original native predicate uses only edx and al.
		"sub rsp, 0x60 \n"
		"movups [rsp], xmm0 \n"
		"movups [rsp + 0x10], xmm1 \n"
		"movups [rsp + 0x20], xmm2 \n"
		"movups [rsp + 0x30], xmm3 \n"
		"movups [rsp + 0x40], xmm4 \n"
		"movups [rsp + 0x50], xmm5 \n"
		"mov rcx, rdi \n"
		PREPARE_STACK
		"call OnCanExamineOverworldObject \n"
		RESTORE_STACK
		// PUSH_ALL saved rax 0x70 bytes above the six saved XMM registers.
		// Replace only al with the predicate result before restoring registers.
		"mov byte ptr [rsp + 0xD0], al \n"
		"movups xmm0, [rsp] \n"
		"movups xmm1, [rsp + 0x10] \n"
		"movups xmm2, [rsp + 0x20] \n"
		"movups xmm3, [rsp + 0x30] \n"
		"movups xmm4, [rsp + 0x40] \n"
		"movups xmm5, [rsp + 0x50] \n"
		"add rsp, 0x60 \n"
		POP_ALL
		"mov rcx, rdi \n"
		"test al, al \n"
		"jz 1f \n"
		DEREF_JMP(ASMCanExamineOverworldObject_jmpback)
		"1: \n"
		DEREF_JMP(ASMCanExamineOverworldObject_bail)
		".att_syntax \n"
	);
}

GETTER_VAR(void*, ASMIsChestOpen_jmpback);
GETTER_VAR(void*, ASMIsChestOpen_bail);
__attribute__((naked)) void ASMIsChestOpen() {
	asm(".intel_syntax \n"

		"cmp byte ptr [rdi + 0x19C], 2 \n"
		"jne 1f \n"

		// Todo: If this chrashes, then jmpback should work.
		DEREF_JMP(ASMIsChestOpen_bail)

		"1: \n"

		// Original code
		"mov rax, [rsi + 0x8] \n"
		"mov rcx, [rax + 0x448]\n"
		"mov rax, [rdi + 0x10] \n"
		"mov rdx, [rdi + 0x18] \n"
		"mov r8, [rdi + 0x20]  \n"
		"sub rax, [rcx + 0x10] \n"
		"sub rdx, [rcx + 0x18] \n"
		"sub r8, [rcx + 0x20]  \n"
		
		DEREF_JMP(ASMIsChestOpen_jmpback)

		".att_syntax \n"
	);
}

GETTER_VAR(void*, ASMChestInteractionHandler_jmpback);
__attribute__((naked)) void ASMChestInteractionHandler() {
	asm(".intel_syntax \n"
		// No original code is needed.

		PUSH_ALL
		"mov rdx, rdi \n"
		"mov rcx, rsi \n"

		PREPARE_STACK

		"call OnChestInteraction \n"

		RESTORE_STACK

		POP_ALL

		DEREF_JMP(ASMChestInteractionHandler_jmpback)

		".att_syntax \n"
	);
}

void SetupChestInteractionHandler() {
	const bool extendExamination = overworldBridge::SupportsExaminationHook();
	WriteFarJMP(g_Base + 0x99288, (void*)&ASMChestInteractionHandler);
	ASMChestInteractionHandler_jmpback = (void*)(g_Base + 0x988C1);

	// Todo: Create an OnCreateInteraction(cube::Creature*) function.
	WriteFarJMP(g_Base + 0x991A3, (void*)&ASMIsChestOpen);
	ASMIsChestOpen_jmpback = (void*)(g_Base + 0x991C6);
	ASMIsChestOpen_bail = (void*)(g_Base + 0x99298);

	if (extendExamination) {
		ASMCanExamineOverworldObject_jmpback = (void*)(g_Base + 0x991A3);
		ASMCanExamineOverworldObject_bail = (void*)(g_Base + 0x99298);
		WriteFarJMP(g_Base + 0x99193, (void*)&ASMCanExamineOverworldObject);
		WriteByte(g_Base + 0x991A1, static_cast<char>(0x90));
		WriteByte(g_Base + 0x991A2, static_cast<char>(0x90));
	}
}
