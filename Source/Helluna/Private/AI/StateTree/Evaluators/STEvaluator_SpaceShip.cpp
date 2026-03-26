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
#include "AI/SpaceShipAttackSlotManager.h"
#include "Components/PrimitiveComponent.h"

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
	int32 SearchCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		SearchCount++;
		if (It->ActorHasTag(SpaceShipTag))
		{
			SpaceShipData.TargetActor = *It;
			SpaceShipData.TargetType  = EHellunaTargetType::SpaceShip;
			UE_LOG(LogTemp, Warning, TEXT("[SpaceShipEval] ✅ 우주선 발견! Actor=%s (태그=%s, 탐색수=%d)"),
				*GetNameSafe(*It), *SpaceShipTag.ToString(), SearchCount);
			break;
		}
	}

	if (!SpaceShipData.TargetActor.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[SpaceShipEval] ❌ 우주선을 찾지 못함! 태그='%s' 로 탐색했으나 없음. 월드 내 Actor 총 %d개"),
			*SpaceShipTag.ToString(), SearchCount);
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

	// 거리 갱신 — 메시 표면 거리 (DynamicMeshComponent ComplexAsSimple 활용)
	const AAIController* AIController = InstanceData.AIController;
	if (!AIController) return;

	const APawn* ControlledPawn = AIController->GetPawn();
	if (!ControlledPawn) return;

	const FVector PawnLocation = ControlledPawn->GetActorLocation();
	AActor* ShipActor = SpaceShipData.TargetActor.Get();

	float ComputedDist = -1.f;

	// 1순위: 루트 DynamicMeshComponent 표면 거리
	// ComplexAsSimple + QueryAndPhysics 설정이므로 메시 표면 형상 반영
	if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(ShipActor->GetRootComponent()))
	{
		if (RootPrim->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			FVector ClosestPoint;
			ComputedDist = RootPrim->GetClosestPointOnCollision(PawnLocation, ClosestPoint);
		}
	}

	// 2순위: ShipCombatCollision 태그 컴포넌트 (레거시 호환)
	if (ComputedDist < 0.f)
	{
		TArray<UPrimitiveComponent*> Prims;
		ShipActor->GetComponents<UPrimitiveComponent>(Prims);

		float MinDist = MAX_FLT;
		bool bFound = false;

		for (UPrimitiveComponent* Prim : Prims)
		{
			if (!Prim || !Prim->ComponentHasTag(TEXT("ShipCombatCollision")))
				continue;

			FVector Closest;
			const float D = Prim->GetClosestPointOnCollision(PawnLocation, Closest);
			if (D >= 0.f && D < MinDist)
			{
				MinDist = D;
				bFound = true;
			}
		}

		if (bFound)
			ComputedDist = MinDist;
	}

	// 3순위: 중심점 거리 폴백
	SpaceShipData.DistanceToTarget = (ComputedDist >= 0.f)
		? ComputedDist
		: FVector::Dist(PawnLocation, ShipActor->GetActorLocation());
}
