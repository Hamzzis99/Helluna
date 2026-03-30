/**
 * STTask_ChaseTarget_Test.cpp
 *
 * 우주선/플레이어 통합 추적 Task (v2).
 *
 * ─── 우주선 이동 (지형에 묻힌 우주선 대응) ──────────────────
 *
 *  [Phase 1: 원거리] 중심거리 > SlotEngageRadius
 *    NavMesh 위 중간 경유점으로 우주선 방향 접근.
 *    NavMesh 경계 도달 시 pathfinding OFF 직접 이동.
 *
 *  [Phase 2: 근거리 슬롯] bUseSlotSystem && SlotManager 유효
 *    SlotManager에서 배정받은 NavMesh 검증 위치로 이동.
 *    도착 시 OccupySlot → Running 유지 (Attack State 전환 대기).
 *    Stuck/경로실패 시 슬롯 반납 후 재배정.
 *
 *  [Phase 2': 근거리 자유] 슬롯 OFF 또는 슬롯 없음
 *    NavMesh 경유점 접근 + NavMesh 경계 시 직접 이동.
 *
 * ─── 플레이어 이동 ───────────────────────────────────────────
 *  RepathInterval마다 MoveToActor / EQS 재발행.
 *  Stuck 시 랜덤 우회 후 재추적.
 *  DistanceToTarget <= PlayerAttackRange → Succeeded.
 *
 * @author 김민우
 */

#include "AI/StateTree/Tasks/STTask_ChaseTarget_Test.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"

#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "AI/SpaceShipAttackSlotManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogChaseTarget, Log, All);

