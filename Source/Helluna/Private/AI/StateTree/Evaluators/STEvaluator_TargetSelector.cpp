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
#include "Components/PrimitiveComponent.h"
#include "Components/StateTreeComponent.h"
#include "HellunaGameplayTags.h"

// ============================================================================
// TreeStart - initialize the default spaceship target if one already exists.
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

	// #3 최적화: 50마리 동시 스폰 시 TActorIterator 반복 방지 — 정적 캐시 사용
	static TWeakObjectPtr<AActor> CachedSpaceShip;
	static int32 CacheHitCount = 0;
	static int32 CacheMissCount = 0;

	if (!CachedSpaceShip.IsValid())
	{
		CacheMissCount++;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(FName("SpaceShip")))
			{
				CachedSpaceShip = *It;
				break;
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[fast][TargetSelector] 캐시 MISS — TActorIterator 실행 (누적: Hit=%d Miss=%d)"),
			CacheHitCount, CacheMissCount);
	}
	else
	{
		CacheHitCount++;
	}

	if (CachedSpaceShip.IsValid())
	{
		TargetData.TargetActor = CachedSpaceShip.Get();
		TargetData.TargetType  = EHellunaTargetType::SpaceShip;
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

	// ── 광폭화 완료: 타겟 선택 전체 차단 ────────────────────────────
	if (TargetData.bEnraged)
		return;

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

				// 광폭화 확정 - 이후 타겟 변경 완전 차단
				TargetData.bEnraged      = true;
				TargetData.bPlayerLocked = true;
			}
		}

		// 광폭화 후에는 타겟 선택 로직 전체를 건너뜀
		if (TargetData.bEnraged)
			return;

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
	else
	{
		// 플레이어 없음 → 우주선 타겟으로 복귀 + 거리 갱신
		if (!TargetData.bTargetingPlayer)
		{
			TargetData.TargetType = EHellunaTargetType::SpaceShip;
		}

		// 우주선 거리 갱신 (표면 거리로 계산)
		// #9 관련 최적화: GetComponents 매 틱 호출 대신 정적 캐시 사용
		if (TargetData.TargetActor.IsValid())
		{
			static TWeakObjectPtr<AActor> CachedShipForDist;
			static TArray<TWeakObjectPtr<UPrimitiveComponent>> CachedCollisionPrims;

			// 우주선이 바뀌었거나 캐시가 비어있으면 재수집
			if (CachedShipForDist.Get() != TargetData.TargetActor.Get() || CachedCollisionPrims.IsEmpty())
			{
				CachedShipForDist = TargetData.TargetActor;
				CachedCollisionPrims.Reset();
				TArray<UPrimitiveComponent*> Prims;
				TargetData.TargetActor->GetComponents<UPrimitiveComponent>(Prims);
				for (UPrimitiveComponent* Prim : Prims)
				{
					if (Prim && Prim->ComponentHasTag(TEXT("ShipCombatCollision")))
						CachedCollisionPrims.Add(Prim);
				}
			}

			float MinDist = MAX_FLT;
			bool bFound = false;
			for (const TWeakObjectPtr<UPrimitiveComponent>& WeakPrim : CachedCollisionPrims)
			{
				UPrimitiveComponent* Prim = WeakPrim.Get();
				if (!Prim) continue;
				const float D = FVector::Dist(PawnLocation, Prim->GetComponentLocation());
				if (D < MinDist) { MinDist = D; bFound = true; }
			}
			TargetData.DistanceToTarget = bFound
				? MinDist
				: FVector::Dist(PawnLocation, TargetData.TargetActor->GetActorLocation());
		}
	}
}
