/**
 * STEvaluator_SpaceShip.cpp
 *
 * 우주선 Actor를 탐색하고 거리를 매 틱 갱신한다.
 * EnrageLoop State의 Task들이 이 데이터를 바인딩해서
 * 광폭화 후 우주선을 향해 돌진하도록 유도한다.
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
// TreeStart — 우주선을 첫 틱 전에 탐색해서 캐싱
// ============================================================================
void FSTEvaluator_SpaceShip::TreeStart(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FHellunaAITargetData& SpaceShipData = InstanceData.SpaceShipData;

	const UWorld* World = Context.GetWorld();
	if (!World) return;

	// 우주선은 게임 내 고정 오브젝트이므로 TreeStart에서 한 번만 탐색
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(SpaceShipTag))
		{
			SpaceShipData.TargetActor = *It;
			SpaceShipData.TargetType  = EHellunaTargetType::SpaceShip;
			break;
		}
	}
}

// ============================================================================
// Tick — 우주선까지 거리 갱신 + 소멸 시 재탐색
// ============================================================================
void FSTEvaluator_SpaceShip::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FHellunaAITargetData& SpaceShipData = InstanceData.SpaceShipData;

	// 우주선이 소멸됐으면 재탐색 시도
	if (!SpaceShipData.TargetActor.IsValid())
	{
		const UWorld* World = Context.GetWorld();
		if (!World) return;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(SpaceShipTag))
			{
				SpaceShipData.TargetActor = *It;
				SpaceShipData.TargetType  = EHellunaTargetType::SpaceShip;
				break;
			}
		}
		return;
	}

	// 거리 갱신
	const AAIController* AIController = InstanceData.AIController;
	if (!AIController) return;

	const APawn* ControlledPawn = AIController->GetPawn();
	if (!ControlledPawn) return;

	SpaceShipData.DistanceToTarget = FVector::Dist(
		ControlledPawn->GetActorLocation(),
		SpaceShipData.TargetActor->GetActorLocation()
	);
}