namespace ChaseTargetTestHelpers {

FVector ComputeDetourGoal(
	const FVector& PawnLoc,
	const FVector& TargetLoc,
	AAIController* AIC,
	float OffsetAmount,
	int32 DirectionSign = 0);

int32 ResolveDetourDirectionSign(
	FSTTask_ChaseTarget_TestInstanceData& D,
	bool bUsePersistentDirection);

// 전방 선언: 우주선 밑/위인지 체크하는 공용 함수
static bool IsUnderOrOnShip(UWorld* World, AActor* Ship, const FVector& Location)
{
	if (!World || !Ship) return false;

	FCollisionQueryParams TraceParams;

	// 위로 Trace: 우주선 밑인지
	{
		const FVector UpStart = Location + FVector(0, 0, 10.f);
		const FVector UpEnd   = Location + FVector(0, 0, 1500.f);
		FHitResult UpHit;
		if (World->LineTraceSingleByChannel(UpHit, UpStart, UpEnd, ECC_Visibility, TraceParams))
		{
			if (UpHit.GetActor() == Ship) return true;
		}
	}

	// 아래로 Trace: 우주선 위인지
	{
		const FVector DownStart = Location + FVector(0, 0, 10.f);
		const FVector DownEnd   = Location - FVector(0, 0, 500.f);
		FHitResult DownHit;
		if (World->LineTraceSingleByChannel(DownHit, DownStart, DownEnd, ECC_Visibility, TraceParams))
		{
			if (DownHit.GetActor() == Ship) return true;
		}
	}

	return false;
}

// ============================================================================
// 헬퍼: 우주선 방향 NavMesh 위 중간 경유점 계산
//   우주선 밑/위로 투영되는 경유점은 거부하고, 대안 경유점을 시도한다.
// ============================================================================
FVector ComputeNavGoalTowardShip(
	const FVector& PawnLoc,
	const FVector& ShipLoc,
	AAIController* AIC,
	AActor* ShipActor = nullptr,
	float MaxStepDist = 1200.f)
{
	const float Dist = FVector::Dist(PawnLoc, ShipLoc);
	const FVector Dir = (ShipLoc - PawnLoc).GetSafeNormal();

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(AIC->GetWorld());
	if (!NavSys) return PawnLoc;

	// 여러 거리를 시도해서 유효한 경유점을 찾는다
	// (가까운 것부터: 80%, 60%, 40%, 20% 거리)
	const float Fractions[] = { 0.8f, 0.6f, 0.4f, 0.2f };

	for (float Frac : Fractions)
	{
		const float StepDist = FMath::Min(Dist * Frac, MaxStepDist);
		if (StepDist < 80.f) continue;  // 너무 가까우면 스킵

		const FVector RawGoal = PawnLoc + Dir * StepDist;

		FNavLocation NavGoal;
		if (!NavSys->ProjectPointToNavigation(RawGoal, NavGoal, FVector(200.f, 200.f, 500.f)))
			continue;

		// 현재 위치와 너무 가까우면 스킵
		if (FVector::Dist(PawnLoc, NavGoal.Location) < 80.f)
			continue;

		// 우주선 밑/위인지 체크
		if (ShipActor && IsUnderOrOnShip(AIC->GetWorld(), ShipActor, NavGoal.Location))
			continue;

		return NavGoal.Location;
	}

	// 모든 직선 경유점이 실패 → 현재 위치 반환 (정지)
	return PawnLoc;
}

// ============================================================================
// 헬퍼: 위치로 이동 (MoveTo 실패 시 중간 경유점 폴백)
// ============================================================================
void IssueMoveToLocation(AAIController* AIC, const FVector& Goal, float Radius)
{
	if (!IsValid(AIC)) return;

	FAIMoveRequest Req;
	Req.SetGoalLocation(Goal);
	Req.SetAcceptanceRadius(Radius);
	Req.SetUsePathfinding(true);
	Req.SetAllowPartialPath(true);
	FPathFollowingRequestResult Result = AIC->MoveTo(Req);

	if (Result.Code == EPathFollowingRequestResult::Failed)
	{
		APawn* Pawn = AIC->GetPawn();
		if (!IsValid(Pawn)) return;

		const FVector FallbackGoal = ComputeNavGoalTowardShip(
			Pawn->GetActorLocation(), Goal, AIC, nullptr);

		FAIMoveRequest FallbackReq;
		FallbackReq.SetGoalLocation(FallbackGoal);
		FallbackReq.SetAcceptanceRadius(Radius);
		FallbackReq.SetUsePathfinding(true);
		FallbackReq.SetAllowPartialPath(true);
		AIC->MoveTo(FallbackReq);
	}
}

// ============================================================================
// 헬퍼: 우주선 방향 이동
//   NavMesh 경계에 도달하면 무리하게 직진하지 않고 정지한다.
//   (pathfinding OFF 직진은 우주선 메시 아래로 파고드는 문제 유발)
// ============================================================================
void IssueMoveTowardShip(
	AAIController* AIC,
	APawn* Pawn,
	AActor* Ship,
	float AcceptanceRadius)
{
	const FVector PawnLoc = Pawn->GetActorLocation();
	const FVector ShipLoc = Ship->GetActorLocation();
	const FVector NavGoal = ComputeNavGoalTowardShip(PawnLoc, ShipLoc, AIC, Ship);

	// 경유점이 현재 위치와 같으면 (제자리 반환) → 이동 불가, 정지
	if (FVector::Dist(PawnLoc, NavGoal) < 50.f)
	{
		const FVector DetourGoal = ComputeDetourGoal(PawnLoc, ShipLoc, AIC, 300.f, 0);

		UE_LOG(LogChaseTarget, Warning,
			TEXT("[Chase][MoveTowardShip] NavGoal invalid -> Detour 시도 | Pawn=%s DetourGoal=%s"),
			*Pawn->GetName(),
			*DetourGoal.ToString());

		IssueMoveToLocation(AIC, DetourGoal, AcceptanceRadius);
		return;
	}

	IssueMoveToLocation(AIC, NavGoal, AcceptanceRadius);
}

// ============================================================================
// 헬퍼: 우회 오프셋 목표 계산 (좌/우 랜덤)
// ============================================================================
FVector ComputeDetourGoal(
	const FVector& PawnLoc,
	const FVector& TargetLoc,
	AAIController* AIC,
	float OffsetAmount,
	int32 DirectionSign /* = 0 */)
{
	const FVector DirToTarget = (TargetLoc - PawnLoc).GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, DirToTarget).GetSafeNormal();
	const float Sign = DirectionSign == 0
		? ((FMath::RandBool()) ? 1.f : -1.f)
		: (DirectionSign > 0 ? 1.f : -1.f);

	const FVector RawGoal = PawnLoc + DirToTarget * OffsetAmount * 0.5f + Right * Sign * OffsetAmount;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(AIC->GetWorld());
	FNavLocation NavGoal;
	if (NavSys && NavSys->ProjectPointToNavigation(RawGoal, NavGoal, FVector(300.f, 300.f, 500.f)))
		return NavGoal.Location;

	return RawGoal;
}

