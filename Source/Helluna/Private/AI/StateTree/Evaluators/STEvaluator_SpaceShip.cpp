/**
 * STEvaluator_SpaceShip.cpp
 *
 * @author 김민우
 */

#include "AI/StateTree/Evaluators/STEvaluator_SpaceShip.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

// ============================================================================
// TreeStart — 우주선을 한 번 탐색해서 캐싱
// ============================================================================
void FSTEvaluator_SpaceShip::TreeStart(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FHellunaSpaceShipTargetData& SpaceShipData = InstanceData.SpaceShipData;

	const UWorld* World = Context.GetWorld();
	if (!World) return;

	// 우주선은 게임 중 교체되지 않으므로 TreeStart에서 한 번만 탐색
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(SpaceShipTag))
		{
			SpaceShipData.TargetActor = *It;
			break;
		}
	}
}

// ============================================================================
// Tick — 우주선까지 거리 갱신
// ============================================================================
void FSTEvaluator_SpaceShip::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FHellunaSpaceShipTargetData& SpaceShipData = InstanceData.SpaceShipData;

	// 우주선이 없으면 재탐색 시도
	if (!SpaceShipData.TargetActor.IsValid())
	{
		const UWorld* World = Context.GetWorld();
		if (!World) return;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(SpaceShipTag))
			{
				SpaceShipData.TargetActor = *It;
				break;
			}
		}
		return;
	}

	const AAIController* AIController = InstanceData.AIController;
	if (!AIController) return;

	const APawn* ControlledPawn = AIController->GetPawn();
	if (!ControlledPawn) return;

	SpaceShipData.DistanceToTarget = FVector::Dist(
		ControlledPawn->GetActorLocation(),
		SpaceShipData.TargetActor->GetActorLocation()
	);
}
