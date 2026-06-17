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
#include "AI/HellunaAIAttackZone.h" // [SurfaceDistanceV1] 공용 표면거리 헬퍼

// ============================================================================
// TreeStart — 우주선을 첫 틱 전에 탐색해서 캐싱
// ============================================================================
void FSTEvaluator_SpaceShip::TreeStart(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FHellunaAITargetData& SpaceShipData = InstanceData.SpaceShipData;

	const UWorld* World = Context.GetWorld();
	if (!World) return;

	// #4 최적화: 50마리 동시 스폰 시 TActorIterator 반복 방지 — 정적 캐시 사용
	static TWeakObjectPtr<AActor> CachedSpaceShip;
	static int32 CacheHitCount = 0;
	static int32 CacheMissCount = 0;

	if (!CachedSpaceShip.IsValid())
	{
		CacheMissCount++;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(SpaceShipTag))
			{
				CachedSpaceShip = *It;
				break;
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[fast][SpaceShipEval] TreeStart 캐시 MISS (누적: Hit=%d Miss=%d)"),
			CacheHitCount, CacheMissCount);
	}
	else
	{
		CacheHitCount++;
		UE_LOG(LogTemp, Verbose, TEXT("[fast][SpaceShipEval] TreeStart 캐시 HIT (누적: Hit=%d Miss=%d)"),
			CacheHitCount, CacheMissCount);
	}

	if (CachedSpaceShip.IsValid())
	{
		SpaceShipData.TargetActor = CachedSpaceShip.Get();
		SpaceShipData.TargetType  = EHellunaTargetType::SpaceShip;
		UE_LOG(LogTemp, Verbose, TEXT("[SpaceShipEval] 우주선 발견 (캐시): Actor=%s"),
			*GetNameSafe(CachedSpaceShip.Get()));

		// #9 최적화: ShipCombatCollision 컴포넌트 TreeStart에서 캐싱
		InstanceData.CachedShipCollisionPrims.Reset();
		TArray<UPrimitiveComponent*> Prims;
		CachedSpaceShip->GetComponents<UPrimitiveComponent>(Prims);
		for (UPrimitiveComponent* Prim : Prims)
		{
			if (Prim && Prim->ComponentHasTag(TEXT("ShipCombatCollision")))
				InstanceData.CachedShipCollisionPrims.Add(Prim);
		}
		InstanceData.bShipCollisionPrimsCached = true;
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("[SpaceShipEval] 우주선 미발견: 태그='%s' (보스 테스트맵에서는 정상)"),
			*SpaceShipTag.ToString());
	}
}

// ============================================================================
// Tick — 우주선까지 거리 갱신 + 소멸 시 재탐색
// ============================================================================
void FSTEvaluator_SpaceShip::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FHellunaAITargetData& SpaceShipData = InstanceData.SpaceShipData;

	// 우주선이 소멸됐으면 재탐색 시도 (매 프레임이 아닌 2초 간격)
	// #4 최적화: 정적 캐시 사용 — 50마리 동시 소실 시에도 프레임당 1회만 탐색
	if (!SpaceShipData.TargetActor.IsValid())
	{
		InstanceData.RespawnSearchTimer += DeltaTime;
		if (InstanceData.RespawnSearchTimer < 2.f)
			return;

		InstanceData.RespawnSearchTimer = 0.f;

		static TWeakObjectPtr<AActor> CachedSpaceShip;
		static uint64 LastSearchFrame = 0;

		if (CachedSpaceShip.IsValid())
		{
			SpaceShipData.TargetActor = CachedSpaceShip.Get();
			SpaceShipData.TargetType  = EHellunaTargetType::SpaceShip;
		}
		else if (GFrameCounter != LastSearchFrame)
		{
			LastSearchFrame = GFrameCounter;
			const UWorld* World = Context.GetWorld();
			if (!World) return;

			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->ActorHasTag(SpaceShipTag))
				{
					CachedSpaceShip = *It;
					SpaceShipData.TargetActor = *It;
					SpaceShipData.TargetType  = EHellunaTargetType::SpaceShip;
					break;
				}
			}
		}

		// 재탐색 성공 시 ShipCombatCollision 캐시 갱신
		if (SpaceShipData.TargetActor.IsValid())
		{
			InstanceData.CachedShipCollisionPrims.Reset();
			TArray<UPrimitiveComponent*> Prims;
			SpaceShipData.TargetActor->GetComponents<UPrimitiveComponent>(Prims);
			for (UPrimitiveComponent* Prim : Prims)
			{
				if (Prim && Prim->ComponentHasTag(TEXT("ShipCombatCollision")))
					InstanceData.CachedShipCollisionPrims.Add(Prim);
			}
			InstanceData.bShipCollisionPrimsCached = true;
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

	// [SurfaceDistanceV1] 우주선 표면까지 거리. 공용 헬퍼가 복합 콜리전(메시) 일 때 OBB 폴백을
	//   적용한다. 기존 GetClosestPointOnCollision 단독 방식은 우주선 메시에서 -1 을 반환해
	//   매번 원점 거리로 폴백되던 버그가 있었다(2026-06-11 확인).
	SpaceShipData.DistanceToTarget = HellunaAI::GetTargetSurfaceDistance(PawnLocation, ShipActor);
}