int32 ResolveDetourDirectionSign(
	FSTTask_ChaseTarget_TestInstanceData& D,
	bool bUsePersistentDirection)
{
	if (!bUsePersistentDirection)
		return FMath::RandBool() ? 1 : -1;

	if (D.DetourDirectionSign == 0)
		D.DetourDirectionSign = FMath::RandBool() ? 1 : -1;

	return D.DetourDirectionSign;
}

// ============================================================================
// 헬퍼: EQS 실행 후 결과 위치로 이동 (플레이어 전용)
// ============================================================================
void RunPlayerAttackEQS(
	UEnvQuery* Query,
	AAIController* AIController,
	float InAcceptanceRadius,
	AActor* FallbackTarget)
{
	if (!Query || !IsValid(AIController)) return;

	APawn* Pawn = AIController->GetPawn();
	if (!IsValid(Pawn)) return;

	UWorld* World = Pawn->GetWorld();
	if (!World) return;

	UEnvQueryManager* EQSManager = UEnvQueryManager::GetCurrent(World);
	if (!EQSManager) return;

	TWeakObjectPtr<AAIController> WeakController = AIController;
	TWeakObjectPtr<AActor> WeakFallback = FallbackTarget;

	FEnvQueryRequest Request(Query, Pawn);
	Request.Execute(EEnvQueryRunMode::SingleResult,
		FQueryFinishedSignature::CreateLambda(
			[WeakController, WeakFallback, InAcceptanceRadius](TSharedPtr<FEnvQueryResult> Result)
			{
				AAIController* Ctrl = WeakController.Get();
				if (!IsValid(Ctrl)) return;

				if (Result.IsValid() && !Result->IsAborted() && Result->Items.Num() > 0)
				{
					IssueMoveToLocation(Ctrl, Result->GetItemAsLocation(0), InAcceptanceRadius);
				}
				else if (AActor* Fallback = WeakFallback.Get(); IsValid(Fallback))
				{
					Ctrl->MoveToActor(Fallback, InAcceptanceRadius, true, true, false, nullptr, true);
				}
			}));
}

// ============================================================================
// 헬퍼: 공격 태그 확인
// ============================================================================
bool IsAttacking(APawn* Pawn)
{
	if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn))
	{
		if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
		{
			FGameplayTag AttackingTag = FGameplayTag::RequestGameplayTag(
				FName("State.Enemy.Attacking"), false);
			if (AttackingTag.IsValid() && ASC->HasMatchingGameplayTag(AttackingTag))
				return true;
		}
	}
	return false;
}

// ============================================================================
// 헬퍼: 표면 거리 계산
//   우주선처럼 크기가 큰 Actor는 중심점 거리가 아닌 메시 표면까지 최단 거리.
// ============================================================================
float ComputeSurfaceDistance(const FVector& FromLoc, const AActor* ToActor)
{
	if (!ToActor) return MAX_FLT;

	// 루트 PrimitiveComponent의 표면 거리
	if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(ToActor->GetRootComponent()))
	{
		if (RootPrim->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			FVector ClosestPoint;
			const float D = RootPrim->GetClosestPointOnCollision(FromLoc, ClosestPoint);
			if (D >= 0.f) return D;
		}
	}

	// ShipCombatCollision 태그 컴포넌트
	TArray<UPrimitiveComponent*> Prims;
	ToActor->GetComponents<UPrimitiveComponent>(Prims);

	float MinDist = MAX_FLT;
	bool bFound = false;
	for (UPrimitiveComponent* Prim : Prims)
	{
		if (!Prim || Prim->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			continue;

		FVector Closest;
		const float D = Prim->GetClosestPointOnCollision(FromLoc, Closest);
		if (D >= 0.f && D < MinDist)
		{
			MinDist = D;
			bFound = true;
		}
	}

	if (bFound) return MinDist;

	return FVector::Dist(FromLoc, ToActor->GetActorLocation());
}

// ============================================================================
// 헬퍼: SpaceShipAttackSlotManager 가져오기
// ============================================================================
USpaceShipAttackSlotManager* GetSlotManager(AActor* SpaceShip)
{
	if (!SpaceShip) return nullptr;
	return SpaceShip->FindComponentByClass<USpaceShipAttackSlotManager>();
}

