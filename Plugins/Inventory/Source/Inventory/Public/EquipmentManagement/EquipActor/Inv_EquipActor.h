// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Inv_EquipActor.generated.h"

class UGameplayAbility;

UCLASS()
class INVENTORY_API AInv_EquipActor : public AActor
{
	GENERATED_BODY()

public:
	AInv_EquipActor();
	
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	FGameplayTag GetEquipmentType() const { return EquipmentType; }
	void SetEquipmentType(FGameplayTag Type) { EquipmentType = Type; }

	// ============================================
	// ⭐ [WeaponBridge] 무기 스폰 GA 클래스 Getter
	// ⭐ 팀원의 GA_SpawnWeapon을 직접 호출하기 위함
	// ============================================
	TSubclassOf<UGameplayAbility> GetSpawnWeaponAbility() const { return SpawnWeaponAbility; }

	// ============================================
	// ⭐ [WeaponBridge] 무기 슬롯 인덱스 (0=주무기, 1=보조무기)
	// ============================================
	int32 GetWeaponSlotIndex() const { return WeaponSlotIndex; }
	void SetWeaponSlotIndex(int32 Index) { WeaponSlotIndex = Index; }

private:

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "장비 타입 태그"))
	FGameplayTag EquipmentType;

	// ============================================
	// ⭐ [WeaponBridge] 무기 스폰 GA
	// ⭐ 팀원이 만든 GA_Hero_SpawnWeapon 블루프린트 지정
	// ⭐ 1키 입력 시 이 GA를 활성화하여 무기 스폰
	// ⭐ 예: GA_Hero_SpawnWeapon (도끼), GA_Hero_SpawnWeapon2 (총) 등
	// ============================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Weapon", meta = (AllowPrivateAccess = "true", DisplayName = "무기 스폰 GA"))
	TSubclassOf<UGameplayAbility> SpawnWeaponAbility;

	// ============================================
	// ⭐ [WeaponBridge] 무기 슬롯 인덱스
	// ⭐ 0 = 주무기 슬롯, 1 = 보조무기 슬롯
	// ⭐ 장착 시 EquipmentComponent에서 설정
	// ============================================
	UPROPERTY(Replicated, VisibleAnywhere, Category = "Inventory|Weapon", meta = (DisplayName = "무기 슬롯 인덱스"))
	int32 WeaponSlotIndex = -1;
};