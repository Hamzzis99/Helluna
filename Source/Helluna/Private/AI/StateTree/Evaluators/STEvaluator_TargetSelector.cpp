/**
 * STEvaluator_TargetSelector.cpp
 *
 * 플레이어 탐지 및 광폭화 이벤트 발송 전담.
 * 우주선 타겟은 STEvaluator_SpaceShip이 별도로 관리한다.
 *
 * @author 김민우
 */

#include "AI/StateTree/Evaluators/STEvaluator_TargetSelector.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Components/StateTreeComponent.h"
#include "HellunaGameplayTags.h"

// ============================================================================
// TreeStart - ?? ?? ?? ??(???)?? ???
// ============================================================================
void FSTEvaluator_TargetSelector::TreeStart(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FHellunaAITargetData& TargetData = InstanceData.TargetData;

	TargetData.TargetActor         = nullptr;
	TargetData.TargetType          = EHellunaTargetType::SpaceShip;
	TargetData.bTargetingPlayer    = false;
	TargetData.bPlayerLocked       = false;
	TargetData.bEnraged            = false;
	TargetData.PlayerTargetingTime = 0.f;
	TargetData.DistanceToTarget    = 0.f;

	const UWorld* World = Context.GetWorld();
	if (!World) return;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(FName("SpaceShip")))
		{
			TargetData.TargetActor = *It;
			break;
		}
	}
}

void FSTEvaluator_TargetSelector::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
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

	// ── 플레이어 락온 중: 타겟 변경 차단 ──────────────────────────────
	if (TargetData.bPlayerLocked)
	{
		if (TargetData.TargetActor.IsValid())
		{
			TargetData.DistanceToTarget    = FVector::Dist(PawnLocation, TargetData.TargetActor->GetActorLocation());
			TargetData.PlayerTargetingTime += DeltaTime;
		}
		else
		{
			// 락온 대상 소멸 → 플래그 리셋
			TargetData.bPlayerLocked       = false;
			TargetData.bEnraged            = false;
			TargetData.bTargetingPlayer    = false;
			TargetData.TargetActor         = nullptr;
			TargetData.PlayerTargetingTime = 0.f;
		}
		return;
	}

	// ── 플레이어 추적 중 ────────────────────────────────────────────
	if (TargetData.bTargetingPlayer && TargetData.TargetActor.IsValid())
	{
		TargetData.DistanceToTarget    = FVector::Dist(PawnLocation, TargetData.TargetActor->GetActorLocation());
		TargetData.PlayerTargetingTime += DeltaTime;

		// 광폭화 이벤트 발송
		if (!TargetData.bEnraged && TargetData.PlayerTargetingTime >= EnrageDelay)
		{
			if (UStateTreeComponent* STComp = const_cast<AAIController*>(AIController)
				->FindComponentByClass<UStateTreeComponent>())
			{
				FStateTreeEvent EnrageEvent;
				EnrageEvent.Tag = HellunaGameplayTags::Enemy_Event_Enrage;
				STComp->SendStateTreeEvent(EnrageEvent);
			}
		}
		return;
	}

	// ── AggroRange 내 플레이어 탐색 ─────────────────────────────────
	AActor* NearestPlayer = nullptr;
	float   NearestDistSq = AggroRange * AggroRange;

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

	if (NearestPlayer)
	{
		TargetData.TargetActor         = NearestPlayer;
		TargetData.TargetType          = EHellunaTargetType::Player;
		TargetData.DistanceToTarget    = FMath::Sqrt(NearestDistSq);
		TargetData.PlayerTargetingTime = 0.f;
		TargetData.bTargetingPlayer    = true;

		const_cast<AAIController*>(AIController)->SetFocus(NearestPlayer);
	}
}