// ============================================================================
// 헬퍼: 위치 변화량 기반 Stuck 감지
// ============================================================================
bool CheckPositionBasedStuck(
	FSTTask_ChaseTarget_TestInstanceData& D,
	const FVector& CurrentLoc,
	float SurfaceDist,
	float AttackThreshold,
	float DeltaTime,
	float InStuckCheckInterval,
	float InStuckDistThreshold)
{
	D.StuckCheckTimer += DeltaTime;

	if (D.StuckCheckTimer < InStuckCheckInterval)
		return false;

	const float MovedDist = FVector::Dist(CurrentLoc, D.LastCheckedLocation);
	const bool bStuck = (MovedDist <= InStuckDistThreshold)
		&& (SurfaceDist > AttackThreshold + 150.f);

	if (bStuck)
		D.ConsecutiveStuckCount++;
	else
	{
		D.ConsecutiveStuckCount = 0;
		D.DetourDirectionSign = 0;
	}

	D.LastCheckedLocation = CurrentLoc;
	D.StuckCheckTimer = 0.f;

	return bStuck;
}
} // namespace ChaseTargetTestHelpers
// Unity Build에서 ChaseTargetHelpers와 이름 충돌 방지를 위해
// using namespace 대신 명시적 네임스페이스 사용
namespace CTH = ChaseTargetTestHelpers;

// ============================================================================
// EnterState
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseTarget_Test::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& D = Context.GetInstanceData(*this);

	AAIController* AIC = D.AIController;
	if (!IsValid(AIC))
	{
		UE_LOG(LogChaseTarget, Warning, TEXT("[Chase] EnterState - AIController 무효. Failed 반환"));
		return EStateTreeRunStatus::Failed;
	}

	const FHellunaAITargetData& TD = D.TargetData;

	// 인스턴스 데이터 초기화
	D.TimeSinceRepath       = 0.f;
	D.TimeUntilNextEQS      = 0.f;
	D.StuckCheckTimer       = 0.f;
	D.ConsecutiveStuckCount = 0;
	D.DetourDirectionSign   = 0;
	D.AssignedSlotIndex     = -1;
	D.AssignedSlotLocation  = FVector::ZeroVector;
	D.SlotRetryTimer        = 0.f;
	D.bSlotArrived          = false;
	D.WaitingDestination    = FVector::ZeroVector;
	D.bHasWaitingDestination = false;
	D.ConsecutiveSlotFailures = 0;

	APawn* Pawn = AIC->GetPawn();
	if (IsValid(Pawn))
		D.LastCheckedLocation = Pawn->GetActorLocation();

	if (!TD.HasValidTarget())
	{
		UE_LOG(LogChaseTarget, Log, TEXT("[Chase] EnterState - [%s] 유효한 타겟 없음. Running 대기"),
			IsValid(Pawn) ? *Pawn->GetName() : TEXT("NoPawn"));
		D.LastMoveTarget = nullptr;
		return EStateTreeRunStatus::Running;
	}

	AActor* TargetActor = TD.TargetActor.Get();
	const bool bIsSpaceShip = (Cast<AResourceUsingObject_SpaceShip>(TargetActor) != nullptr);

	UE_LOG(LogChaseTarget, Log,
		TEXT("[Chase] EnterState - [%s] -> [%s] | 타입=%s | 거리=%.1f"),
		IsValid(Pawn) ? *Pawn->GetName() : TEXT("NoPawn"),
		*TargetActor->GetName(),
		bIsSpaceShip ? TEXT("우주선") : TEXT("플레이어"),
		TD.DistanceToTarget);

	if (!IsValid(Pawn))
	{
		D.LastMoveTarget = TD.TargetActor;
		return EStateTreeRunStatus::Running;
	}

	// ── 이동 시작 ────────────────────────────────────────────
	if (bIsSpaceShip)
	{
		const float SurfaceDist = CTH::ComputeSurfaceDistance(Pawn->GetActorLocation(), TargetActor);

		// 이미 공격 범위 안이면 Running 대기 (Attack State 전환 대기)
		if (SurfaceDist <= AttackRange)
		{
			UE_LOG(LogChaseTarget, Log,
				TEXT("[Chase] EnterState - [%s] 우주선 공격 범위 내 (표면거리=%.1f). Running 대기"),
				*Pawn->GetName(), SurfaceDist);
			D.LastMoveTarget = TD.TargetActor;
			return EStateTreeRunStatus::Running;
		}

		USpaceShipAttackSlotManager* SlotMgr = bUseSlotSystem ? CTH::GetSlotManager(TargetActor) : nullptr;

		// 슬롯 진입 판정: 표면거리 기준 (우주선이 크면 중심거리가 항상 크므로)
		if (SlotMgr && SurfaceDist <= SlotEngageRadius)
		{
			// 슬롯 진입 범위 안: 슬롯 배정 시도
			int32 SlotIdx = -1;
			FVector SlotLoc;
			if (SlotMgr->RequestSlot(Pawn, SlotIdx, SlotLoc))
			{
				D.AssignedSlotIndex    = SlotIdx;
				D.AssignedSlotLocation = SlotLoc;
				CTH::IssueMoveToLocation(AIC, SlotLoc, AcceptanceRadius);
				UE_LOG(LogChaseTarget, Log,
					TEXT("[Chase] EnterState - [%s] 슬롯 배정 성공: %d -> %s"),
					*Pawn->GetName(), SlotIdx, *SlotLoc.ToString());
			}
			else
			{
				FVector WaitLoc;
				if (SlotMgr->GetWaitingPosition(Pawn, WaitLoc))
					CTH::IssueMoveToLocation(AIC, WaitLoc, AcceptanceRadius);
				else
					CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius);
			}
		}
		else
		{
			// 원거리 또는 슬롯 미사용: NavMesh 경유점 접근
			CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius);
		}
	}
	else
	{
		// 플레이어: 이미 공격 범위면 즉시 Succeeded
		if (TD.DistanceToTarget <= PlayerAttackRange)
		{
			UE_LOG(LogChaseTarget, Log,
				TEXT("[Chase] EnterState - [%s] 플레이어 공격 범위 내. Succeeded"),
				*Pawn->GetName());
			return EStateTreeRunStatus::Succeeded;
		}

		AIC->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);
	}

	D.LastMoveTarget = TD.TargetActor;
	return EStateTreeRunStatus::Running;
}

