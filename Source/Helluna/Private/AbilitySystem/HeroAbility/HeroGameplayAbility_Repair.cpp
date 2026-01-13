// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Repair.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Character/HellunaHeroCharacter.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "Component/RepairComponent.h"
#include "Widgets/RepairMaterialWidget.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"

#include "DebugHelper.h"

void UHeroGameplayAbility_Repair::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Repair(ActorInfo);

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UHeroGameplayAbility_Repair::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UHeroGameplayAbility_Repair::Repair(const FGameplayAbilityActorInfo* ActorInfo)
{
	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get());

	if (!Hero)
	{
		return;
	}

	// ⭐ 로컬 플레이어만 Widget 열기
	if (Hero->IsLocallyControlled())
	{
		UE_LOG(LogTemp, Warning, TEXT("=== [Repair Ability] Widget 열기 시작 ==="));

		// GameState에서 SpaceShip 가져오기
		if (AHellunaDefenseGameState* GS = GetWorld()->GetGameState<AHellunaDefenseGameState>())
		{
			if (AResourceUsingObject_SpaceShip* Ship = GS->GetSpaceShip())
			{
				// RepairComponent 찾기
				URepairComponent* RepairComp = Ship->FindComponentByClass<URepairComponent>();
				if (!RepairComp)
				{
					UE_LOG(LogTemp, Error, TEXT("  ❌ RepairComponent를 찾을 수 없음!"));
					return;
				}

				// PlayerController 가져오기
				APlayerController* PC = Hero->GetController<APlayerController>();
				if (!PC)
				{
					UE_LOG(LogTemp, Error, TEXT("  ❌ PlayerController를 찾을 수 없음!"));
					return;
				}

				// InventoryComponent 가져오기
				UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(PC);
				if (!InvComp)
				{
					UE_LOG(LogTemp, Error, TEXT("  ❌ InventoryComponent를 찾을 수 없음!"));
					return;
				}

				// ⭐ Widget 생성 및 표시
				if (RepairMaterialWidgetClass)
				{
					URepairMaterialWidget* Widget = CreateWidget<URepairMaterialWidget>(PC, RepairMaterialWidgetClass);
					if (Widget)
					{
						Widget->InitializeWidget(RepairComp, InvComp);
						Widget->AddToViewport(100);  // 최상위 Z-Order

						UE_LOG(LogTemp, Warning, TEXT("  ✅ RepairMaterial Widget 생성 완료!"));
					}
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("  ❌ RepairMaterialWidgetClass가 설정되지 않음! Blueprint에서 설정하세요!"));
				}
			}
		}
	}
}

