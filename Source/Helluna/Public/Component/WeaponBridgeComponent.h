// ============================================
// ⭐ WeaponBridgeComponent
// ⭐ 작성자: Gihyeon
// ⭐ 목적: Inventory 플러그인과 Helluna 무기 시스템 연동
// ⭐ - EquipmentComponent 델리게이트 수신
// ⭐ - 손 무기 스폰 (팀원의 Server_RequestSpawnWeapon 호출)
// ⭐ - 손 무기 Destroy
// ============================================

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "WeaponBridgeComponent.generated.h"

class AHellunaHeroCharacter;
class UHellunaAbilitySystemComponent;
class AInv_EquipActor;
class UInv_EquipmentComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class HELLUNA_API UWeaponBridgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponBridgeComponent();

protected:
	virtual void BeginPlay() override;

private:
	// ============================================
	// ⭐ 참조
	// ============================================
	
	TWeakObjectPtr<AHellunaHeroCharacter> OwningCharacter;
	TWeakObjectPtr<UHellunaAbilitySystemComponent> AbilitySystemComponent;
	TWeakObjectPtr<UInv_EquipmentComponent> EquipmentComponent;
	
	// ============================================
	// ⭐ 초기화
	// ============================================
	
	// EquipmentComponent 찾아서 델리게이트 바인딩
	void InitializeWeaponBridge();
	
	// ============================================
	// ⭐ 델리게이트 콜백
	// ============================================
	
	// Inventory에서 무기 꺼내기/집어넣기 요청 시 호출
	UFUNCTION()
	void OnWeaponEquipRequested(
		const FGameplayTag& WeaponTag,
		AInv_EquipActor* BackWeaponActor,
		TSubclassOf<AActor> HandWeaponClass,
		bool bEquip
	);
	
	// ============================================
	// ⭐ 손 무기 관리
	// ============================================
	
	// 손 무기 스폰 (팀원의 Server_RequestSpawnWeapon 호출)
	void SpawnHandWeapon(TSubclassOf<AActor> HandWeaponClass);
	
	// 손 무기 Destroy
	void DestroyHandWeapon();
};
