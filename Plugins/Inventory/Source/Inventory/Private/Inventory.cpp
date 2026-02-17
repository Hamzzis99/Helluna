// Copyright Epic Games, Inc. All Rights Reserved.

#include "Inventory.h"
#include "GameplayTagsManager.h"

#define LOCTEXT_NAMESPACE "FInventoryModule"

DEFINE_LOG_CATEGORY(LogInventory);

void FInventoryModule::StartupModule()
{
	// ════════════════════════════════════════════════════════════════
	// 📌 부착물 슬롯 GameplayTag 네이티브 등록
	// ════════════════════════════════════════════════════════════════
	// C++ meta = (Categories = "AttachmentSlot") 필터에 의해
	// "AttachmentSlot.*" 태그만 에디터 드롭다운에 표시됨
	// 에디터 태그(.ini)는 실수로 삭제 가능하므로
	// C++ 네이티브 등록으로 항상 존재 보장
	//
	// 📌 새 슬롯 타입 추가 시:
	//    1. 여기에 AddNativeGameplayTag 한 줄 추가
	//    2. 무기 BP → HostFragment → SlotDefinitions에서 새 태그 선택
	//    3. 부착물 BP → AttachableFragment → AttachmentType에서 같은 태그 선택
	// ════════════════════════════════════════════════════════════════
	UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();

	TagManager.AddNativeGameplayTag(FName("AttachmentSlot.Scope"),    TEXT("스코프/조준경 슬롯"));
	TagManager.AddNativeGameplayTag(FName("AttachmentSlot.Muzzle"),   TEXT("총구/소음기 슬롯"));
	TagManager.AddNativeGameplayTag(FName("AttachmentSlot.Grip"),     TEXT("그립/손잡이 슬롯"));
	TagManager.AddNativeGameplayTag(FName("AttachmentSlot.Magazine"), TEXT("탄창 슬롯"));
	TagManager.AddNativeGameplayTag(FName("AttachmentSlot.Stock"),    TEXT("개머리판 슬롯"));
	TagManager.AddNativeGameplayTag(FName("AttachmentSlot.Laser"),    TEXT("레이저/조명 슬롯"));
}

void FInventoryModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FInventoryModule, Inventory)