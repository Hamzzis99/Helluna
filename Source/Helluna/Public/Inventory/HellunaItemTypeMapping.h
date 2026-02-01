// Helluna Inventory Save System - Phase 1: DataTable Mapping

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "HellunaItemTypeMapping.generated.h"

/**
 * ============================================
 * 📌 FItemTypeToActorMapping
 * ============================================
 * 
 * DataTable Row 구조체
 * ItemType(GameplayTag) → Actor Blueprint 클래스 매핑
 * 
 * ============================================
 * 📌 사용 목적:
 * ============================================
 * - 인벤토리 저장 시: InventoryItem의 GameplayTag만 저장
 * - 인벤토리 로드 시: GameplayTag로 Actor 클래스 조회 → 임시 스폰 → ItemManifest 추출
 * 
 * ============================================
 * 📌 DataTable 설정:
 * ============================================
 * 위치: Content/Data/Inventory/DT_ItemTypeMapping
 * 
 * 예시:
 * | Row Name   | ItemType                              | ItemActorClass          |
 * |------------|---------------------------------------|-------------------------|
 * | Axe        | GameItems.Equipment.Weapons.Axe       | BP_Inv_Axe              |
 * | PotionBlue | GameItems.Consumables.Potions.Blue.Small | BP_Inv_Potion_Small_Blue |
 * 
 * 📌 작성자: Claude & Gihyeon
 */
USTRUCT(BlueprintType)
struct HELLUNA_API FItemTypeToActorMapping : public FTableRowBase
{
	GENERATED_BODY()

public:
	/**
	 * 아이템 타입 (GameplayTag)
	 * 예: "GameItems.Equipment.Weapons.Axe"
	 * 
	 * meta = (Categories = "GameItems") : GameItems 카테고리만 선택 가능
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Mapping", 
		meta = (Categories = "GameItems"))
	FGameplayTag ItemType;

	/**
	 * 아이템 Actor Blueprint 클래스
	 * 예: BP_Inv_Axe
	 * 
	 * 이 클래스를 스폰하여 ItemComponent에서 ItemManifest를 추출함
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Mapping")
	TSubclassOf<AActor> ItemActorClass;
};


/**
 * ============================================
 * 📌 UHellunaItemTypeMapping
 * ============================================
 * 
 * DataTable 조회 유틸리티 클래스
 * 
 * ============================================
 * 📌 사용 예시:
 * ============================================
 * ```cpp
 * // GameMode나 다른 클래스에서
 * UPROPERTY(EditDefaultsOnly)
 * UDataTable* ItemTypeMappingDataTable;
 * 
 * // 로드 시
 * FGameplayTag ItemType = FGameplayTag::RequestGameplayTag("GameItems.Equipment.Weapons.Axe");
 * TSubclassOf<AActor> ActorClass = UHellunaItemTypeMapping::GetActorClassFromItemType(
 *     ItemTypeMappingDataTable, ItemType);
 * 
 * if (ActorClass)
 * {
 *     AActor* TempActor = GetWorld()->SpawnActor<AActor>(ActorClass, ...);
 *     // ItemManifest 추출...
 * }
 * ```
 */
UCLASS()
class HELLUNA_API UHellunaItemTypeMapping : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * ItemType(GameplayTag)으로 Actor 클래스 조회
	 * 
	 * @param DataTable - DT_ItemTypeMapping DataTable 에셋
	 * @param ItemType - 찾을 아이템의 GameplayTag
	 * @return Actor Blueprint 클래스 (없으면 nullptr)
	 */
	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory")
	static TSubclassOf<AActor> GetActorClassFromItemType(
		const UDataTable* DataTable,
		const FGameplayTag& ItemType
	);

	/**
	 * DataTable의 모든 매핑 정보를 로그에 출력 (디버깅용)
	 * 
	 * @param DataTable - DT_ItemTypeMapping DataTable 에셋
	 */
	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory|Debug")
	static void DebugPrintAllMappings(const UDataTable* DataTable);
};
