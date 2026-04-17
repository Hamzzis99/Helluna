/**
 * STTask_Enrage.cpp
 *
 * 광폭화 진입 Task.
 * 광폭화 GA 활성화 후 타겟을 우주선으로 고정한다.
 * 이후 EnrageLoop State에서 우주선을 향해 빠르게 돌진+공격.
 *
 * @author 김민우
 */

#include "AI/StateTree/Tasks/STTask_Enrage.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Character/HellunaEnemyCharacter.h"
#include "EngineUtils.h"
#include "Helluna.h"

// ============================================================================
// EnterState — 광폭화 진입: GA 활성화 + 우주선으로 타겟 전환
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
#if HELLUNA_DEBUG_ENEMY
		UE_LOG(LogTemp, Warning, TEXT("[STTask_Enrage] AIController is null"));
#endif
		return EStateTreeRunStatus::Failed;
	}

	APawn* Pawn = AIController->GetPawn();
	if (!Pawn)
	{
#if HELLUNA_DEBUG_ENEMY
		UE_LOG(LogTemp, Warning, TEXT("[STTask_Enrage] Pawn is null"));
#endif
		return EStateTreeRunStatus::Failed;
	}

	AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Pawn);
	if (!Enemy)
	{
#if HELLUNA_DEBUG_ENEMY
		UE_LOG(LogTemp, Warning, TEXT("[STTask_Enrage] Pawn is not AHellunaEnemyCharacter"));
#endif
		return EStateTreeRunStatus::Failed;
	}

	FHellunaAITargetData& TargetData = InstanceData.TargetData;

	// 광폭화 플래그 설정 — Evaluator가 더 이상 타겟을 변경하지 않음
	TargetData.bEnraged      = true;
	TargetData.bPlayerLocked = true;

	// 타겟을 우주선으로 전환 (기존 플레이어/터렛 → 우주선)
	TargetData.bTargetingPlayer    = false;
	TargetData.PlayerTargetingTime = 0.f;
	TargetData.TargetType          = EHellunaTargetType::SpaceShip;

	// 우주선 Actor 찾기
	const UWorld* World = Context.GetWorld();
	if (World)
	{
		static TWeakObjectPtr<AActor> CachedSpaceShip;
		if (!CachedSpaceShip.IsValid())
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->ActorHasTag(FName("SpaceShip")))
				{
					CachedSpaceShip = *It;
					break;
				}
			}
		}
		if (CachedSpaceShip.IsValid())
		{
			TargetData.TargetActor = CachedSpaceShip.Get();
		}
	}

	// 포커스 해제 (우주선 방향으로 전환)
	AIController->ClearFocus(EAIFocusPriority::Gameplay);

	// 광폭화 적용 (서버에서만 실제 적용: 이동속도 증가 + 몽타주 + VFX)
	Enemy->EnterEnraged();
	InstanceData.bEnrageApplied = true;

	// 몽타주 완료 시 bMontageFinished 세팅 → Tick에서 Succeeded 반환
	FInstanceDataType* InstanceDataPtr = &InstanceData;
	Enemy->OnEnrageMontageFinished.BindLambda([InstanceDataPtr]()
	{
		InstanceDataPtr->bMontageFinished = true;
	});

#if HELLUNA_DEBUG_ENEMY
	UE_LOG(LogTemp, Log, TEXT("[STTask_Enrage] %s 광폭화 시작 → 우주선으로 타겟 전환"), *Enemy->GetName());
#endif

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

	// 광폭화 몽타주 완료 → EnrageLoop State로 전환
	if (InstanceData.bMontageFinished)
	{
#if HELLUNA_DEBUG_ENEMY
		UE_LOG(LogTemp, Log, TEXT("[STTask_Enrage] 몽타주 완료 → EnrageLoop 전환"));
#endif
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// ExitState — 델리게이트 정리 (광폭화 플래그는 유지 — 영구 광폭화)
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

	// 광폭화 플래그는 해제하지 않음 — 영구 광폭화
	// bEnraged = true, bPlayerLocked = true 유지
	// EnrageLoop State에서 SpaceShip Evaluator의 데이터를 바인딩하므로
	// 우주선을 향한 돌진+공격이 자동으로 이어진다.
}
