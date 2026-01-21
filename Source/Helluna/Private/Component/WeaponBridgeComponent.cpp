// ============================================
// ⭐ WeaponBridgeComponent
// ⭐ 작성자: Gihyeon
// ⭐ Inventory 플러그인 ↔ Helluna 무기 시스템 브릿지
// ============================================

#include "Component/WeaponBridgeComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "EquipmentManagement/Components/Inv_EquipmentComponent.h"
#include "Weapon/HellunaHeroWeapon.h"

UWeaponBridgeComponent::UWeaponBridgeComponent()
{
	PrimaryActorTick.bCanEverTick = false;
}

void UWeaponBridgeComponent::BeginPlay()
{
	Super::BeginPlay();
	
	InitializeWeaponBridge();
}

void UWeaponBridgeComponent::InitializeWeaponBridge()
{
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] InitializeWeaponBridge 시작"));
	
	// ⭐ Character 참조 가져오기
	OwningCharacter = Cast<AHellunaHeroCharacter>(GetOwner());
	if (!OwningCharacter.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] OwningCharacter가 null! GetOwner: %s"), 
			GetOwner() ? *GetOwner()->GetName() : TEXT("nullptr"));
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] OwningCharacter 찾음: %s"), *OwningCharacter->GetName());
	
	// ⭐ ASC 참조 가져오기
	AbilitySystemComponent = Cast<UHellunaAbilitySystemComponent>(
		OwningCharacter->GetAbilitySystemComponent()
	);
	if (AbilitySystemComponent.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] AbilitySystemComponent 찾음"));
	}
	
	// ⭐ EquipmentComponent 찾기 (PlayerController에 있음)
	if (APlayerController* PC = Cast<APlayerController>(OwningCharacter->GetController()))
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] PlayerController 찾음: %s"), *PC->GetName());
		
		EquipmentComponent = PC->FindComponentByClass<UInv_EquipmentComponent>();
		
		if (EquipmentComponent.IsValid())
		{
			// ⭐ 델리게이트 바인딩
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
	TSubclassOf<AHellunaHeroWeapon> HeroWeaponClass = *HandWeaponClass;
	if (!HeroWeaponClass)
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] HandWeaponClass가 AHellunaHeroWeapon이 아님!"));
		return;
	}
	
	// ⭐ WeaponClass의 CDO에서 몽타주 가져오기
	AHellunaHeroWeapon* WeaponCDO = HeroWeaponClass->GetDefaultObject<AHellunaHeroWeapon>();
	UAnimMontage* EquipMontage = WeaponCDO ? WeaponCDO->GetAnimSet().Equip : nullptr;
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] WeaponClass: %s"), *HeroWeaponClass->GetName());
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] EquipMontage: %s"), 
		EquipMontage ? *EquipMontage->GetName() : TEXT("nullptr"));
	
	// ⭐ 팀원의 Server_RequestSpawnWeapon 직접 호출
	FName SocketName = TEXT("WeaponSocket");
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] Server_RequestSpawnWeapon 호출 - Socket: %s"), *SocketName.ToString());
	
	OwningCharacter->Server_RequestSpawnWeapon(HeroWeaponClass, SocketName, EquipMontage);
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] SpawnHandWeapon 완료"));
}

void UWeaponBridgeComponent::DestroyHandWeapon()
{
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] DestroyHandWeapon 시작"));
	
	if (!OwningCharacter.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] OwningCharacter가 null!"));
		return;
	}
	
	// ⭐ CurrentWeapon Destroy
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
