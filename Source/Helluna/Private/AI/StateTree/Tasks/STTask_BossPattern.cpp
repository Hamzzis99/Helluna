/**
 * STTask_BossPattern.cpp
 *
 * 보스 패턴 실행기 Task.
 * Evaluator가 PendingPatternIndex를 기록하면 해당 GA를 발동한다.
 *
 * @author 김민우
 */

#include "AI/StateTree/Tasks/STTask_BossPattern.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Character/HellunaEnemyCharacter.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"

// ============================================================================
// EnterState
// ============================================================================
EStateTreeRunStatus FSTTask_BossPattern::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.ActivePatternGA   = nullptr;
	Data.PatternEntries    = PatternEntries;

	FBossPatternData& TD = Data.BossPatternData;
	TD.bPatternActive      = false;
	TD.PendingPatternIndex = -1;

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// Tick
// ============================================================================
EStateTreeRunStatus FSTTask_BossPattern::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	FBossPatternData& TD = Data.BossPatternData;

	AAIController* AIC = Data.AIController;
	if (!AIC) return EStateTreeRunStatus::Failed;

	APawn* Pawn = AIC->GetPawn();
	if (!Pawn) return EStateTreeRunStatus::Failed;

	AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Pawn);
	if (!Enemy) return EStateTreeRunStatus::Failed;

	UHellunaAbilitySystemComponent* ASC = Cast<UHellunaAbilitySystemComponent>(
		Enemy->GetAbilitySystemComponent());
	if (!ASC) return EStateTreeRunStatus::Failed;

	// ── 패턴 GA 실행 중 → 완료 감시 ────────────────────────
	if (TD.bPatternActive && Data.ActivePatternGA.Get())
	{
		bool bStillActive = false;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (Spec.Ability && Spec.Ability->GetClass() == Data.ActivePatternGA && Spec.IsActive())
			{
				bStillActive = true;
				break;
			}
		}

		if (!bStillActive)
		{
			// 패턴 GA 종료
			TD.bPatternActive      = false;
			TD.PendingPatternIndex = -1;
			Data.ActivePatternGA   = nullptr;

			UE_LOG(LogTemp, Log, TEXT("[BossPattern] 패턴 GA 종료 → 일반 행동 복귀"));
		}

		return EStateTreeRunStatus::Running;
	}

	// ── 패턴 발동 요청 감지 ─────────────────────────────────
	const int32 PendingIdx = TD.PendingPatternIndex;
	if (PendingIdx < 0 || PendingIdx >= Data.PatternEntries.Num())
		return EStateTreeRunStatus::Running;

	const FBossPatternEntry& Entry = Data.PatternEntries[PendingIdx];
	TSubclassOf<UHellunaEnemyGameplayAbility> GAClass = Entry.PatternAbilityClass;
	if (!GAClass.Get())
	{
		TD.PendingPatternIndex = -1;
		return EStateTreeRunStatus::Running;
	}

	// GA 부여
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

	// 이동 정지 + 패턴 발동
	AIC->StopMovement();

	const bool bActivated = ASC->TryActivateAbilityByClass(GAClass);
	if (bActivated)
	{
		TD.bPatternActive    = true;
		Data.ActivePatternGA = GAClass;

		UE_LOG(LogTemp, Log, TEXT("[BossPattern] 패턴 %d 발동: %s (TriggerType=%d)"),
			PendingIdx, *GetNameSafe(GAClass.Get()), (int32)Entry.TriggerType);
	}
	else
	{
		// 발동 실패 → 인덱스 초기화
		TD.PendingPatternIndex = -1;
		UE_LOG(LogTemp, Warning, TEXT("[BossPattern] 패턴 %d 발동 실패: %s"),
			PendingIdx, *GetNameSafe(GAClass.Get()));
	}

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// ExitState
// ============================================================================
void FSTTask_BossPattern::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.BossPatternData.bPatternActive      = false;
	Data.BossPatternData.PendingPatternIndex = -1;
	Data.ActivePatternGA = nullptr;
}
