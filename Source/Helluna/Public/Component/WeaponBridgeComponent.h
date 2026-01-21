// ============================================
// ⭐ WeaponBridgeComponent
// ⭐ 작성자: Gihyeon
// ⭐ 목적: Inventory 플러그인과 Helluna 무기 시스템 연동
// ⭐ 
// ⭐ [주요 역할]
// ⭐ - EquipmentComponent의 델리게이트(OnWeaponEquipRequested) 수신
// ⭐ - 무기 꺼내기: 팀원의 GA_SpawnWeapon 활성화
// ⭐ - 무기 집어넣기: CurrentWeapon Destroy
// ⭐ 
// ⭐ [위치]
// ⭐ - HellunaHeroCharacter에 부착됨 (생성자에서 CreateDefaultSubobject)
// ⭐ 
// ⭐ [의존성]
// ⭐ - Inventory 플러그인: Inv_EquipmentComponent, Inv_EquipActor
// ⭐ - Helluna 모듈: HellunaHeroCharacter, HellunaHeroWeapon
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
class UGameplayAbility;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class HELLUNA_API UWeaponBridgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// ⭐ 생성자
	UWeaponBridgeComponent();

protected:
	// ⭐ BeginPlay - 초기화 시작점
	virtual void BeginPlay() override;

private:
	// ============================================
	// ⭐ 참조 변수들
	// ============================================
	
	// 소유 캐릭터 (HellunaHeroCharacter)
	// CurrentWeapon 접근 및 GA 활성화에 사용
	TWeakObjectPtr<AHellunaHeroCharacter> OwningCharacter;
	
	// AbilitySystemComponent
	// GA 활성화에 사용 (TryActivateAbilityByClass)
	TWeakObjectPtr<UHellunaAbilitySystemComponent> AbilitySystemComponent;
	
	// EquipmentComponent (PlayerController에 부착됨)
	// 델리게이트 바인딩 대상
	TWeakObjectPtr<UInv_EquipmentComponent> EquipmentComponent;
	
	// ============================================
	// ⭐ 초기화 함수
	// ============================================
	
	// EquipmentComponent 찾아서 델리게이트 바인딩
	// BeginPlay에서 호출됨
	void InitializeWeaponBridge();
	
	// ============================================
	// ⭐ 델리게이트 콜백 함수
	// ============================================
	
	// Inventory에서 무기 꺼내기/집어넣기 요청 시 호출
	// @param WeaponTag: 무기 종류 태그 (예: GameItems.Equipment.Weapons.Axe)
	// @param BackWeaponActor: 등에 붙은 무기 Actor
	// @param SpawnWeaponAbility: 활성화할 GA 클래스 (팀원의 GA_SpawnWeapon)
	// @param bEquip: true=꺼내기, false=집어넣기
	// @param WeaponSlotIndex: 무기 슬롯 인덱스 (0=주무기, 1=보조무기)
	UFUNCTION()
	void OnWeaponEquipRequested(
		const FGameplayTag& WeaponTag,
		AInv_EquipActor* BackWeaponActor,
		TSubclassOf<UGameplayAbility> SpawnWeaponAbility,
		bool bEquip,
		int32 WeaponSlotIndex
	);
	
	// ============================================
	// ⭐ 손 무기 관리 함수
	// ============================================
	
	// 손 무기 스폰 (GA 활성화)
	// 팀원의 GA_SpawnWeapon을 TryActivateAbilityByClass로 호출
	// @param SpawnWeaponAbility: 활성화할 GA 클래스
	void SpawnHandWeapon(TSubclassOf<UGameplayAbility> SpawnWeaponAbility);
	
	// 손 무기 제거
	// CurrentWeapon을 Destroy하고 nullptr로 설정
	void DestroyHandWeapon();
};
