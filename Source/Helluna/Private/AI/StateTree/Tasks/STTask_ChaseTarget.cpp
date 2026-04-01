/**
 * STTask_ChaseTarget.cpp
 *
 * 플레이어/우주선을 향해 이동 명령을 발행하고 경로를 유지한다.
 *
 * ─── 우주선 이동 방식 (슬롯 기반) ───────────────────────────
 *  1. EnterState에서 SpaceShipAttackSlotManager에 슬롯 요청
 *  2. 배정된 슬롯 위치로 MoveToLocation
 *  3. 도착 판정 시 OccupySlot 호출 → Tick 종료 (Attack State 전환 대기)
 *  4. Stuck/경로 실패 시 슬롯 반납 후 재배정
 *
 *  [플레이어]
 *    - RepathInterval마다 MoveToActor 재발행 (움직이는 대상 추적)
 *    - AttackPositionQuery 설정 시 EQS로 공격 위치 계산 후 이동
 *    - 이동 중 타겟 방향으로 서서히 회전 (RInterpTo)
 *    - Stuck(이동 중 저속) 감지 시 즉시 재발행
 *
 * @author 김민우
 */

#include "AI/StateTree/Tasks/STTask_ChaseTarget.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "AITypes.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "GameFramework/Pawn.h"

#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "AI/SpaceShipAttackSlotManager.h"

namespace ChaseTargetHelpers {
// ============================================================================
// 헬퍼: 위치로 이동 명령
// ============================================================================
void IssueMoveToLocation(AAIController* AIController, const FVector& Goal, float AcceptanceRadius)
{
	if (!AIController) return;

	FAIMoveRequest Req;
	Req.SetGoalLocation(Goal);
	Req.SetAcceptanceRadius(AcceptanceRadius);
	Req.SetReachTestIncludesAgentRadius(false);
	Req.SetReachTestIncludesGoalRadius(false);
	Req.SetUsePathfinding(true);
	Req.SetAllowPartialPath(true);
	Req.SetCanStrafe(false);
	AIController->MoveTo(Req);
}

// ============================================================================
// 헬퍼: 우주선 NavMesh 투영 이동 (MoveToActor 대체)
// 우주선 Root가 메시 내부/상공 → NavMesh 도달 불가 → 멈춤 방지
// ============================================================================
void IssueMoveToActorNavProjected(AAIController* AIController, AActor* TargetActor, float AcceptanceRadius)
{
	if (!AIController || !TargetActor) return;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(AIController->GetWorld());
	if (NavSys)
	{
		FNavLocation NavLoc;
		if (NavSys->ProjectPointToNavigation(TargetActor->GetActorLocation(), NavLoc, FVector(500.f, 500.f, 1000.f)))
		{
			ChaseTargetHelpers::IssueMoveToLocation(AIController, NavLoc.Location, AcceptanceRadius);
			return;
		}
	}
	ChaseTargetHelpers::IssueMoveToLocation(AIController, TargetActor->GetActorLocation(), AcceptanceRadius);
}

// ============================================================================
// 헬퍼: SpaceShipAttackSlotManager 가져오기
// ============================================================================
USpaceShipAttackSlotManager* GetSlotManager(AActor* SpaceShip)
{
	if (!SpaceShip) return nullptr;
	return SpaceShip->FindComponentByClass<USpaceShipAttackSlotManager>();
}

float ComputeSlotReserveRadius(const float SlotEngageRadius, const float AcceptanceRadius)
{
	return FMath::Clamp(SlotEngageRadius * 0.6f, AcceptanceRadius + 100.f, SlotEngageRadius);
}

// ============================================================================
// 헬퍼: EQS 실행 후 결과 위치로 이동 (플레이어 전용)
// ============================================================================
void RunAttackPositionEQS(
	UEnvQuery* Query,
	AAIController* AIController,
	float AcceptanceRadius,
	AActor* FallbackTarget)
{
	if (!Query || !AIController) return;

	APawn* Pawn = AIController->GetPawn();
	if (!Pawn) return;

	UWorld* World = Pawn->GetWorld();
	if (!World) return;

	UEnvQueryManager* EQSManager = UEnvQueryManager::GetCurrent(World);
	if (!EQSManager) return;

	TWeakObjectPtr<AAIController> WeakController = AIController;
	TWeakObjectPtr<AActor> WeakFallback = FallbackTarget;

	FEnvQueryRequest Request(Query, Pawn);
	Request.Execute(EEnvQueryRunMode::SingleResult,
		FQueryFinishedSignature::CreateLambda(
			[WeakController, WeakFallback, AcceptanceRadius](TSharedPtr<FEnvQueryResult> Result)
			{
				AAIController* Ctrl = WeakController.Get();
				if (!Ctrl) return;

				if (Result.IsValid() && !Result->IsAborted() && Result->Items.Num() > 0)
				{
					ChaseTargetHelpers::IssueMoveToLocation(Ctrl, Result->GetItemAsLocation(0), AcceptanceRadius);
				}
				else if (AActor* Fallback = WeakFallback.Get())
				{
					Ctrl->MoveToActor(Fallback, AcceptanceRadius, true, true, false, nullptr, true);
				}
			}));
}
} // namespace ChaseTargetHelpers
using namespace ChaseTargetHelpers;

