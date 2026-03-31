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
	{
		UE_LOG(LogTemp, Warning, TEXT("[DeathDiag][DeathTaskEnterFail] Reason=MissingAIController"));
		return EStateTreeRunStatus::Failed;
	}

	AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(AIC->GetPawn());
	if (!Enemy)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DeathDiag][DeathTaskEnterFail] AIController=%s Reason=MissingEnemyPawn"),
			*GetNameSafe(AIC));
		return EStateTreeRunStatus::Failed;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[DeathDiag][DeathTaskEnter] Enemy=%s AIController=%s"),
		*GetNameSafe(Enemy),
		*GetNameSafe(AIC));

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
			UE_LOG(LogTemp, Warning,
				TEXT("[DeathDiag][DeathTaskActivateGA] Enemy=%s Result=Activated"),
				*GetNameSafe(Enemy));
			FInstanceDataType* DataPtr = &InstanceData;
			Enemy->OnDeathMontageFinished.BindLambda([DataPtr]()
			{
				DataPtr->bMontageFinished = true;
			});
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[DeathDiag][DeathTaskActivateGA] Enemy=%s Result=Failed Fallback=Enabled"),
				*GetNameSafe(Enemy));
			InstanceData.bUseFallback = true;
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DeathDiag][DeathTaskNoASC] Enemy=%s Fallback=Enabled"),
			*GetNameSafe(Enemy));
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
	{
		if (AAIController* AIC = InstanceData.AIController)
		{
			if (AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(AIC->GetPawn()))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[DeathDiag][DeathTaskComplete] Enemy=%s Reason=MontageFinished"),
					*GetNameSafe(Enemy));
			}
		}
		return EStateTreeRunStatus::Succeeded;
	}

	if (InstanceData.bUseFallback)
	{
		InstanceData.FallbackTimer += DeltaTime;
		if (InstanceData.FallbackTimer >= FallbackDuration)
		{
			if (AAIController* AIC = InstanceData.AIController)
			{
				if (AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(AIC->GetPawn()))
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[DeathDiag][DeathTaskFallbackDespawn] Enemy=%s Elapsed=%.2f"),
						*GetNameSafe(Enemy),
						InstanceData.FallbackTimer);
					// DespawnMassEntityOnServer가 Mass Entity 정리 + Destroy()까지 처리
					Enemy->DespawnMassEntityOnServer(TEXT("STTask_Death_Fallback"));
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
