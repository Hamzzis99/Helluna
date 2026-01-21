// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Inv_EquipActor.generated.h"

UCLASS()
class INVENTORY_API AInv_EquipActor : public AActor
{
	GENERATED_BODY()

public:
	AInv_EquipActor();
	FGameplayTag GetEquipmentType() const { return EquipmentType; }
	void SetEquipmentType(FGameplayTag Type) { EquipmentType = Type; }

	// ============================================
	// ⭐ [WeaponBridge] 손 무기 클래스 Getter
	// ⭐ 등 무기 BP에서 손 무기 BP를 지정하여 연결
	// ============================================
	TSubclassOf<AActor> GetHandWeaponClass() const { return HandWeaponClass; }

private:

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "장비 타입 태그"))
	FGameplayTag EquipmentType;

	// ============================================
	// ⭐ [WeaponBridge] 손에 장착할 무기 BP
	// ⭐ 팀원이 만든 AHellunaHeroWeapon 블루프린트 지정
	// ⭐ 예: BP_HeroWeapon_Axe, BP_HeroWeapon_Sword 등
	// ============================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Weapon", meta = (AllowPrivateAccess = "true", DisplayName = "손 무기 클래스"))
	TSubclassOf<AActor> HandWeaponClass;
};