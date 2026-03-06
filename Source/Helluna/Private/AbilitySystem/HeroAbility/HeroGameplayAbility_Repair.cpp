// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Repair.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Character/HellunaHeroCharacter.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "Component/RepairComponent.h"
#include "Widgets/RepairMaterialWidget.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"

#include "DebugHelper.h"
#include "Helluna.h"

void UHeroGameplayAbility_Repair::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	Repair(ActorInfo);

	// ⭐ Widget 열고 바로 종료! (Widget은 독립적으로 동작)
	// 이렇게 해야 다음에 다시 활성화 가능
	EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
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

	// ⭐ 로컬 플레이어만 Widget 열기/닫기
	if (Hero->IsLocallyControlled())
	{
		// ⭐⭐⭐ F키 토글: Widget이 이미 열려있으면 닫기!
		if (CurrentWidget && CurrentWidget->IsInViewport())
		{
#if HELLUNA_DEBUG_REPAIR
			UE_LOG(LogTemp, Warning, TEXT("=== [Repair Ability] Widget이 이미 열려있음! 닫기 ==="));
#endif
			CurrentWidget->CloseWidget();
			CurrentWidget = nullptr;
			return;
		}

#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Warning, TEXT("=== [Repair Ability] Widget 열기 시작 ==="));
#endif

		// GameState에서 SpaceShip 가져오기
		if (AHellunaDefenseGameState* GS = GetWorld()->GetGameState<AHellunaDefenseGameState>())
		{
			if (AResourceUsingObject_SpaceShip* Ship = GS->GetSpaceShip())
			{
				// RepairComponent 찾기
				URepairComponent* RepairComp = Ship->FindComponentByClass<URepairComponent>();
				if (!RepairComp)
				{
#if HELLUNA_DEBUG_REPAIR
					UE_LOG(LogTemp, Error, TEXT("  ❌ RepairComponent를 찾을 수 없음!"));
#endif
					return;
				}

				// PlayerController 가져오기
				APlayerController* PC = Hero->GetController<APlayerController>();
				if (!PC)
				{
#if HELLUNA_DEBUG_REPAIR
					UE_LOG(LogTemp, Error, TEXT("  ❌ PlayerController를 찾을 수 없음!"));
#endif
					return;
				}

				// InventoryComponent 가져오기
				UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(PC);
				if (!InvComp)
				{
#if HELLUNA_DEBUG_REPAIR
					UE_LOG(LogTemp, Error, TEXT("  ❌ InventoryComponent를 찾을 수 없음!"));
#endif
					return;
				}

				// ⭐ Widget 생성 및 표시
				if (RepairMaterialWidgetClass)
				{
					CurrentWidget = CreateWidget<URepairMaterialWidget>(PC, RepairMaterialWidgetClass);
					if (CurrentWidget)
					{
						CurrentWidget->InitializeWidget(RepairComp, InvComp);
						CurrentWidget->AddToViewport(100);  // 최상위 Z-Order

						// ⭐ 마우스 커서 표시 및 입력 모드 변경
						PC->FlushPressedKeys();
						PC->SetInputMode(FInputModeUIOnly());
						PC->bShowMouseCursor = true;

#if HELLUNA_DEBUG_REPAIR
						UE_LOG(LogTemp, Warning, TEXT("  ✅ RepairMaterial Widget 생성 완료!"));
						UE_LOG(LogTemp, Warning, TEXT("  🖱️ 마우스 커서 활성화!"));
#endif
					}
				}
				else
				{
#if HELLUNA_DEBUG_REPAIR
					UE_LOG(LogTemp, Error, TEXT("  ❌ RepairMaterialWidgetClass가 설정되지 않음! Blueprint에서 설정하세요!"));
#endif
				}
			}
		}
	}
}

