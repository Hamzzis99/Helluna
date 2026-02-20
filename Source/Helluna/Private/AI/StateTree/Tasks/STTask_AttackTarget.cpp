/**
 * STTask_AttackTarget.cpp
 *
 * @author 김민우
 */

#include "AI/StateTree/Tasks/STTask_AttackTarget.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"

#include "Character/HellunaEnemyCharacter.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_Attack.h"

EStateTreeRunStatus FSTTask_AttackTarget::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	InstanceData.AttackCooldownRemaining = 0.f;

	if (!InstanceData.AIController)
		return EStateTreeRunStatus::Failed;

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_AttackTarget::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	AAIController* AIController = InstanceData.AIController;
	if (!AIController) return EStateTreeRunStatus::Failed;

	APawn* Pawn = AIController->GetPawn();
	if (!Pawn) return EStateTreeRunStatus::Failed;

	AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Pawn);
	if (!Enemy) return EStateTreeRunStatus::Failed;

	const FHellunaAITargetData& TargetData = InstanceData.TargetData;

	if (InstanceData.AttackCooldownRemaining > 0.f)
	{
		InstanceData.AttackCooldownRemaining -= DeltaTime;
		return EStateTreeRunStatus::Running;
	}

	// 타겟 방향으로 회전
	if (TargetData.IsValid())
	{
		const FVector ToTarget = (TargetData.TargetActor->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal();
		if (!ToTarget.IsNearlyZero())
		{
			const FRotator CurrentRot = Pawn->GetActorRotation();
			const FRotator TargetRot  = FRotator(0.f, ToTarget.Rotation().Yaw, 0.f);
			Pawn->SetActorRotation(FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, RotationSpeed));
		}
	}

	UHellunaAbilitySystemComponent* ASC = Cast<UHellunaAbilitySystemComponent>(
		Enemy->GetAbilitySystemComponent());
	if (!ASC)
		return EStateTreeRunStatus::Failed;

	bool bAlreadyHas = false;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetClass() == UEnemyGameplayAbility_Attack::StaticClass())
		{
			bAlreadyHas = true;
			break;
		}
	}
	if (!bAlreadyHas)
	{
		FGameplayAbilitySpec Spec(UEnemyGameplayAbility_Attack::StaticClass());
		Spec.SourceObject = Enemy;
		Spec.Level = 1;
		ASC->GiveAbility(Spec);
	}

	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (UEnemyGameplayAbility_Attack* AttackGA = Cast<UEnemyGameplayAbility_Attack>(Spec.Ability))
		{
			AttackGA->CurrentTarget = TargetData.TargetActor.Get();
			break;
		}
	}

	const bool bActivated = ASC->TryActivateAbilityByClass(UEnemyGameplayAbility_Attack::StaticClass());
	if (bActivated)
		InstanceData.AttackCooldownRemaining = AttackCooldown;

	return EStateTreeRunStatus::Running;
}

void FSTTask_AttackTarget::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
}