// ============================================================================
// Tick
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseTarget_Test::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& D = Context.GetInstanceData(*this);

	AAIController* AIC = D.AIController;
	if (!IsValid(AIC)) return EStateTreeRunStatus::Failed;

	const FHellunaAITargetData& TD = D.TargetData;
	if (!TD.HasValidTarget()) return EStateTreeRunStatus::Failed;

	APawn* Pawn = AIC->GetPawn();
	if (!IsValid(Pawn)) return EStateTreeRunStatus::Failed;

	AActor* TargetActor = TD.TargetActor.Get();
	if (!IsValid(TargetActor)) return EStateTreeRunStatus::Failed;

	// ── 공격 중이면 이동 정지 ────────────────────────────────
	if (CTH::IsAttacking(Pawn))
	{
		AIC->StopMovement();
		return EStateTreeRunStatus::Running;
	}

	const bool bIsSpaceShip = (Cast<AResourceUsingObject_SpaceShip>(TargetActor) != nullptr);

	D.TimeSinceRepath  += DeltaTime;
	D.TimeUntilNextEQS -= DeltaTime;

	if (bIsSpaceShip)
	{
		// ══════════════════════════════════════════════════════
		// 우주선 이동
		// ══════════════════════════════════════════════════════

		const FVector PawnLoc = Pawn->GetActorLocation();
		const float SurfaceDist = CTH::ComputeSurfaceDistance(PawnLoc, TargetActor);
		const float CenterDist = FVector::Dist(PawnLoc, TargetActor->GetActorLocation());

		// ── 디버그 로그 (2초마다) ────────────────────────────
		static float ShipDbgTimer = 0.f;
		ShipDbgTimer += DeltaTime;
		if (ShipDbgTimer >= 2.f)
		{
			ShipDbgTimer = 0.f;
			UE_LOG(LogChaseTarget, Log,
				TEXT("[Chase] Tick - [%s] 우주선 | 표면거리=%.1f 중심거리=%.1f | SlotIdx=%d SlotArrived=%d | PFC=%d"),
				*Pawn->GetName(), SurfaceDist, CenterDist,
				D.AssignedSlotIndex, (int)D.bSlotArrived,
				AIC->GetPathFollowingComponent() ? (int32)AIC->GetPathFollowingComponent()->GetStatus() : -1);
		}

		// ── Stuck 감지 (토글: bUseStuckDetour) ──────────────
		const bool bStuck = bUseStuckDetour
			? CTH::CheckPositionBasedStuck(D, PawnLoc, SurfaceDist, AttackRange,
				DeltaTime, StuckCheckInterval, StuckDistThreshold)
			: false;

		if (bStuck)
		{
			UE_LOG(LogChaseTarget, Warning,
				TEXT("[Chase] Tick - [%s] *** STUCK *** | 연속=%d | 표면거리=%.1f"),
				*Pawn->GetName(), D.ConsecutiveStuckCount, SurfaceDist);
		}

		// ── Idle 감지 ───────────────────────────────────────
		bool bIdle = false;
		if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
			bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);

		// ── 슬롯 시스템 분기 ────────────────────────────────
		USpaceShipAttackSlotManager* SlotMgr = bUseSlotSystem ? CTH::GetSlotManager(TargetActor) : nullptr;

		if (SlotMgr)
		{
			// ─── 슬롯 시스템 ON ─────────────────────────────

			// 이미 슬롯 도착: 정지 유지
			if (D.bSlotArrived)
			{
				AIC->StopMovement();

				// 우주선 방향 바라보기
				const FVector ToShip = (TargetActor->GetActorLocation() - PawnLoc).GetSafeNormal2D();
				if (!ToShip.IsNearlyZero())
					AIC->SetFocalPoint(PawnLoc + ToShip * 1000.f);

				return EStateTreeRunStatus::Running;
			}

			// 슬롯 미배정 상태
			if (D.AssignedSlotIndex < 0)
			{
				// 슬롯 진입 범위 밖: 우주선 방향 접근 (표면거리 기준)
				if (SurfaceDist > SlotEngageRadius)
				{
					if (D.TimeSinceRepath >= RepathInterval || bIdle || bStuck)
					{
						if (bStuck)
						{
							const float Mult = FMath::Min((float)D.ConsecutiveStuckCount, 3.f);
							const int32 DetourDirectionSign = CTH::ResolveDetourDirectionSign(D, bUsePersistentDetourDirection);
							CTH::IssueMoveToLocation(AIC,
								CTH::ComputeDetourGoal(PawnLoc, TargetActor->GetActorLocation(), AIC, DetourOffset * Mult, DetourDirectionSign),
								AcceptanceRadius);
						}
						else
						{
							CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius);
						}
						D.TimeSinceRepath = 0.f;
					}
					return EStateTreeRunStatus::Running;
				}

				// 연속 실패 3회 이상: 슬롯 포기, 자유 이동
				if (D.ConsecutiveSlotFailures >= 3)
				{
					if (D.TimeSinceRepath >= RepathInterval || bIdle || bStuck)
					{
						CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius);
						D.TimeSinceRepath = 0.f;
					}
					return EStateTreeRunStatus::Running;
				}

				// 슬롯 진입 범위 안: 배정 재시도 (2초 쿨다운)
				D.SlotRetryTimer -= DeltaTime;
				if (D.SlotRetryTimer <= 0.f)
				{
					D.SlotRetryTimer = 2.f;
					int32 SlotIdx = -1;
					FVector SlotLoc;
					if (SlotMgr->RequestSlot(Pawn, SlotIdx, SlotLoc))
					{
						D.AssignedSlotIndex    = SlotIdx;
						D.AssignedSlotLocation = SlotLoc;
						D.bSlotArrived         = false;
						D.bHasWaitingDestination = false;
						CTH::IssueMoveToLocation(AIC, SlotLoc, AcceptanceRadius);
					}
					else
					{
						// 슬롯 없음: 우주선 방향 자유 이동 (멍때림 방지)
						CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius);
					}
				}

				// 대기 중(슬롯 미배정 + Idle): 우주선 방향 접근
				if (bIdle && SurfaceDist > AttackRange + 100.f)
				{
					CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius);
				}
				else if (bIdle)
				{
					// 공격 범위 내 대기: 우주선을 바라보고 서있기
					const FVector ToShip = (TargetActor->GetActorLocation() - PawnLoc).GetSafeNormal2D();
					if (!ToShip.IsNearlyZero())
					{
						Pawn->SetActorRotation(FMath::RInterpTo(
							Pawn->GetActorRotation(),
							FRotator(0.f, ToShip.Rotation().Yaw, 0.f),
							DeltaTime, 5.f));
					}
				}

				return EStateTreeRunStatus::Running;
			}

			// 슬롯 배정됨 → 도착 판정
			const float DistToSlot = FVector::Dist(PawnLoc, D.AssignedSlotLocation);
			if (DistToSlot <= AcceptanceRadius + 30.f)
			{
				D.bSlotArrived = true;
				D.ConsecutiveSlotFailures = 0;
				AIC->StopMovement();
				SlotMgr->OccupySlot(D.AssignedSlotIndex, Pawn);

				const FVector ToShip = (TargetActor->GetActorLocation() - PawnLoc).GetSafeNormal2D();
				if (!ToShip.IsNearlyZero())
					AIC->SetFocalPoint(PawnLoc + ToShip * 1000.f);

				UE_LOG(LogChaseTarget, Log,
					TEXT("[Chase] Tick - [%s] 슬롯 도착: %d, 거리=%.1f"),
					*Pawn->GetName(), D.AssignedSlotIndex, DistToSlot);

				return EStateTreeRunStatus::Running;
			}

			// Stuck/Idle: 슬롯 반납 후 재배정
			bool bNeedReassign = bStuck;
			if (bIdle && DistToSlot > AcceptanceRadius + 200.f)
				bNeedReassign = true;

			if (bNeedReassign)
			{
				const bool bIdleRelease = !bStuck;

				UE_LOG(LogChaseTarget, Warning,
					TEXT("[Chase] Tick - [%s] 슬롯 재배정 (이유: %s), 이전 슬롯=%d, 연속실패=%d"),
					*Pawn->GetName(), bStuck ? TEXT("Stuck") : TEXT("Idle"),
					D.AssignedSlotIndex, D.ConsecutiveSlotFailures);

				SlotMgr->ReleaseSlotByIndex(D.AssignedSlotIndex);
				D.AssignedSlotIndex     = -1;
				D.ConsecutiveStuckCount = 0;
				D.DetourDirectionSign   = 0;

				if (bIdleRelease)
				{
					// PFC=Idle = 경로 미발견 → 쿨다운 부여 (매 프레임 재배정 방지)
					D.ConsecutiveSlotFailures++;
					D.SlotRetryTimer = 2.f;

					// 대기 중 우주선 방향으로 자유 이동 (멍때림 방지)
					CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius);
				}
				else
				{
					// Stuck: 빠른 재시도 허용
					D.SlotRetryTimer = 0.5f;
				}
			}
		}
		else
		{
			// ─── 슬롯 시스템 OFF (자유 이동) ────────────────
			// 이동 중(Moving)이면 Repath를 건너뛰어 경로 리셋 진동 방지
			bool bMoving = false;
			if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
				bMoving = (PFC->GetStatus() == EPathFollowingStatus::Moving);

			if (bStuck)
			{
				const float Mult = FMath::Min((float)D.ConsecutiveStuckCount, 3.f);
				const int32 DetourDirectionSign = CTH::ResolveDetourDirectionSign(D, bUsePersistentDetourDirection);
				const FVector DetourGoal = CTH::ComputeDetourGoal(
					PawnLoc, TargetActor->GetActorLocation(), AIC, DetourOffset * Mult, DetourDirectionSign);

				UE_LOG(LogChaseTarget, Log,
					TEXT("[Chase] Tick - [%s] 우주선 Stuck 우회 | 배수=%.1f"),
					*Pawn->GetName(), Mult);

				CTH::IssueMoveToLocation(AIC, DetourGoal, AcceptanceRadius);
				D.TimeSinceRepath = 0.f;
			}
			else if (bIdle && SurfaceDist > AttackRange + 100.f)
			{
				// Idle인데 아직 멀면 재이동
				CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius);
				D.TimeSinceRepath = 0.f;
			}
			else if (!bMoving && D.TimeSinceRepath >= RepathInterval)
			{
				// 이동 중이 아닐 때만 Repath (이동 중 Repath는 경로 리셋 진동 유발)
				CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius);
				D.TimeSinceRepath = 0.f;
			}
		}

		// 우주선 방향 회전 (슬롯 도착 상태가 아닐 때)
		// CharacterMovement의 bOrientRotationToMovement가 이동 방향 회전을 처리하므로
		// 이동 중에는 수동 회전을 하지 않는다 (두 시스템이 충돌하면 움찔거림 발생).
		// 정지 상태(Idle)에서만 우주선을 향해 회전한다.
		if (!D.bSlotArrived && bIdle)
		{
			const FVector ToShip = (TargetActor->GetActorLocation() - PawnLoc).GetSafeNormal2D();
			if (!ToShip.IsNearlyZero())
			{
				Pawn->SetActorRotation(FMath::RInterpTo(
					Pawn->GetActorRotation(),
					FRotator(0.f, ToShip.Rotation().Yaw, 0.f),
					DeltaTime, 5.f));
			}
		}
	}
	else
	{
		// ══════════════════════════════════════════════════════
		// 플레이어 이동
		// ══════════════════════════════════════════════════════

		const float DistToTarget = TD.DistanceToTarget;

		// 공격 범위 도달 → Succeeded
		if (DistToTarget <= PlayerAttackRange)
		{
			UE_LOG(LogChaseTarget, Log,
				TEXT("[Chase] Tick - [%s] 플레이어 공격 범위 도달 (거리=%.1f). Succeeded"),
				*Pawn->GetName(), DistToTarget);
			AIC->StopMovement();
			return EStateTreeRunStatus::Succeeded;
		}

		// Stuck 감지 (토글: bUseStuckDetour)
		const bool bStuck = bUseStuckDetour
			? CTH::CheckPositionBasedStuck(D, Pawn->GetActorLocation(), DistToTarget, PlayerAttackRange,
				DeltaTime, StuckCheckInterval, StuckDistThreshold)
			: false;

		bool bIdle = false;
		if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
			bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);

		const bool bRepathDue     = D.TimeSinceRepath >= RepathInterval;
		const bool bTargetChanged = D.LastMoveTarget != TD.TargetActor;

		if (bTargetChanged)
		{
			D.ConsecutiveStuckCount = 0;
			D.DetourDirectionSign = 0;
			D.TimeUntilNextEQS = 0.f;
		}

		if (bTargetChanged || bRepathDue || bStuck || (bIdle && DistToTarget > PlayerAttackRange + 100.f))
		{
			if (bStuck)
			{
				const float Mult = FMath::Min((float)D.ConsecutiveStuckCount, 3.f);
				const int32 DetourDirectionSign = CTH::ResolveDetourDirectionSign(D, bUsePersistentDetourDirection);
				CTH::IssueMoveToLocation(AIC,
					CTH::ComputeDetourGoal(Pawn->GetActorLocation(), TargetActor->GetActorLocation(), AIC, DetourOffset * Mult, DetourDirectionSign),
					AcceptanceRadius);
			}
			else if (bUseEQS && AttackPositionQuery && D.TimeUntilNextEQS <= 0.f)
			{
				CTH::RunPlayerAttackEQS(AttackPositionQuery, AIC, AcceptanceRadius, TargetActor);
				D.TimeUntilNextEQS = EQSInterval;
			}
			else
			{
				AIC->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);
			}

			D.LastMoveTarget  = TD.TargetActor;
			D.TimeSinceRepath = 0.f;
		}

		// 플레이어를 향해 회전 (정지 상태에서만 — 이동 중은 CharacterMovement가 처리)
		if (bIdle)
		{
			const FVector ToTarget = (TargetActor->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
			if (!ToTarget.IsNearlyZero())
			{
				Pawn->SetActorRotation(FMath::RInterpTo(
					Pawn->GetActorRotation(),
					FRotator(0.f, ToTarget.Rotation().Yaw, 0.f),
					DeltaTime, 5.f));
			}
		}
	}

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// ExitState
// ============================================================================
void FSTTask_ChaseTarget_Test::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& D = Context.GetInstanceData(*this);

	// 슬롯 반납
	if (bUseSlotSystem && D.AssignedSlotIndex >= 0)
	{
		const FHellunaAITargetData& TD = D.TargetData;
		if (TD.HasValidTarget())
		{
			if (USpaceShipAttackSlotManager* SlotMgr = CTH::GetSlotManager(TD.TargetActor.Get()))
			{
				if (IsValid(D.AIController))
				{
					SlotMgr->ReleaseSlot(D.AIController->GetPawn());
					UE_LOG(LogChaseTarget, Log,
						TEXT("[Chase] ExitState 슬롯 반납: %d"), D.AssignedSlotIndex);
				}
			}
		}
		D.AssignedSlotIndex = -1;
		D.bSlotArrived      = false;
	}

	if (IsValid(D.AIController))
	{
		UE_LOG(LogChaseTarget, Log,
			TEXT("[Chase] ExitState - [%s] | StuckCount=%d"),
			IsValid(D.AIController->GetPawn()) ? *D.AIController->GetPawn()->GetName() : TEXT("NoPawn"),
			D.ConsecutiveStuckCount);

		D.AIController->StopMovement();
	}
}
