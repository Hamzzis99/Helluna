/**
 * STEvaluator_TargetSelector.cpp
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

	// 플레이어 추적 중 → LeashRange 확인
	if (InstanceData.bTargetingPlayer && TargetData.TargetActor.IsValid())
	{
		const float DistToPlayer = FVector::Dist(PawnLocation, TargetData.TargetActor->GetActorLocation());
		TargetData.DistanceToTarget = DistToPlayer;

		if (DistToPlayer <= LeashRange)
			return;

		InstanceData.bTargetingPlayer = false;
		TargetData.TargetActor = nullptr;
	}

	// AggroRange 내 플레이어 탐색
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
		TargetData.TargetActor        = NearestPlayer;
		TargetData.TargetType         = EHellunaTargetType::Player;
		TargetData.DistanceToTarget   = FMath::Sqrt(NearestDistSq);
		InstanceData.bTargetingPlayer = true;
		return;
	}

	// 기본 타겟: 우주선
	if (TargetData.TargetType == EHellunaTargetType::SpaceShip && TargetData.TargetActor.IsValid())
	{
		TargetData.DistanceToTarget = FVector::Dist(PawnLocation, TargetData.TargetActor->GetActorLocation());
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(SpaceShipTag))
		{
			TargetData.TargetActor      = *It;
			TargetData.TargetType       = EHellunaTargetType::SpaceShip;
			TargetData.DistanceToTarget = FVector::Dist(PawnLocation, It->GetActorLocation());
			break;
		}
	}
}
