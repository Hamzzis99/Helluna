/**
 * STEvaluator_PlayerHunter.cpp
 *
 * 헌터 전용 타겟 평가자 — 오직 최근접 플레이어만 추적.
 *
 * 서버 권한에서만 AI 가 돌므로 GetPlayerControllerIterator 로 모든 플레이어를
 * 훑어 가장 가까운 Pawn 을 TargetData 에 기록한다. 우주선/터렛/광폭화 일절 없음.
 *
 * @author 김민우
 */

#include "AI/StateTree/Evaluators/STEvaluator_PlayerHunter.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

// ============================================================================
// TreeStart — 타겟 초기화 (플레이어 타입, 아직 미지정)
// ============================================================================
void FSTEvaluator_PlayerHunter::TreeStart(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FHellunaAITargetData& TargetData = InstanceData.TargetData;

	TargetData.TargetActor         = nullptr;
	TargetData.TargetType          = EHellunaTargetType::Player;
	TargetData.bTargetingPlayer    = false;
	TargetData.bPlayerLocked       = false;
	TargetData.bEnraged            = false;
	TargetData.bAttackingSpaceShip = false;
	TargetData.PlayerTargetingTime = 0.f;
	TargetData.DistanceToTarget    = 0.f;
}

// ============================================================================
// Tick — 최근접 플레이어 추적
// ============================================================================
void FSTEvaluator_PlayerHunter::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FHellunaAITargetData& TargetData = InstanceData.TargetData;

	const AAIController* AIController = InstanceData.AIController;
	if (!AIController) return;

	const APawn* ControlledPawn = AIController->GetPawn();
	if (!ControlledPawn) return;

	const UWorld* World = Context.GetWorld();
	if (!World) return;

	const FVector PawnLocation = ControlledPawn->GetActorLocation();

	// 모든 플레이어 중 최근접 Pawn 탐색 (거리 무관 — 헌터는 항상 추격).
	AActor* NearestPlayer = nullptr;
	float NearestDistSq = MAX_FLT;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC) continue;

		APawn* PlayerPawn = PC->GetPawn();
		if (!PlayerPawn) continue;

		const float DistSq = FVector::DistSquared(PawnLocation, PlayerPawn->GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			NearestPlayer = PlayerPawn;
		}
	}

	// 헌터는 우주선/터렛/광폭화 개념이 없다 — 항상 플레이어 전용.
	TargetData.bAttackingSpaceShip = false;
	TargetData.bEnraged            = false;
	TargetData.bPlayerLocked       = false;

	if (NearestPlayer)
	{
		// [AttackGraceAfterSwitchV1 호환] STCondition_InAttackZone 은 플레이어 공격 진입에
		//   PlayerTargetingTime >= 0.5 를 요구한다(전환 직후 즉시타격 방지용 grace). 헌터는 항상
		//   플레이어를 쫓으므로, 같은 플레이어를 계속 추격하는 동안 시간을 누적해야 공격이 발동된다.
		//   누적을 안 하면 영원히 0 → 공격존 조건이 절대 통과 못 해 "쫓기만 하고 공격 안 함".
		const bool bSameTarget = TargetData.bTargetingPlayer && (TargetData.TargetActor.Get() == NearestPlayer);

		TargetData.TargetActor      = NearestPlayer;
		TargetData.TargetType       = EHellunaTargetType::Player;
		TargetData.bTargetingPlayer = true;
		TargetData.DistanceToTarget = FMath::Sqrt(NearestDistSq);
		TargetData.PlayerTargetingTime = bSameTarget ? (TargetData.PlayerTargetingTime + DeltaTime) : 0.f;

		const_cast<AAIController*>(AIController)->SetFocus(NearestPlayer);
	}
	else
	{
		TargetData.TargetActor      = nullptr;
		TargetData.bTargetingPlayer = false;
		TargetData.PlayerTargetingTime = 0.f;
		const_cast<AAIController*>(AIController)->ClearFocus(EAIFocusPriority::Gameplay);
	}
}
