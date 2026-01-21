// ============================================
// ⭐ WeaponBridgeComponent
// ⭐ 작성자: Gihyeon
// ⭐ 목적: Inventory 플러그인 ↔ Helluna 무기 시스템 브릿지
// ⭐ 
// ⭐ [역할]
// ⭐ - EquipmentComponent의 델리게이트를 수신
// ⭐ - 무기 꺼내기: 팀원의 GA_SpawnWeapon 활성화
// ⭐ - 무기 집어넣기: CurrentWeapon Destroy
// ⭐ 
// ⭐ [흐름]
// ⭐ 1. BeginPlay에서 EquipmentComponent 찾기
// ⭐ 2. OnWeaponEquipRequested 델리게이트 바인딩
// ⭐ 3. 1키 입력 시 델리게이트 수신
// ⭐ 4. bEquip에 따라 SpawnHandWeapon(GA 활성화) 또는 DestroyHandWeapon 호출
// ============================================

#include "Component/WeaponBridgeComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "EquipmentManagement/Components/Inv_EquipmentComponent.h"
#include "EquipmentManagement/EquipActor/Inv_EquipActor.h"
#include "Weapon/HellunaHeroWeapon.h"
#include "Abilities/GameplayAbility.h"

// ============================================
// ⭐ 생성자
// ⭐ Tick 비활성화 (이벤트 기반으로만 동작)
// ============================================
UWeaponBridgeComponent::UWeaponBridgeComponent()
{
	// Tick 사용 안 함 - 델리게이트 이벤트 기반으로 동작
	PrimaryComponentTick.bCanEverTick = false;
}

// ============================================
// ⭐ BeginPlay
// ⭐ 컴포넌트 시작 시 초기화 함수 호출
// ============================================
void UWeaponBridgeComponent::BeginPlay()
{
	Super::BeginPlay();
	
	InitializeWeaponBridge();
}

// ============================================
// ⭐ InitializeWeaponBridge
// ⭐ Character, ASC, EquipmentComponent 참조 획득
// ⭐ EquipmentComponent의 델리게이트에 바인딩
// ============================================
void UWeaponBridgeComponent::InitializeWeaponBridge()
{
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] InitializeWeaponBridge 시작"));
	
	// ⭐ Character 참조 가져오기
	// 이 컴포넌트는 HellunaHeroCharacter에 부착됨
	OwningCharacter = Cast<AHellunaHeroCharacter>(GetOwner());
	if (!OwningCharacter.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] OwningCharacter가 null! GetOwner: %s"), 
			GetOwner() ? *GetOwner()->GetName() : TEXT("nullptr"));
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] OwningCharacter 찾음: %s"), *OwningCharacter->GetName());
	
	// ⭐ ASC 참조 가져오기 (GA 활성화에 필수!)
	AbilitySystemComponent = Cast<UHellunaAbilitySystemComponent>(
		OwningCharacter->GetAbilitySystemComponent()
	);
	if (AbilitySystemComponent.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] AbilitySystemComponent 찾음"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] AbilitySystemComponent를 찾을 수 없음!"));
	}
	
	// ⭐ EquipmentComponent 찾기
	// EquipmentComponent는 PlayerController에 부착되어 있음
	if (APlayerController* PC = Cast<APlayerController>(OwningCharacter->GetController()))
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] PlayerController 찾음: %s"), *PC->GetName());
		
		EquipmentComponent = PC->FindComponentByClass<UInv_EquipmentComponent>();
		
		if (EquipmentComponent.IsValid())
		{
			// ⭐ 델리게이트 바인딩
			// 이미 바인딩되어 있으면 중복 방지
			if (!EquipmentComponent->OnWeaponEquipRequested.IsAlreadyBound(this, &ThisClass::OnWeaponEquipRequested))
			{
				EquipmentComponent->OnWeaponEquipRequested.AddDynamic(this, &ThisClass::OnWeaponEquipRequested);
				UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 델리게이트 바인딩 성공!"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] EquipmentComponent를 찾을 수 없음!"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] PlayerController를 찾을 수 없음!"));
	}
}

