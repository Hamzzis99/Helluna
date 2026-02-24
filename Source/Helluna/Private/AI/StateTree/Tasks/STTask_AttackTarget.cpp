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

	if (!InstanceData.AIController)
		return EStateTreeRunStatus::Failed;

	// Attack State 진입 즉시 이동 정지
	InstanceData.AIController->StopMovement();

	// 진입 시 쿨다운 없음 → 즉시 첫 공격 가능
	InstanceData.CooldownRemaining = 0.f;

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

	UHellunaAbilitySystemComponent* ASC = Cast<UHellunaAbilitySystemComponent>(
		Enemy->GetAbilitySystemComponent());
	if (!ASC) return EStateTreeRunStatus::Failed;

	// 사용할 GA 클래스: 에디터에서 선택한 클래스, 없으면 기본 클래스 사용
	TSubclassOf<UEnemyGameplayAbility_Attack> GAClass = AttackAbilityClass.Get()
		? AttackAbilityClass
		: TSubclassOf<UEnemyGameplayAbility_Attack>(UEnemyGameplayAbility_Attack::StaticClass());

	const FHellunaAITargetData& TargetData = InstanceData.TargetData;

	// ① GA 활성 중(몽타주 재생 + AttackRecoveryDelay)이면 아무것도 하지 않음
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetClass() == GAClass)
		{
			if (Spec.IsActive())
				return EStateTreeRunStatus::Running;
			break;
		}
	}

	// ② GA 종료 후 쿨다운 대기 중: 카운트다운 + 타겟 방향 회전
	if (InstanceData.CooldownRemaining > 0.f)
	{
		InstanceData.CooldownRemaining -= DeltaTime;

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
		return EStateTreeRunStatus::Running;
	}

	// ③ 쿨다운 완료 → GA 발동
	bool bAlreadyHas = false;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetClass() == GAClass)
		{
			bAlreadyHas = true;
			break;
		}
	}
	if (!bAlreadyHas)
	{
		FGameplayAbilitySpec Spec(GAClass);
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

	const bool bActivated = ASC->TryActivateAbilityByClass(GAClass);
	if (bActivated)
	{
		// 광폭화 상태이면 쿨다운 배율 적용
		// EnrageCooldownMultiplier 가 0.5이면 절반 시간만 대기 (2배 빠른 공격)
		const float CooldownMultiplier = Enemy->bEnraged ? Enemy->EnrageCooldownMultiplier : 1.f;
		InstanceData.CooldownRemaining = AttackCooldown * CooldownMultiplier;
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_AttackTarget::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
}
