/**
 * STTask_Enrage.cpp
 *
 * @author 김민우
 */

// File: Source/Helluna/Private/AI/StateTree/Tasks/STTask_Enrage.cpp
// Build Target: Helluna (Server + Client)

#include "AI/StateTree/Tasks/STTask_Enrage.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Character/HellunaEnemyCharacter.h"
#include "EngineUtils.h"

// ============================================================================
// EnterState
// ============================================================================
EStateTreeRunStatus FSTTask_Enrage::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	InstanceData.bEnrageApplied   = false;
	InstanceData.bMontageFinished = false;

	AAIController* AIController = InstanceData.AIController;
	if (!AIController)
	{
		UE_LOG(LogTemp, Warning, TEXT("[STTask_Enrage] AIController is null"));
		return EStateTreeRunStatus::Failed;
	}

	APawn* Pawn = AIController->GetPawn();
	if (!Pawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("[STTask_Enrage] Pawn is null"));
		return EStateTreeRunStatus::Failed;
	}

	AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Pawn);
	if (!Enemy)
	{
		UE_LOG(LogTemp, Warning, TEXT("[STTask_Enrage] Pawn is not AHellunaEnemyCharacter"));
		return EStateTreeRunStatus::Failed;
	}

	FHellunaAITargetData& TargetData = InstanceData.TargetData;

	// 현재 타겟(플레이어)이 없으면 광폭화 의미 없음 → Failed
	if (!TargetData.TargetActor.IsValid() ||
		TargetData.TargetType != EHellunaTargetType::Player)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[STTask_Enrage] 플레이어 타겟 없음 - 광폭화 진입 불가 (%s)"), *Enemy->GetName());
		return EStateTreeRunStatus::Failed;
	}

	// 플레이어 락온: Evaluator가 타겟을 바꾸지 않도록 잠금
	TargetData.bPlayerLocked = true;
	TargetData.bEnraged      = true;

	// 광폭화 적용 (HasAuthority 내부 검사 → 서버에서만 실제 적용)
	Enemy->EnterEnraged();
	InstanceData.bEnrageApplied = true;

	// 몽타주 완료 시 bMontageFinished 세팅 → Tick에서 Succeeded 반환
	// 람다는 InstanceData 참조를 직접 캡처할 수 없으므로 포인터를 우회
	// StateTree InstanceData는 Task 생존 기간 동안 안전하게 유지됨
	FInstanceDataType* InstanceDataPtr = &InstanceData;
	Enemy->OnEnrageMontageFinished.BindLambda([InstanceDataPtr]()
	{
		InstanceDataPtr->bMontageFinished = true;
	});

	UE_LOG(LogTemp, Log, TEXT("[STTask_Enrage] %s 광폭화 시작"), *Enemy->GetName());

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// Tick — 몽타주 완료 또는 타겟 소멸 감지
// ============================================================================
EStateTreeRunStatus FSTTask_Enrage::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const FHellunaAITargetData& TargetData = InstanceData.TargetData;

	// 락온 대상 소멸 → 즉시 종료
	if (!TargetData.TargetActor.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("[STTask_Enrage] 락온 대상 소멸 → 종료"));
		return EStateTreeRunStatus::Succeeded;
	}

	// 광폭화 몽타주 완료 → EnrageLoop State로 전환
	if (InstanceData.bMontageFinished)
	{
		UE_LOG(LogTemp, Log, TEXT("[STTask_Enrage] 몽타주 완료 → EnrageLoop 전환"));
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// ExitState — 플래그 정리 + 델리게이트 언바인딩
// ============================================================================
void FSTTask_Enrage::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.bEnrageApplied) return;

	// 델리게이트 언바인딩 (InstanceData 포인터가 무효화되기 전에 해제)
	AAIController* AIController = InstanceData.AIController;
	if (AIController)
	{
		if (APawn* Pawn = AIController->GetPawn())
		{
			if (AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Pawn))
			{
				Enemy->OnEnrageMontageFinished.Unbind();
			}
		}
	}

	FHellunaAITargetData& TargetData = InstanceData.TargetData;

	// 락온/광폭화 플래그 해제 (광폭화 스탯은 영구 유지)
	TargetData.bPlayerLocked       = false;
	TargetData.bEnraged            = false;
	TargetData.bTargetingPlayer    = false;
	TargetData.TargetActor         = nullptr;
	TargetData.PlayerTargetingTime = 0.f;
}
