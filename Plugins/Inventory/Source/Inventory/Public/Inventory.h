// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

// ============================================
// 디버깅 로그 전처리기 플래그
// Shipping 빌드에서는 자동으로 모두 0 처리됩니다.
// 개발 중 필요한 항목만 1로 설정하세요.
// ============================================

#if UE_BUILD_SHIPPING

// Shipping 빌드: 모든 디버그 로그 강제 비활성화
#define INV_DEBUG_EQUIP        0
#define INV_DEBUG_INVENTORY    0
#define INV_DEBUG_WIDGET       0
#define INV_DEBUG_CRAFT        0
#define INV_DEBUG_BUILD        0
#define INV_DEBUG_RESOURCE     0
#define INV_DEBUG_PLAYER       0
#define INV_DEBUG_ATTACHMENT   0
#define INV_DEBUG_SAVE         0
#define INV_DEBUG_ITEM_POINTER 0
#define INV_DEBUG_INTERACTION  0

#else // Development / Debug 빌드

// 장착 시스템 (EquipmentComponent, EquipActor, EquippedGridSlot, ItemFragment)
#define INV_DEBUG_EQUIP 1

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
#define INV_DEBUG_PLAYER 1

// 부착물 시스템 (AttachmentFragments, AttachmentPanel, FastArray 부착진단)
#define INV_DEBUG_ATTACHMENT 1

// 저장/로드 시스템 (SaveGameMode, 로드 복원)
#define INV_DEBUG_SAVE 1

// 아이템 포인터/부착물 저장 진단 (CollectInventoryDataForSave 포인터 추적)
#define INV_DEBUG_ITEM_POINTER 0

// 상호작용 시스템 (HighlightableStaticMesh, Interaction 관련)
#define INV_DEBUG_INTERACTION 0

#endif // UE_BUILD_SHIPPING

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
