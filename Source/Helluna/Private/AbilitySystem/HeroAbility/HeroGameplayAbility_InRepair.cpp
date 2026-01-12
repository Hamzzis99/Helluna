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
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!ActorInfo || !ActorInfo->IsLocallyControlled())
		return;

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
	if (ActorInfo && ActorInfo->IsLocallyControlled())
	{
		RemoveRepairUI();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UHeroGameplayAbility_InRepair::ShowRepairUI(const FGameplayAbilityActorInfo* ActorInfo)
{
	if (!RepairWidgetClass || RepairWidgetInstance || !ActorInfo)
		return;

	APawn* Pawn = Cast<APawn>(ActorInfo->AvatarActor.Get());
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!PC)
		return;

	RepairWidgetInstance = CreateWidget<UUserWidget>(PC, RepairWidgetClass);
	if (RepairWidgetInstance)
	{
		RepairWidgetInstance->AddToViewport();
	}

}

void UHeroGameplayAbility_InRepair::RemoveRepairUI()
{
	if (RepairWidgetInstance)
	{
		RepairWidgetInstance->RemoveFromParent();
		RepairWidgetInstance = nullptr;
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

