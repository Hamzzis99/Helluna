// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

// ============================================
// 🔧 디버깅 로그 전처리기 플래그
// 출시 전에 모두 0으로 변경하세요
// ============================================

// 장착 시스템 (EquipmentComponent, EquipActor, EquippedGridSlot, ItemFragment)
#define INV_DEBUG_EQUIP 0

// 인벤토리 관리 (InventoryComponent, FastArray)
#define INV_DEBUG_INVENTORY 0

// UI 위젯 (SpatialInventory, InventoryGrid, HUD)
#define INV_DEBUG_WIDGET 0

// 제작 시스템 (CraftingButton, CraftingMenu, CraftingStation)
#define INV_DEBUG_CRAFT 0

// 건설 시스템 (BuildingButton, BuildingComponent)
#define INV_DEBUG_BUILD 0

// 자원 시스템 (ResourceComponent)
#define INV_DEBUG_RESOURCE 0

// 플레이어 (PlayerController)
#define INV_DEBUG_PLAYER 0

// 부착물 시스템 (AttachmentFragments, AttachmentPanel, FastArray 부착진단)
#define INV_DEBUG_ATTACHMENT 1

// 저장/로드 시스템 (SaveGameMode, 로드 복원)
#define INV_DEBUG_SAVE 0

// ============================================

//로그에 대해서 허용할 때 여기다가 이름을 넣는다?
DECLARE_LOG_CATEGORY_EXTERN(LogInventory, Log, All);

class FInventoryModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};