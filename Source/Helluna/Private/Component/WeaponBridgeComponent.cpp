// ============================================
// ⭐ WeaponBridgeComponent
// ⭐ 작성자: Gihyeon
// ⭐ 목적: Inventory 플러그인 ↔ Helluna 무기 시스템 브릿지
// ⭐ 
// ⭐ [역할]
// ⭐ - EquipmentComponent의 델리게이트를 수신
// ⭐ - 무기 꺼내기: 팀원의 Server_RequestSpawnWeapon() 호출
// ⭐ - 무기 집어넣기: CurrentWeapon Destroy
// ⭐ 
// ⭐ [흐름]
// ⭐ 1. BeginPlay에서 EquipmentComponent 찾기
// ⭐ 2. OnWeaponEquipRequested 델리게이트 바인딩
// ⭐ 3. 1키 입력 시 델리게이트 수신
// ⭐ 4. bEquip에 따라 SpawnHandWeapon 또는 DestroyHandWeapon 호출
// ============================================

#include "Component/WeaponBridgeComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "EquipmentManagement/Components/Inv_EquipmentComponent.h"
#include "EquipmentManagement/EquipActor/Inv_EquipActor.h"
#include "Weapon/HellunaHeroWeapon.h"

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
	
	// ⭐ ASC 참조 가져오기 (나중에 GA 활성화에 사용 가능)
	AbilitySystemComponent = Cast<UHellunaAbilitySystemComponent>(
		OwningCharacter->GetAbilitySystemComponent()
	);
	if (AbilitySystemComponent.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] AbilitySystemComponent 찾음"));
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
// ⭐ @param HandWeaponClass: 손에 스폰할 무기 BP 클래스
// ⭐ @param bEquip: true=꺼내기, false=집어넣기
// ============================================
void UWeaponBridgeComponent::OnWeaponEquipRequested(
	const FGameplayTag& WeaponTag,
	AInv_EquipActor* BackWeaponActor,
	TSubclassOf<AActor> HandWeaponClass,
	bool bEquip)
{
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] OnWeaponEquipRequested 수신!"));
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] - WeaponTag: %s"), *WeaponTag.ToString());
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] - BackWeaponActor: %s"), 
		BackWeaponActor ? *BackWeaponActor->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] - HandWeaponClass: %s"), 
		HandWeaponClass ? *HandWeaponClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] - bEquip: %s"), bEquip ? TEXT("true (꺼내기)") : TEXT("false (집어넣기)"));
	
	if (bEquip)
	{
		// ⭐ 무기 꺼내기
		SpawnHandWeapon(HandWeaponClass);
	}
	else
	{
		// ⭐ 무기 집어넣기
		DestroyHandWeapon();
	}
}

// ============================================
// ⭐ SpawnHandWeapon
// ⭐ 손에 무기를 스폰하는 함수
// ⭐ 팀원이 만든 Server_RequestSpawnWeapon() RPC를 직접 호출
// ⭐ 
// ⭐ @param HandWeaponClass: 스폰할 무기 BP 클래스 (AHellunaHeroWeapon 상속)
// ⭐ 
// ⭐ [처리 과정]
// ⭐ 1. HandWeaponClass를 AHellunaHeroWeapon으로 캐스트
// ⭐ 2. WeaponClass의 CDO에서 Equip 몽타주 가져오기
// ⭐ 3. Server_RequestSpawnWeapon() 호출 → 서버에서 무기 스폰
// ============================================
void UWeaponBridgeComponent::SpawnHandWeapon(TSubclassOf<AActor> HandWeaponClass)
{
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] SpawnHandWeapon 시작"));
	
	if (!HandWeaponClass)
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] HandWeaponClass가 null!"));
		return;
	}
	
	if (!OwningCharacter.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] OwningCharacter가 null!"));
		return;
	}
	
	// ⭐ HandWeaponClass를 AHellunaHeroWeapon으로 캐스트
	// AActor로 받았지만 실제로는 AHellunaHeroWeapon이어야 함
	TSubclassOf<AHellunaHeroWeapon> HeroWeaponClass = *HandWeaponClass;
	if (!HeroWeaponClass)
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] HandWeaponClass가 AHellunaHeroWeapon이 아님!"));
		return;
	}
	
	// ⭐ WeaponClass의 CDO(Class Default Object)에서 몽타주 가져오기
	// CDO는 블루프린트에서 설정한 기본값을 가지고 있음
	AHellunaHeroWeapon* WeaponCDO = HeroWeaponClass->GetDefaultObject<AHellunaHeroWeapon>();
	UAnimMontage* EquipMontage = WeaponCDO ? WeaponCDO->GetAnimSet().Equip : nullptr;
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] WeaponClass: %s"), *HeroWeaponClass->GetName());
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] EquipMontage: %s"), 
		EquipMontage ? *EquipMontage->GetName() : TEXT("nullptr"));
	
	// ⭐ 팀원의 Server_RequestSpawnWeapon 직접 호출
	// 이 RPC는 서버에서 무기를 스폰하고 소켓에 부착함
	FName SocketName = TEXT("WeaponSocket");
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] Server_RequestSpawnWeapon 호출 - Socket: %s"), *SocketName.ToString());
	
	OwningCharacter->Server_RequestSpawnWeapon(HeroWeaponClass, SocketName, EquipMontage);
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] SpawnHandWeapon 완료"));
}

// ============================================
// ⭐ DestroyHandWeapon
// ⭐ 손에 든 무기를 제거하는 함수
// ⭐ CurrentWeapon을 Destroy하고 nullptr로 설정
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
	
	// ⭐ CurrentWeapon Destroy
	// CurrentWeapon은 HellunaHeroCharacter에서 관리하는 현재 손에 든 무기
	AHellunaHeroWeapon* CurrentWeapon = OwningCharacter->GetCurrentWeapon();
	if (IsValid(CurrentWeapon))
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] CurrentWeapon Destroy: %s"), *CurrentWeapon->GetName());
		
		CurrentWeapon->Destroy();
		OwningCharacter->SetCurrentWeapon(nullptr);
		
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 손 무기 Destroy 완료!"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] CurrentWeapon이 이미 null - Destroy 불필요"));
	}
}