// ============================================
// ⭐ OnWeaponEquipRequested (델리게이트 콜백)
// ⭐ EquipmentComponent에서 무기 꺼내기/집어넣기 요청 시 호출됨
// ⭐ 
// ⭐ @param WeaponTag: 무기 종류 태그 (예: GameItems.Equipment.Weapons.Axe)
// ⭐ @param BackWeaponActor: 등에 붙은 무기 Actor (Hidden 처리용)
// ⭐ @param SpawnWeaponAbility: 활성화할 GA 클래스 (팀원의 GA_SpawnWeapon)
// ⭐ @param bEquip: true=꺼내기, false=집어넣기
// ⭐ @param WeaponSlotIndex: 무기 슬롯 인덱스 (0=주무기, 1=보조무기)
// ============================================
void UWeaponBridgeComponent::OnWeaponEquipRequested(
	const FGameplayTag& WeaponTag,
	AInv_EquipActor* BackWeaponActor,
	TSubclassOf<UGameplayAbility> SpawnWeaponAbility,
	bool bEquip,
	int32 WeaponSlotIndex)
{
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] OnWeaponEquipRequested 수신!"));
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] - WeaponTag: %s"), *WeaponTag.ToString());
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] - BackWeaponActor: %s"), 
		BackWeaponActor ? *BackWeaponActor->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] - SpawnWeaponAbility: %s"), 
		SpawnWeaponAbility ? *SpawnWeaponAbility->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] - bEquip: %s"), bEquip ? TEXT("true (꺼내기)") : TEXT("false (집어넣기)"));
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] - WeaponSlotIndex: %d (%s)"), 
		WeaponSlotIndex, WeaponSlotIndex == 0 ? TEXT("주무기") : TEXT("보조무기"));
	
	if (bEquip)
	{
		// ⭐ 무기 꺼내기 - GA 활성화
		SpawnHandWeapon(SpawnWeaponAbility);
	}
	else
	{
		// ⭐ 무기 집어넣기
		DestroyHandWeapon();
	}
}

// ============================================
// ⭐ SpawnHandWeapon
// ⭐ 팀원의 GA_SpawnWeapon을 활성화하여 무기 스폰
// ⭐ 
// ⭐ @param SpawnWeaponAbility: 활성화할 GA 클래스
// ⭐ 
// ⭐ [처리 과정]
// ⭐ 1. ASC 유효성 검사
// ⭐ 2. GA 클래스 유효성 검사
// ⭐ 3. TryActivateAbilityByClass() 호출
// ⭐ 4. GA 내부에서 Server_RequestSpawnWeapon 등 처리
// ============================================
void UWeaponBridgeComponent::SpawnHandWeapon(TSubclassOf<UGameplayAbility> SpawnWeaponAbility)
{
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] SpawnHandWeapon 시작 (GA 방식)"));
	
	if (!SpawnWeaponAbility)
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] SpawnWeaponAbility가 null!"));
		return;
	}
	
	if (!AbilitySystemComponent.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] AbilitySystemComponent가 null!"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] GA 활성화 시도: %s"), *SpawnWeaponAbility->GetName());
	
	// ⭐ GA 활성화!
	// 팀원의 GA_SpawnWeapon 내부에서 Server_RequestSpawnWeapon, 몽타주 등 처리됨
	bool bSuccess = AbilitySystemComponent->TryActivateAbilityByClass(SpawnWeaponAbility);
	
	if (bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] GA 활성화 성공!"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] GA 활성화 실패!"));
	}
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] SpawnHandWeapon 완료"));
}

// ============================================
// ⭐ DestroyHandWeapon
// ⭐ 손에 든 무기를 제거하는 함수
// ⭐ Server RPC를 통해 서버에서 CurrentWeapon Destroy
// ⭐ 
// ⭐ [호출 시점]
// ⭐ - 무기 집어넣기 (1키 다시 누름)
// ⭐ - 무기 전환 (주무기 → 보조무기)
// ============================================
void UWeaponBridgeComponent::DestroyHandWeapon()
{
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] DestroyHandWeapon 시작"));
	
	if (!OwningCharacter.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] OwningCharacter가 null!"));
		return;
	}
	
	// ⭐ Server RPC 호출하여 서버에서 Destroy
	// CurrentWeapon은 서버에서 스폰되어 리플리케이트된 액터이므로 서버에서만 Destroy 가능
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] Server_RequestDestroyWeapon 호출"));
	OwningCharacter->Server_RequestDestroyWeapon();
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] DestroyHandWeapon 완료"));
}
