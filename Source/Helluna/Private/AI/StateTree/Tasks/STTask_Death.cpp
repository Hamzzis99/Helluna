/**
 * STTask_Death.cpp
 *
 * @author 김민우
 */

#include "AI/StateTree/Tasks/STTask_Death.h"

#include "AIController.h"
#include "StateTreeExecutionContext.h"

#include "Character/HellunaEnemyCharacter.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_Death.h"

EStateTreeRunStatus FSTTask_Death::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	InstanceData.bMontageFinished = false;
	InstanceData.FallbackTimer    = 0.f;
	InstanceData.bUseFallback     = false;

	AAIController* AIC = InstanceData.AIController;
	if (!AIC)
		return EStateTreeRunStatus::Failed;

	AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(AIC->GetPawn());
	if (!Enemy)
		return EStateTreeRunStatus::Failed;

	AIC->StopMovement();

	UHellunaAbilitySystemComponent* ASC = Cast<UHellunaAbilitySystemComponent>(
		Enemy->GetAbilitySystemComponent());

	if (ASC)
	{
		bool bAlreadyHas = false;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (Spec.Ability && Spec.Ability->GetClass() == UEnemyGameplayAbility_Death::StaticClass())
			{
				bAlreadyHas = true;
				break;
			}
		}
		if (!bAlreadyHas)
		{
			FGameplayAbilitySpec Spec(UEnemyGameplayAbility_Death::StaticClass());
			Spec.SourceObject = Enemy;
			Spec.Level = 1;
			ASC->GiveAbility(Spec);
		}

		const bool bActivated = ASC->TryActivateAbilityByClass(UEnemyGameplayAbility_Death::StaticClass());
		if (bActivated)
		{
			FInstanceDataType* DataPtr = &InstanceData;
			Enemy->OnDeathMontageFinished.BindLambda([DataPtr]()
			{
				DataPtr->bMontageFinished = true;
			});
		}
		else
		{
			InstanceData.bUseFallback = true;
		}
	}
	else
	{
		InstanceData.bUseFallback = true;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_Death::Tick(
	FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (InstanceData.bMontageFinished)
		return EStateTreeRunStatus::Succeeded;

	if (InstanceData.bUseFallback)
	{
		InstanceData.FallbackTimer += DeltaTime;
		if (InstanceData.FallbackTimer >= FallbackDuration)
		{
			if (AAIController* AIC = InstanceData.AIController)
			{
				if (AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(AIC->GetPawn()))
				{
					Enemy->DespawnMassEntityOnServer(TEXT("STTask_Death_Fallback"));
					Enemy->SetLifeSpan(0.1f);
				}
			}
			return EStateTreeRunStatus::Succeeded;
		}
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_Death::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (AAIController* AIC = InstanceData.AIController)
	{
		if (AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(AIC->GetPawn()))
		{
			Enemy->OnDeathMontageFinished.Unbind();
		}
	}
}