// ============================================================================
// EnterState
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseTarget::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	AAIController* AIController = InstanceData.AIController;
	if (!AIController)
	{
		UE_LOG(LogTemp, Error, TEXT("[enemybugreport][ChaseEnterFail] Reason=NullAIController"));
		return EStateTreeRunStatus::Failed;
	}

	const FHellunaAITargetData& TargetData = InstanceData.TargetData;
	if (!TargetData.HasValidTarget())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[enemybugreport][ChaseEnterFail] Reason=NoTarget"));
		InstanceData.LastMoveTarget      = nullptr;
		InstanceData.TimeSinceRepath     = 0.f;
		InstanceData.TimeUntilNextEQS    = 0.f;
		InstanceData.AssignedSlotIndex   = -1;
		InstanceData.bSlotArrived        = false;
		return EStateTreeRunStatus::Running;
	}

	AActor* TargetActor = TargetData.TargetActor.Get();
	const bool bIsSpaceShip = (Cast<AResourceUsingObject_SpaceShip>(TargetActor) != nullptr);

	UE_LOG(LogTemp, Verbose, TEXT("[enemybugreport][ChaseEnterState] Target=%s bIsSpaceShip=%d Dist=%.1f bUseSlotSystem=%d"),
		*GetNameSafe(TargetActor), (int)bIsSpaceShip, TargetData.DistanceToTarget, (int)bUseSlotSystem);

	InstanceData.TimeSinceRepath    = 0.f;
	InstanceData.TimeUntilNextEQS   = 0.f;
	InstanceData.StuckAccumTime     = 0.f;
	InstanceData.AssignedSlotIndex  = -1;
	InstanceData.bSlotArrived       = false;
	InstanceData.SlotRetryTimer     = 0.f;
	InstanceData.MovementDiagTimer  = 0.f;

	if (bIsSpaceShip)
	{
		USpaceShipAttackSlotManager* SlotMgr = bUseSlotSystem ? GetSlotManager(TargetActor) : nullptr;
		const float DistToShip = TargetData.DistanceToTarget;
		const float SlotReserveRadius = ChaseTargetHelpers::ComputeSlotReserveRadius(SlotEngageRadius, AcceptanceRadius);

		// 슬롯이 0개면 슬롯매니저가 없는 것과 동일 취급
		if (SlotMgr && SlotMgr->GetSlots().Num() == 0)
			SlotMgr = nullptr;

		if (SlotMgr)
		{
			// 슬롯 예약 반경 밖: 우주선 방향 NavMesh 위 중간 지점으로 직접 이동
			// 슬롯을 너무 멀리서 선점하지 않도록 충분히 가까워진 뒤에만 예약한다.
			if (DistToShip > SlotReserveRadius)
			{
				APawn* P = AIController->GetPawn();
				const FVector PawnLoc = P ? P->GetActorLocation() : TargetActor->GetActorLocation();
				const FVector Dir     = (TargetActor->GetActorLocation() - PawnLoc).GetSafeNormal();
				FVector RawGoal = PawnLoc + Dir * FMath::Min(DistToShip - SlotReserveRadius * 0.8f, 1500.f);

				// 몬스터별 분산: 이동 방향의 수직 방향으로 랜덤 오프셋 추가 (뭉침 방지)
				const FVector Perp = FVector(-Dir.Y, Dir.X, 0.f); // 수평면 수직 벡터
				const float SpreadOffset = FMath::FRandRange(-ShipSpreadRadius, ShipSpreadRadius);
				RawGoal += Perp * SpreadOffset;

				UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(AIController->GetWorld());
				FNavLocation NavGoal;
				FVector FinalGoal = RawGoal;
				if (NavSys && NavSys->ProjectPointToNavigation(RawGoal, NavGoal, FVector(200.f, 200.f, 500.f)))
					FinalGoal = NavGoal.Location;

				UE_LOG(LogTemp, Verbose, TEXT("[enemybugreport][ChaseShipApproachVerbose] Goal=%s Dist=%.1f"), *FinalGoal.ToString(), DistToShip);
				UE_LOG(LogTemp, Warning,
					TEXT("[enemybugreport][ChaseEnter] Enemy=%s Mode=ShipApproach DistToShip=%.1f ReserveRadius=%.1f Goal=%s Pawn=%s"),
					*GetNameSafe(AIController->GetPawn()),
					DistToShip,
					SlotReserveRadius,
					*FinalGoal.ToCompactString(),
					P ? *P->GetActorLocation().ToCompactString() : TEXT("None"));
				ChaseTargetHelpers::IssueMoveToLocation(AIController, FinalGoal, AcceptanceRadius);
			}
			// 슬롯 예약 반경 안: 슬롯 배정 시도
			else if (SlotMgr->GetSlots().Num() > 0)
			{
				int32 SlotIdx = -1;
				FVector SlotLoc;
				if (SlotMgr->RequestSlot(AIController->GetPawn(), SlotIdx, SlotLoc))
				{
					InstanceData.AssignedSlotIndex    = SlotIdx;
					InstanceData.AssignedSlotLocation = SlotLoc;
					UE_LOG(LogTemp, Warning,
						TEXT("[enemybugreport][ChaseEnter] Enemy=%s Mode=RequestSlot Slot=%d SlotLoc=%s DistToShip=%.1f"),
						*GetNameSafe(AIController->GetPawn()),
						SlotIdx,
						*SlotLoc.ToCompactString(),
						DistToShip);
					ChaseTargetHelpers::IssueMoveToLocation(AIController, SlotLoc, AcceptanceRadius);
				}
				else
				{
					FVector WaitLoc;
					if (SlotMgr->GetWaitingPosition(AIController->GetPawn(), WaitLoc))
					{
						UE_LOG(LogTemp, Warning,
							TEXT("[enemybugreport][ChaseEnter] Enemy=%s Mode=WaitPosition WaitLoc=%s DistToShip=%.1f"),
							*GetNameSafe(AIController->GetPawn()),
							*WaitLoc.ToCompactString(),
							DistToShip);
						ChaseTargetHelpers::IssueMoveToLocation(AIController, WaitLoc, AcceptanceRadius);
					}
					else
					{
						UE_LOG(LogTemp, Warning,
							TEXT("[enemybugreport][ChaseEnter] Enemy=%s Mode=FallbackMoveToShip DistToShip=%.1f"),
							*GetNameSafe(AIController->GetPawn()),
							DistToShip);
						ChaseTargetHelpers::IssueMoveToActorNavProjected(AIController, TargetActor, AcceptanceRadius);
					}
				}
			}
			else
			{
				ChaseTargetHelpers::IssueMoveToActorNavProjected(AIController, TargetActor, AcceptanceRadius);
			}
		}
		else
		{
			ChaseTargetHelpers::IssueMoveToActorNavProjected(AIController, TargetActor, AcceptanceRadius);
		}
	}
	else
	{
		// 플레이어: 직접 추적 시작
		AIController->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);
	}

	InstanceData.LastMoveTarget = TargetData.TargetActor;

	// ── 공통 디버그: MoveToActor/SlotMove 결과 및 NavMesh/PFC 상태 ──────
	if (UPathFollowingComponent* PFC = AIController->GetPathFollowingComponent())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[enemybugreport][ChasePFC] Status=%d"),
			(int)PFC->GetStatus());
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("[enemybugreport][ChasePFC] MissingPathFollowingComponent"));
	}

	if (APawn* Pawn = AIController->GetPawn())
	{
		if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(AIController->GetWorld()))
		{
			FNavLocation NavLoc;
			const bool bOnNav = NavSys->ProjectPointToNavigation(Pawn->GetActorLocation(), NavLoc, INVALID_NAVEXTENT, nullptr);
			UE_LOG(LogTemp, Verbose, TEXT("[enemybugreport][ChaseNav] Pawn=%s OnNav=%d NavProj=%s"),
				*Pawn->GetActorLocation().ToString(), (int)bOnNav, *NavLoc.Location.ToString());
		}
		else
		{
			UE_LOG(LogTemp, Verbose, TEXT("[enemybugreport][ChaseNav] MissingNavigationSystem"));
		}
	}

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// Tick
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseTarget::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	AAIController* AIController = InstanceData.AIController;
	if (!AIController) return EStateTreeRunStatus::Failed;

	const FHellunaAITargetData& TargetData = InstanceData.TargetData;
	if (!TargetData.HasValidTarget()) return EStateTreeRunStatus::Failed;

	APawn* Pawn = AIController->GetPawn();
	if (!Pawn) return EStateTreeRunStatus::Failed;

	AActor* TargetActor = TargetData.TargetActor.Get();

	// 공격 중이면 이동 정지
	if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn))
	{
		if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
		{
			FGameplayTag AttackingTag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking"), false);
			if (AttackingTag.IsValid() && ASC->HasMatchingGameplayTag(AttackingTag))
			{
				AIController->StopMovement();
				return EStateTreeRunStatus::Running;
			}
		}
	}

	InstanceData.TimeSinceRepath  += DeltaTime;
	InstanceData.TimeUntilNextEQS -= DeltaTime;
	InstanceData.MovementDiagTimer -= DeltaTime;

	const bool bIsSpaceShip = (Cast<AResourceUsingObject_SpaceShip>(TargetActor) != nullptr);

	// ── 2초마다 Tick 상태 덤프 (Verbose — Warning 스팸 방지) ────────────
	static float sDbgTimer = 0.f;
	sDbgTimer += DeltaTime;
	if (sDbgTimer >= 2.f)
	{
		sDbgTimer = 0.f;
		if (UPathFollowingComponent* PFC = AIController->GetPathFollowingComponent())
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[enemybugreport][ChaseTickVerbose] bIsSpaceShip=%d PFC=%d Velocity=%.1f Dist=%.1f SlotIdx=%d SlotArrived=%d"),
				(int)bIsSpaceShip,
				(int)PFC->GetStatus(),
				Pawn->GetVelocity().Size(),
				TargetData.DistanceToTarget,
				InstanceData.AssignedSlotIndex,
				(int)InstanceData.bSlotArrived);
		}
	}

	if (InstanceData.MovementDiagTimer <= 0.f && bIsSpaceShip)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[enemybugreport][ChaseTick] Enemy=%s DistToShip=%.1f Slot=%d SlotArrived=%d SlotLoc=%s PawnLoc=%s Vel=%.1f Stuck=%.2f"),
			*GetNameSafe(Pawn),
			TargetData.DistanceToTarget,
			InstanceData.AssignedSlotIndex,
			(int)InstanceData.bSlotArrived,
			*InstanceData.AssignedSlotLocation.ToCompactString(),
			*Pawn->GetActorLocation().ToCompactString(),
			Pawn->GetVelocity().Size(),
			InstanceData.StuckAccumTime);
		InstanceData.MovementDiagTimer = 0.75f;
	}

	if (bIsSpaceShip)
	{
		// ── 슬롯 기반 우주선 이동 (근거리) vs 자유 이동 (원거리) ────────
		USpaceShipAttackSlotManager* SlotMgr = bUseSlotSystem ? GetSlotManager(TargetActor) : nullptr;

		// 슬롯이 0개면 슬롯매니저가 없는 것과 동일 취급 (NavMesh 미준비 등)
		if (SlotMgr && SlotMgr->GetSlots().Num() == 0)
			SlotMgr = nullptr;

		// 슬롯 시스템 OFF 또는 슬롯 0개 → 분산 이동
		if (!SlotMgr)
		{
			const bool bRepathDue     = InstanceData.TimeSinceRepath >= RepathInterval;
			const bool bTargetChanged = InstanceData.LastMoveTarget != TargetData.TargetActor;

			bool bStuck = false;
			if (UPathFollowingComponent* PFC = AIController->GetPathFollowingComponent())
			{
				const bool bIdle   = (PFC->GetStatus() == EPathFollowingStatus::Idle);
				const bool bPaused = (PFC->GetStatus() == EPathFollowingStatus::Paused);
				const bool bSlow   = (Pawn->GetVelocity().SizeSquared2D() < 30.f * 30.f);
				const bool bFar    = (TargetData.DistanceToTarget > AcceptanceRadius + 150.f);
				bStuck = (bIdle || bPaused) && bSlow && bFar;
			}

			if (bTargetChanged || bRepathDue || bStuck)
			{
				const FVector PawnLoc = Pawn->GetActorLocation();
				const FVector ShipLoc = TargetActor->GetActorLocation();
				const FVector Dir = (ShipLoc - PawnLoc).GetSafeNormal();

				// Stuck이 반복되면 분산 반경을 줄여 더 직접적으로 접근
				const float EffectiveSpread = bStuck ? ShipSpreadRadius * 0.3f : ShipSpreadRadius;
				const FVector Perp = FVector(-Dir.Y, Dir.X, 0.f);
				const float SpreadOffset = FMath::FRandRange(-EffectiveSpread, EffectiveSpread);
				const FVector RawGoal = ShipLoc - Dir * AcceptanceRadius + Perp * SpreadOffset;

				// NavMesh 투영 시도 (확장된 범위)
				UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(AIController->GetWorld());
				FNavLocation NavGoal;
				FVector FinalGoal = RawGoal;
				if (NavSys && NavSys->ProjectPointToNavigation(RawGoal, NavGoal, FVector(300.f, 300.f, 800.f)))
					FinalGoal = NavGoal.Location;

				FAIMoveRequest Req;
				Req.SetGoalLocation(FinalGoal);
				Req.SetAcceptanceRadius(AcceptanceRadius);
				Req.SetReachTestIncludesAgentRadius(false);
				Req.SetUsePathfinding(true);
				Req.SetAllowPartialPath(true);
				Req.SetCanStrafe(true);
				const FPathFollowingRequestResult Result = AIController->MoveTo(Req);

				// MoveTo 실패 시: NavMesh 투영 → 그래도 실패 시 우주선 방향 200cm 직접 이동
				if (Result.Code == EPathFollowingRequestResult::Failed)
				{
					ChaseTargetHelpers::IssueMoveToActorNavProjected(AIController, TargetActor, AcceptanceRadius);

					// 최후 수단: 짧은 거리 직선 이동 (NavMesh 없는 지역 탈출용)
					if (UPathFollowingComponent* PFC = AIController->GetPathFollowingComponent())
					{
						if (PFC->GetStatus() == EPathFollowingStatus::Idle)
						{
							const FVector DirectGoal = PawnLoc + Dir * 200.f;
							ChaseTargetHelpers::IssueMoveToLocation(AIController, DirectGoal, AcceptanceRadius);
						}
					}
				}

				InstanceData.LastMoveTarget  = TargetData.TargetActor;
				InstanceData.TimeSinceRepath = 0.f;
			}
			return EStateTreeRunStatus::Running;
		}

		// ── 슬롯 시스템 ON (근거리) ─────────────────────────────────────

		const float DistToShip = TargetData.DistanceToTarget;
		const float SlotReserveRadius = ChaseTargetHelpers::ComputeSlotReserveRadius(SlotEngageRadius, AcceptanceRadius);

		// 슬롯 미배정 상태
		if (InstanceData.AssignedSlotIndex < 0)
		{
			// 슬롯 예약 반경 밖이면 우주선 방향으로 NavMesh 위 중간 목표 지점으로 이동
			if (DistToShip > SlotReserveRadius)
			{
				const bool bRepathDue = InstanceData.TimeSinceRepath >= RepathInterval;
				bool bIdle = false;
				if (UPathFollowingComponent* PFC = AIController->GetPathFollowingComponent())
					bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);

				if (bRepathDue || bIdle)
				{
					// MoveToActor 대신 우주선 방향 NavMesh 위 지점으로 이동
					// (우주선 메시 내부가 목적지가 되어 경로 실패하는 문제 방지)
					const FVector PawnLoc  = Pawn->GetActorLocation();
					const FVector ShipLoc  = TargetActor->GetActorLocation();
					const FVector Dir      = (ShipLoc - PawnLoc).GetSafeNormal();
					FVector RawGoal  = PawnLoc + Dir * FMath::Min(DistToShip - SlotReserveRadius * 0.8f, 1500.f);

					// 몬스터별 분산: 이동 방향의 수직 방향으로 랜덤 오프셋 추가 (뭉침 방지)
					const FVector Perp = FVector(-Dir.Y, Dir.X, 0.f);
					const float SpreadOffset = FMath::FRandRange(-ShipSpreadRadius, ShipSpreadRadius);
					RawGoal += Perp * SpreadOffset;

					UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(AIController->GetWorld());
					FNavLocation NavGoal;
					FVector FinalGoal = RawGoal;
					if (NavSys && NavSys->ProjectPointToNavigation(RawGoal, NavGoal, FVector(200.f, 200.f, 500.f)))
						FinalGoal = NavGoal.Location;

					UE_LOG(LogTemp, Verbose, TEXT("[enemybugreport][ChaseShipApproachVerbose] Goal=%s Dist=%.1f Repath=%d Idle=%d"),
						*FinalGoal.ToString(), DistToShip, (int)bRepathDue, (int)bIdle);
					UE_LOG(LogTemp, Warning,
						TEXT("[enemybugreport][ChaseShipApproach] Enemy=%s Goal=%s DistToShip=%.1f ReserveRadius=%.1f Repath=%d Idle=%d"),
						*GetNameSafe(Pawn),
						*FinalGoal.ToCompactString(),
						DistToShip,
						SlotReserveRadius,
						(int)bRepathDue,
						(int)bIdle);

					ChaseTargetHelpers::IssueMoveToLocation(AIController, FinalGoal, AcceptanceRadius);
					InstanceData.TimeSinceRepath = 0.f;
				}
				return EStateTreeRunStatus::Running;
			}

			// 진입 범위 안: 슬롯 배정 재시도 (0.5초 쿨다운)
			InstanceData.SlotRetryTimer -= DeltaTime;
			if (InstanceData.SlotRetryTimer <= 0.f)
			{
				InstanceData.SlotRetryTimer = 0.5f;
				int32 SlotIdx = -1;
				FVector SlotLoc;
				if (SlotMgr->RequestSlot(Pawn, SlotIdx, SlotLoc))
				{
					InstanceData.AssignedSlotIndex    = SlotIdx;
					InstanceData.AssignedSlotLocation = SlotLoc;
					InstanceData.bSlotArrived         = false;
					UE_LOG(LogTemp, Warning,
						TEXT("[enemybugreport][SlotAssign] Enemy=%s Slot=%d SlotLoc=%s PawnLoc=%s DistToShip=%.1f"),
						*GetNameSafe(Pawn),
						SlotIdx,
						*SlotLoc.ToCompactString(),
						*Pawn->GetActorLocation().ToCompactString(),
						DistToShip);
					ChaseTargetHelpers::IssueMoveToLocation(AIController, SlotLoc, AcceptanceRadius);
				}
				else
				{
					// 슬롯 없음 → 대기 위치 → NavMesh 투영 → 최후 수단: 직접 우주선 방향으로 이동
					FVector WaitLoc;
					if (SlotMgr->GetWaitingPosition(Pawn, WaitLoc))
					{
						UE_LOG(LogTemp, Warning,
							TEXT("[enemybugreport][SlotWait] Enemy=%s WaitLoc=%s DistToShip=%.1f"),
							*GetNameSafe(Pawn),
							*WaitLoc.ToCompactString(),
							DistToShip);
						ChaseTargetHelpers::IssueMoveToLocation(AIController, WaitLoc, AcceptanceRadius);
					}
					else
					{
						UE_LOG(LogTemp, Warning,
							TEXT("[enemybugreport][SlotWaitFail] Enemy=%s DistToShip=%.1f -> FallbackMove"),
							*GetNameSafe(Pawn),
							DistToShip);
						// NavMesh 투영 이동 시도
						FAIMoveRequest Req;
						Req.SetGoalActor(TargetActor);
						Req.SetAcceptanceRadius(AcceptanceRadius);
						Req.SetUsePathfinding(true);
						Req.SetAllowPartialPath(true);
						const FPathFollowingRequestResult Result = AIController->MoveTo(Req);

						// 경로 탐색 완전 실패 시: 우주선 방향으로 NavMesh 없이 직접 이동
						if (Result.Code == EPathFollowingRequestResult::Failed)
						{
							const FVector ToShip = (TargetActor->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
							const FVector DirectGoal = Pawn->GetActorLocation() + ToShip * 200.f;
							UE_LOG(LogTemp, Warning,
								TEXT("[enemybugreport][SlotFallbackDirect] Enemy=%s DirectGoal=%s DistToShip=%.1f"),
								*GetNameSafe(Pawn),
								*DirectGoal.ToCompactString(),
								DistToShip);
							ChaseTargetHelpers::IssueMoveToLocation(AIController, DirectGoal, AcceptanceRadius);
						}
					}
				}
			}

			// 슬롯 대기 중에도 우주선 방향으로 회전 (공격존 진입 준비)
			if (TargetActor)
			{
				const FVector ToShip = (TargetActor->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
				if (!ToShip.IsNearlyZero())
				{
					const FRotator CurrentRot = Pawn->GetActorRotation();
					const FRotator TargetRot  = FRotator(0.f, ToShip.Rotation().Yaw, 0.f);
					Pawn->SetActorRotation(FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, 10.f));
				}
			}

			return EStateTreeRunStatus::Running;
		}


		// 이미 도착한 경우 정지 유지 + 우주선 방향 회전 (Attack State가 처리)
		if (InstanceData.bSlotArrived)
		{
			AIController->StopMovement();

			if (TargetActor)
			{
				AIController->SetFocus(TargetActor);

				// 우주선 방향으로 빠르게 회전 → 전방 공격존이 우주선을 향하도록
				const FVector ToShip = (TargetActor->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
				if (!ToShip.IsNearlyZero())
				{
					const FRotator CurrentRot = Pawn->GetActorRotation();
					const FRotator TargetRot  = FRotator(0.f, ToShip.Rotation().Yaw, 0.f);
					const FRotator NewRot = FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, 15.f);
					Pawn->SetActorRotation(NewRot);

					if (AController* C = Pawn->GetController())
						C->SetControlRotation(NewRot);

					// 회전 차이가 클 때만 네트워크 업데이트 (매 틱 호출 방지)
					const float YawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentRot.Yaw, TargetRot.Yaw));
					if (YawDelta > 2.f)
						Pawn->ForceNetUpdate();
				}
			}

			return EStateTreeRunStatus::Running;
		}

		// 슬롯은 지형 높이/우주선 배치 때문에 Z 편차가 클 수 있어 XY 기준으로 도착 처리한다.
		const FVector PawnLocation = Pawn->GetActorLocation();
		const float DistToSlot2D = FVector::Dist2D(PawnLocation, InstanceData.AssignedSlotLocation);
		const float DistToSlot3D = FVector::Dist(PawnLocation, InstanceData.AssignedSlotLocation);
		const float SlotHeightDelta = FMath::Abs(PawnLocation.Z - InstanceData.AssignedSlotLocation.Z);
		if (DistToSlot2D <= AcceptanceRadius + 30.f)
		{
			InstanceData.bSlotArrived = true;
			AIController->StopMovement();

			if (SlotMgr)
				SlotMgr->OccupySlot(InstanceData.AssignedSlotIndex, Pawn);

			// 우주선 방향 즉시 바라보기 + 네트워크 동기화
			const FVector ToShip = (TargetActor->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
			if (!ToShip.IsNearlyZero())
			{
				const FRotator FaceRot(0.f, ToShip.Rotation().Yaw, 0.f);
				Pawn->SetActorRotation(FaceRot);
				if (AController* C = Pawn->GetController())
					C->SetControlRotation(FaceRot);
				AIController->SetFocalPoint(Pawn->GetActorLocation() + ToShip * 1000.f);
				Pawn->ForceNetUpdate();
			}

			UE_LOG(LogTemp, Log, TEXT("[enemybugreport][SlotArriveVerbose] Slot=%d Dist2D=%.1f Dist3D=%.1f"),
				InstanceData.AssignedSlotIndex, DistToSlot2D, DistToSlot3D);
			UE_LOG(LogTemp, Warning,
				TEXT("[enemybugreport][SlotArrive] Enemy=%s Slot=%d DistToSlot2D=%.1f DistToSlot3D=%.1f HeightDelta=%.1f PawnRot=%s ControlRot=%s"),
				*GetNameSafe(Pawn),
				InstanceData.AssignedSlotIndex,
				DistToSlot2D,
				DistToSlot3D,
				SlotHeightDelta,
				*Pawn->GetActorRotation().ToCompactString(),
				*AIController->GetControlRotation().ToCompactString());

			return EStateTreeRunStatus::Running;
		}

		// Stuck 감지
		bool bMovingSlow = false;
		bool bIdle       = false;
		if (UPathFollowingComponent* PFC = AIController->GetPathFollowingComponent())
		{
			bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);
			const bool bMoving = (PFC->GetStatus() == EPathFollowingStatus::Moving);
			const bool bSlow   = (Pawn->GetVelocity().SizeSquared2D() < 30.f * 30.f);
			const bool bFar    = (DistToSlot2D > AcceptanceRadius + 150.f);
			bMovingSlow = bMoving && bSlow && bFar;
		}

		if (bMovingSlow)
			InstanceData.StuckAccumTime += DeltaTime;
		else
			InstanceData.StuckAccumTime = FMath::Max(0.f, InstanceData.StuckAccumTime - DeltaTime);

		const bool bStuck   = (InstanceData.StuckAccumTime >= 2.f);
		const bool bNeedMove = bIdle && (DistToSlot2D > AcceptanceRadius + 200.f);

		if (bStuck || bNeedMove)
		{
			// 슬롯 반납 후 재배정
			if (SlotMgr)
			{
				UE_LOG(LogTemp, Verbose, TEXT("[enemybugreport][SlotReassignVerbose] Reason=%s Slot=%d"),
					bStuck ? TEXT("Stuck") : TEXT("Idle"), InstanceData.AssignedSlotIndex);
				UE_LOG(LogTemp, Warning,
					TEXT("[enemybugreport][SlotReassign] Enemy=%s Reason=%s Slot=%d DistToSlot2D=%.1f DistToSlot3D=%.1f HeightDelta=%.1f Vel=%.1f PFCIdle=%d StuckAccum=%.2f"),
					*GetNameSafe(Pawn),
					bStuck ? TEXT("Stuck") : TEXT("Idle"),
					InstanceData.AssignedSlotIndex,
					DistToSlot2D,
					DistToSlot3D,
					SlotHeightDelta,
					Pawn->GetVelocity().Size(),
					(int)bIdle,
					InstanceData.StuckAccumTime);

				SlotMgr->ReleaseSlotByIndex(InstanceData.AssignedSlotIndex);
				InstanceData.AssignedSlotIndex = -1;
				InstanceData.StuckAccumTime    = 0.f;
				InstanceData.SlotRetryTimer    = 0.f; // 즉시 재시도
			}
			else
			{
				// SlotManager 없음 → NavMesh 투영 위치로 재이동
				ChaseTargetHelpers::IssueMoveToActorNavProjected(AIController, TargetActor, AcceptanceRadius);
				InstanceData.StuckAccumTime = 0.f;
			}
		}
	}
	else
	{
		// ── 플레이어 ────────────────────────────────────────────
		const bool bRepathDue = InstanceData.TimeSinceRepath >= RepathInterval;
		const bool bTargetChanged = InstanceData.LastMoveTarget != TargetData.TargetActor;

		bool bStuck = false;
		if (UPathFollowingComponent* PFC = AIController->GetPathFollowingComponent())
		{
			const bool bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);
			const bool bSlow = (Pawn->GetVelocity().SizeSquared2D() < 50.f * 50.f);
			const bool bFar  = (TargetData.DistanceToTarget > (AcceptanceRadius + 150.f));
			bStuck = bIdle && bSlow && bFar;
		}

		if (bTargetChanged || bRepathDue || bStuck)
		{
			if (AttackPositionQuery && InstanceData.TimeUntilNextEQS <= 0.f)
			{
				RunAttackPositionEQS(AttackPositionQuery, AIController, AcceptanceRadius, TargetActor);
				InstanceData.TimeUntilNextEQS = EQSInterval;
			}
			else
			{
				AIController->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);
			}

			InstanceData.LastMoveTarget  = TargetData.TargetActor;
			InstanceData.TimeSinceRepath = 0.f;
		}

		// 플레이어를 향해 회전 + 네트워크 동기화
		const FVector ToTarget = (TargetActor->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
		if (!ToTarget.IsNearlyZero())
		{
			const FRotator CurrentRot = Pawn->GetActorRotation();
			const FRotator TargetRot  = FRotator(0.f, ToTarget.Rotation().Yaw, 0.f);
			const FRotator NewRot = FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, 5.f);
			Pawn->SetActorRotation(NewRot);
			if (AController* C = Pawn->GetController())
				C->SetControlRotation(NewRot);
		}
	}

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// ExitState
// ============================================================================
void FSTTask_ChaseTarget::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// 슬롯 기반 우주선 공격은 AttackState에서 슬롯을 유지해야 하므로 여기서 즉시 반납하지 않는다.
	if (bUseSlotSystem && InstanceData.AssignedSlotIndex >= 0)
	{
		const FHellunaAITargetData& TargetData = InstanceData.TargetData;
		if (TargetData.HasValidTarget())
		{
			const bool bIsSpaceShip = (Cast<AResourceUsingObject_SpaceShip>(TargetData.TargetActor.Get()) != nullptr);
			if (!bIsSpaceShip)
			{
				if (USpaceShipAttackSlotManager* SlotMgr = GetSlotManager(TargetData.TargetActor.Get()))
				{
					if (AAIController* AIC = InstanceData.AIController)
					{
						SlotMgr->ReleaseSlot(AIC->GetPawn());
						UE_LOG(LogTemp, Log, TEXT("[enemybugreport][ChaseExitReleaseSlot] Slot=%d"),
							InstanceData.AssignedSlotIndex);
					}
				}
			}
		}
		InstanceData.AssignedSlotIndex = -1;
		InstanceData.bSlotArrived      = false;
	}

	if (AAIController* AIController = InstanceData.AIController)
		AIController->StopMovement();
}


