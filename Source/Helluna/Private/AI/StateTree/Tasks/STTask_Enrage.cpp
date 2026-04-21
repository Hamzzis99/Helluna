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
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Character/HellunaEnemyCharacter.h"
#include "EngineUtils.h"
#include "Helluna.h"

namespace
{
	// [EnrageRecoveryV1] 피격으로 EnrageMontage 가 끊기고 OnMontageEnded 콜백이
	// 유실되는 드문 케이스에 대비한 타임아웃. 몽타주 길이(~1.5~2s)보다 넉넉하게.
	constexpr float GEnrageMontageMaxWait = 5.f;
}

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
	InstanceData.ElapsedTime      = 0.f;

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

	// [EnrageRecoveryV1] 재진입 케이스 — 이미 광폭화가 적용된 적이라면
	// EnterEnraged()가 early-return 하므로 몽타주도 콜백도 재실행되지 않는다.
	// 여기서 바로 Succeeded 반환해서 EnrageLoop 로 넘겨야 Task 가 영구 Running
	// 상태로 고착되지 않는다. (피격 직후 상태 고착 버그 재현 경로 중 하나)
	if (Enemy->bEnraged)
	{
		InstanceData.bEnrageApplied   = true;
		InstanceData.bMontageFinished = true;
		UE_LOG(LogTemp, Warning,
			TEXT("[EnrageRecoveryV1] %s 재진입: 이미 bEnraged → 즉시 EnrageLoop 전환"),
			*Enemy->GetName());
		return EStateTreeRunStatus::Running;
	}

	// [EnrageRecoveryV1] 람다 바인딩은 EnterEnraged 호출 전에 완료해야 한다.
	// EnrageMontage 가 null인 적이면 EnterEnraged 내부에서 OnEnrageMontageFinished 가
	// 동기적으로 ExecuteIfBound 된다 — 바인딩이 늦으면 콜백을 놓쳐 Task 가 고착된다.
	FInstanceDataType* InstanceDataPtr = &InstanceData;
	Enemy->OnEnrageMontageFinished.BindLambda([InstanceDataPtr]()
	{
		InstanceDataPtr->bMontageFinished = true;
	});

	// 광폭화 적용 (서버에서만 실제 적용: 이동속도 증가 + 몽타주 + VFX)
	Enemy->EnterEnraged();
	InstanceData.bEnrageApplied = true;

	UE_LOG(LogTemp, Warning,
		TEXT("[EnrageRecoveryV1] %s 광폭화 시작 → 우주선으로 타겟 전환 (Montage=%s)"),
		*Enemy->GetName(),
		Enemy->EnrageMontage ? *Enemy->EnrageMontage->GetName() : TEXT("none"));

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
		UE_LOG(LogTemp, Warning, TEXT("[EnrageRecoveryV1] 몽타주 완료/Interrupt → EnrageLoop 전환"));
		return EStateTreeRunStatus::Succeeded;
	}

	// [EnrageRecoveryV1] 안전장치: 피격(HitReact)이 EnrageMontage 를 중단시켰음에도
	// OnMontageEnded 브로드캐스트 순서/슬롯 이슈로 콜백이 유실되는 경우를 대비.
	// 1) 실제로 EnrageMontage 가 더 이상 재생 중이 아니면 종료로 간주.
	// 2) GEnrageMontageMaxWait 를 초과하면 강제 전환 (최종 안전망).
	InstanceData.ElapsedTime += DeltaTime;

	AAIController* AIController = InstanceData.AIController;
	APawn* Pawn = AIController ? AIController->GetPawn() : nullptr;
	AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Pawn);
	if (Enemy)
	{
		if (USkeletalMeshComponent* Mesh = Enemy->GetMesh())
		{
			if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
			{
				// 몽타주가 설정되어 있고 재생 중이 아니면 이미 끝났거나 교체된 것
				// (진입 직후 한 틱은 아직 재생 시작 전일 수 있으므로 ElapsedTime>0.2s 가드)
				if (Enemy->EnrageMontage &&
					InstanceData.ElapsedTime > 0.2f &&
					!AnimInst->Montage_IsPlaying(Enemy->EnrageMontage))
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[EnrageRecoveryV1] %s EnrageMontage 재생 아님 (콜백 유실 추정) → 강제 전환"),
						*Enemy->GetName());
					return EStateTreeRunStatus::Succeeded;
				}
			}
		}
	}

	if (InstanceData.ElapsedTime >= GEnrageMontageMaxWait)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[EnrageRecoveryV1] 타임아웃 %.1fs 도달 → 강제 EnrageLoop 전환"),
			GEnrageMontageMaxWait);
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
