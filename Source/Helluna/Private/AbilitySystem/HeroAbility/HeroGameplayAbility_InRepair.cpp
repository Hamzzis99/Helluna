// Fill out your copyright notice in the Description page of Project Settings.
// 범위 방식

#include "AbilitySystem/HeroAbility/HeroGameplayAbility_InRepair.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/HellunaWidget_SpaceShip.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "GameMode/HellunaDefenseGameState.h"

#include "DebugHelper.h"

void UHeroGameplayAbility_InRepair::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UE_LOG(LogTemp, Warning, TEXT("[InRepair] ActivateAbility 호출됨"));
	
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!ActorInfo || !ActorInfo->IsLocallyControlled())
	{
		UE_LOG(LogTemp, Warning, TEXT("[InRepair] IsLocallyControlled = FALSE, return"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[InRepair] ShowRepairUI 호출 전, RepairWidgetInstance = %s"), 
		RepairWidgetInstance ? TEXT("EXISTS") : TEXT("nullptr"));

	ShowRepairUI(ActorInfo);

	if (UHellunaWidget_SpaceShip* W = Cast<UHellunaWidget_SpaceShip>(RepairWidgetInstance))
	{
		if (AResourceUsingObject_SpaceShip* Ship = GetSpaceShip())
		{
			W->SetTargetShip(Ship);
		}
	}
}

void UHeroGameplayAbility_InRepair::EndAbility(	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	UE_LOG(LogTemp, Warning, TEXT("[InRepair] EndAbility 호출됨, bWasCancelled = %s"), 
		bWasCancelled ? TEXT("TRUE") : TEXT("FALSE"));

	if (ActorInfo && ActorInfo->IsLocallyControlled())
	{
		UE_LOG(LogTemp, Warning, TEXT("[InRepair] RemoveRepairUI 호출"));
		RemoveRepairUI();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UHeroGameplayAbility_InRepair::ShowRepairUI(const FGameplayAbilityActorInfo* ActorInfo)
{
	UE_LOG(LogTemp, Warning, TEXT("[InRepair] ShowRepairUI - RepairWidgetClass=%s, RepairWidgetInstance=%s"),
		RepairWidgetClass ? TEXT("Valid") : TEXT("nullptr"),
		RepairWidgetInstance ? TEXT("EXISTS ❌") : TEXT("nullptr ✅"));

	if (!RepairWidgetClass || RepairWidgetInstance || !ActorInfo)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InRepair] ShowRepairUI 조건 불충족! return"));
		return;
	}

	APawn* Pawn = Cast<APawn>(ActorInfo->AvatarActor.Get());
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!PC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InRepair] PC nullptr! return"));
		return;
	}

	RepairWidgetInstance = CreateWidget<UUserWidget>(PC, RepairWidgetClass);
	if (RepairWidgetInstance)
	{
		RepairWidgetInstance->AddToViewport();
		UE_LOG(LogTemp, Warning, TEXT("[InRepair] ✅ 위젯 생성 및 Viewport 추가 완료"));
	}
}

void UHeroGameplayAbility_InRepair::RemoveRepairUI()
{
	UE_LOG(LogTemp, Warning, TEXT("[InRepair] RemoveRepairUI - RepairWidgetInstance=%s"),
		RepairWidgetInstance ? TEXT("EXISTS") : TEXT("nullptr"));

	if (RepairWidgetInstance)
	{
		RepairWidgetInstance->RemoveFromParent();
		RepairWidgetInstance = nullptr;
		UE_LOG(LogTemp, Warning, TEXT("[InRepair] ✅ 위젯 제거 완료, RepairWidgetInstance = nullptr"));
	}
}

AResourceUsingObject_SpaceShip* UHeroGameplayAbility_InRepair::GetSpaceShip() const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;

	AHellunaDefenseGameState* GS = World->GetGameState<AHellunaDefenseGameState>();
	if (!GS) return nullptr;

	return GS->GetSpaceShip();

}

