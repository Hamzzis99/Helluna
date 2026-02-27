/**
 * STTask_AttackTarget.cpp
 *
 * 공격 GA를 주기적으로 발동하고 쿨다운을 관리한다.
 *
 * ─── Tick 흐름 ───────────────────────────────────────────────
 *  ① GA 활성 중 → Running (GA 내부에서 몽타주 + 경직 처리)
 *  ② GA 종료 + 쿨다운 중 → 카운트다운 + 타겟 방향 회전
 *  ③ 쿨다운 완료 → GA 발동 + CooldownRemaining 초기화
 *
 *  광폭화 시: CooldownRemaining *= EnrageCooldownMultiplier (0.5 → 2배 빠름)
 *
 * @author 김민우
 */

#include "AI/StateTree/Tasks/STTask_AttackTarget.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "Character/HellunaEnemyCharacter.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_Attack.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_RangedAttack.h"

EStateTreeRunStatus FSTTask_AttackTarget::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.AIController)
		return EStateTreeRunStatus::Failed;

	// Attack State 진입: 이동 정지 + 타겟 방향 회전 모드 전환
	InstanceData.AIController->StopMovement();

	APawn* Pawn = InstanceData.AIController->GetPawn();
	if (UCharacterMovementComponent* MoveComp = Pawn ? Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent()) : nullptr)
	{
		// bOrientRotationToMovement OFF, bUseControllerDesiredRotation ON
		// → SetFocus 방향으로 부드럽게 회전
		MoveComp->bOrientRotationToMovement     = false;
		MoveComp->bUseControllerDesiredRotation = true;
	}

	const FHellunaAITargetData& TargetData = Context.GetInstanceData(*this).TargetData;
	if (TargetData.HasValidTarget())
		InstanceData.AIController->SetFocus(TargetData.TargetActor.Get());

	InstanceData.CooldownRemaining = InitialAttackDelay;

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
	TSubclassOf<UHellunaEnemyGameplayAbility> GAClass = AttackAbilityClass.Get()
		? AttackAbilityClass
		: TSubclassOf<UHellunaEnemyGameplayAbility>(UHellunaEnemyGameplayAbility::StaticClass());

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

	// ② GA 종료 후 쿨다운 대기 중
	if (InstanceData.CooldownRemaining > 0.f)
	{
		InstanceData.CooldownRemaining -= DeltaTime;

		// SetFocus로 타겟 방향 바라보기 (bOrientRotationToMovement 건드리지 않음)
		if (TargetData.HasValidTarget())
			AIController->SetFocus(TargetData.TargetActor.Get());

		return EStateTreeRunStatus::Running;
	}

	// 쿨다운 끝 → 포커스 해제
	AIController->ClearFocus(EAIFocusPriority::Gameplay);

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

	// TryActivate 이전에 CurrentTarget 설정 (GA ActivateAbility 내부에서 즉시 참조)
	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (!Spec.Ability || Spec.Ability->GetClass() != GAClass) continue;

		if (UEnemyGameplayAbility_Attack* AttackGA = Cast<UEnemyGameplayAbility_Attack>(Spec.Ability))
			AttackGA->CurrentTarget = TargetData.TargetActor.Get();
		else if (UEnemyGameplayAbility_RangedAttack* RangedGA = Cast<UEnemyGameplayAbility_RangedAttack>(Spec.Ability))
			RangedGA->CurrentTarget = TargetData.TargetActor.Get();

		break;
	}

	const bool bActivated = ASC->TryActivateAbilityByClass(GAClass);
	if (bActivated)
	{
		const float CooldownMultiplier = Enemy->bEnraged ? Enemy->EnrageCooldownMultiplier : 1.f;
		InstanceData.CooldownRemaining = AttackCooldown * CooldownMultiplier;
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_AttackTarget::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (AAIController* AIC = InstanceData.AIController)
	{
		AIC->ClearFocus(EAIFocusPriority::Gameplay);

		if (APawn* Pawn = AIC->GetPawn())
		{
			if (UCharacterMovementComponent* MoveComp = Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent()))
			{
				// 원래 설정으로 복원
				MoveComp->bOrientRotationToMovement     = true;
				MoveComp->bUseControllerDesiredRotation = false;
			}
		}
	}
}
