/**
 * STEvaluator_TargetSelector.cpp
 *
 * 타겟 선택 + 어그로 관리 + 광폭화 감시.
 *
 * ─── 행동 로직 요약 ───────────────────────────────────────────
 *  1. 기본: 우주선을 향해 이동
 *  2. 어그로 범위 내 플레이어/터렛 → 타겟 전환 (가장 가까운 대상)
 *  3. 타겟 고정: 어그로 범위 밖으로 나가기 전까지 유지
 *  4. 어그로 범위 이탈 → 우주선 복귀
 *  5. 플레이어 타겟 중 EnrageDelay 경과 → 광폭화 이벤트
 *  6. 광폭화 → 우주선 영구 고정
 *  7. 우주선 공격 범위 내 → 어그로 전환 차단
 *  8. 포탑당 어그로 몬스터 수 제한 (MaxMonstersPerTurret)
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
#include "Object/ResourceUsingObject/ResourceUsingObject_AttackTurret.h"
#include "Object/ResourceUsingObject/HellunaTurretBase.h"
#include "AI/TurretAggroTracker.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"

// ============================================================================
// 헬퍼: 우주선 캐시 가져오기
// ============================================================================
static AActor* GetCachedSpaceShip(const UWorld* World)
{
	static TWeakObjectPtr<AActor> CachedSpaceShip;
	if (!CachedSpaceShip.IsValid() && World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(FName("SpaceShip")))
			{
				CachedSpaceShip = *It;
				break;
			}
		}
	}
	return CachedSpaceShip.Get();
}

// ============================================================================
// 헬퍼: 우주선으로 복귀 처리
// ============================================================================
static void ResetToSpaceShip(FHellunaAITargetData& TargetData, const AAIController* AIController, const UWorld* World, const APawn* Pawn)
{
	// 터렛 타겟이었으면 어그로 해제
	if (TargetData.TargetType == EHellunaTargetType::Turret && Pawn)
	{
		FTurretAggroTracker::UnregisterMonster(Pawn);
	}

	if (AIController)
	{
		const_cast<AAIController*>(AIController)->ClearFocus(EAIFocusPriority::Gameplay);
	}

	TargetData.bTargetingPlayer    = false;
	TargetData.bPlayerLocked       = false;
	TargetData.PlayerTargetingTime = 0.f;
	TargetData.TargetType          = EHellunaTargetType::SpaceShip;

	AActor* Ship = GetCachedSpaceShip(World);
	TargetData.TargetActor = Ship;
}

// ============================================================================
// TreeStart — 우주선을 기본 타겟으로 초기화
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
	TargetData.bAttackingSpaceShip = false;
	TargetData.PlayerTargetingTime = 0.f;
	TargetData.DistanceToTarget    = 0.f;

	const UWorld* World = Context.GetWorld();
	AActor* Ship = GetCachedSpaceShip(World);
	if (Ship)
	{
		TargetData.TargetActor = Ship;
		TargetData.TargetType  = EHellunaTargetType::SpaceShip;
	}
}

// ============================================================================
// Tick — 타겟 선택 + 어그로 이탈 + 광폭화 감시
// ============================================================================
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

	// ════════════════════════════════════════════════════════════
	// 광폭화 완료: 우주선 고정, 타겟 선택 전체 차단
	// ════════════════════════════════════════════════════════════
	if (TargetData.bEnraged)
	{
		// [EnrageTargetLockV1] 광폭화 진입 순간의 TargetActor 는 플레이어(광폭 트리거 조건 자체가 "플레이어 장시간 추적").
		// Why: 이 지점에서 우주선으로 스냅하지 않으면 Run_Rage / chase 계열 태스크가 플레이어를 계속 쫓음.
		// How to apply: 광폭 진입 후 첫 틱에 한 번만 우주선으로 전환, 이후 틱은 if 분기 skip.
		if (TargetData.TargetType != EHellunaTargetType::SpaceShip)
		{
			if (AActor* Ship = GetCachedSpaceShip(World))
			{
				FTurretAggroTracker::UnregisterMonster(const_cast<APawn*>(ControlledPawn));
				const_cast<AAIController*>(AIController)->ClearFocus(EAIFocusPriority::Gameplay);

				TargetData.TargetActor      = Ship;
				TargetData.TargetType       = EHellunaTargetType::SpaceShip;
				TargetData.bTargetingPlayer = false;
				TargetData.bPlayerLocked    = false;

				UE_LOG(LogTemp, Warning,
					TEXT("[EnrageTargetLockV1] %s 광폭화 — 타겟을 우주선(%s)으로 스냅"),
					*ControlledPawn->GetName(), *Ship->GetName());
			}
		}

		if (TargetData.TargetActor.IsValid())
		{
			TargetData.DistanceToTarget = FVector::Dist(PawnLocation, TargetData.TargetActor->GetActorLocation());
		}
		return;
	}

	// ════════════════════════════════════════════════════════════
	// 플레이어/터렛 타겟 중: 어그로 이탈 체크 + 광폭화 타이머
	// ════════════════════════════════════════════════════════════
	if (TargetData.bTargetingPlayer)
	{
		// 타겟이 소멸됨 → 우주선 복귀
		if (!TargetData.TargetActor.IsValid())
		{
			ResetToSpaceShip(TargetData, AIController, World, ControlledPawn);
			return;
		}

		const float DistToTarget = FVector::Dist(PawnLocation, TargetData.TargetActor->GetActorLocation());
		TargetData.DistanceToTarget = DistToTarget;

		// 어그로 범위 이탈 → 우주선 복귀
		if (DistToTarget > AggroRange)
		{
			ResetToSpaceShip(TargetData, AIController, World, ControlledPawn);
			return;
		}

		// 플레이어 타겟일 때만 광폭화 타이머 누적 (터렛은 고정물이므로 카이팅 불가)
		if (TargetData.TargetType == EHellunaTargetType::Player)
		{
			TargetData.PlayerTargetingTime += DeltaTime;

			// 광폭화 발동
			if (TargetData.PlayerTargetingTime >= EnrageDelay)
			{
				// 터렛 어그로 해제 (플레이어였으니 해당 없지만 안전 처리)
				FTurretAggroTracker::UnregisterMonster(ControlledPawn);

				if (UStateTreeComponent* STComp = const_cast<AAIController*>(AIController)
					->FindComponentByClass<UStateTreeComponent>())
				{
					FStateTreeEvent EnrageEvent;
					EnrageEvent.Tag = HellunaGameplayTags::Enemy_Event_Enrage;
					STComp->SendStateTreeEvent(EnrageEvent);

					TargetData.bEnraged      = true;
					TargetData.bPlayerLocked = true;
				}
			}
		}
		return;
	}

	// ════════════════════════════════════════════════════════════
	// 우주선 타겟 중: 어그로 스캔
	// ════════════════════════════════════════════════════════════

	// 우주선까지 거리 갱신
	if (TargetData.TargetActor.IsValid())
	{
		TargetData.DistanceToTarget = FVector::Dist(PawnLocation, TargetData.TargetActor->GetActorLocation());
	}

	// 우주선 공격 범위 내 → 어그로 전환 차단 (거리 기반 유지 — 어그로 전환 차단용 게이트).
	// Attack 발동 자체는 StateTree transition의 InAttackZone Condition이 담당하므로
	// 여기서는 "공격 사거리 근처면 플레이어가 지나가도 타겟 안 바꿈" 정도의 부드러운 게이트로 충분.
	if (TargetData.TargetActor.IsValid() && TargetData.DistanceToTarget <= SpaceShipAttackRange)
	{
		TargetData.bAttackingSpaceShip = true;

		// [OnShipEnrageV1] 우주선 위(OnShip 태그 보유)에 올라가 있으면 근거리에도 플레이어 어그로 스캔을
		// 막지 않음. 그래야 우주선 위에 있는 동안 다가오는 플레이어를 타겟으로 삼고 Enrage 타이머가
		// 누적되어 Enrage 이벤트가 발송됨.
		// Why: Attackplayer_JumpStrike 처럼 OnShip 조건으로만 진입하는 상태에서 사용자가
		//      가까이 다가가도 광폭화가 안 되는 문제를 해결.
		bool bOnShip = false;
		if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(ControlledPawn))
		{
			if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
			{
				const FGameplayTag OnShipTag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.OnShip"), false);
				if (OnShipTag.IsValid() && ASC->HasMatchingGameplayTag(OnShipTag))
				{
					bOnShip = true;
				}
			}
		}
		if (!bOnShip)
		{
			return;  // 지상 + 우주선 공격 근거리 → 기존 동작대로 어그로 전환 차단
		}
		// OnShip 이면 아래 플레이어 스캔 로직 계속 → 플레이어 타겟 전환 → PlayerTargetingTime 누적 → Enrage
	}
	else
	{
		TargetData.bAttackingSpaceShip = false;
	}

	// ── 어그로 범위 내 가장 가까운 플레이어/터렛 탐색 ──────────
	AActor* NearestAggroTarget = nullptr;
	EHellunaTargetType NearestType = EHellunaTargetType::SpaceShip;
	float NearestDistSq = AggroRange * AggroRange;

	// 플레이어 탐색
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
			NearestAggroTarget = PlayerPawn;
			NearestType = EHellunaTargetType::Player;
		}
	}

	// 터렛 탐색 (어그로 수 제한 적용)
	// 공격/회복 포탑 모두 어그로 — 공통 부모 AHellunaTurretBase로 일괄 검색.
	int32 TurretSeen = 0, TurretInRange = 0, TurretBlockedByCap = 0;
	for (TActorIterator<AHellunaTurretBase> It(World); It; ++It)
	{
		AHellunaTurretBase* Turret = *It;
		if (!Turret || Turret->IsActorBeingDestroyed()) continue;
		++TurretSeen;

		const float DistSq = FVector::DistSquared(PawnLocation, Turret->GetActorLocation());
		const float AggroSq = AggroRange * AggroRange;
		if (DistSq < AggroSq)
		{
			++TurretInRange;
		}
		if (DistSq < NearestDistSq)
		{
			// 어그로 수 제한 체크
			if (!FTurretAggroTracker::CanAggro(Turret, MaxMonstersPerTurret))
			{
				++TurretBlockedByCap;
				continue;
			}

			NearestDistSq = DistSq;
			NearestAggroTarget = Turret;
			NearestType = EHellunaTargetType::Turret;
		}
	}

	// 진단 로그 — 포탑이 보이는데도 어그로 안 잡히는 원인 추적용.
	// 마커 [TurretAggroV1]가 로그에 안 나오면 라이브 코딩 미적용.
	UE_LOG(LogTemp, Warning,
		TEXT("[TurretAggroV2] Pawn=%s | TurretSeen=%d InRange=%d BlockedByCap=%d | Picked=%s | bAttackingShip=%d"),
		*ControlledPawn->GetName(), TurretSeen, TurretInRange, TurretBlockedByCap,
		NearestAggroTarget ? *NearestAggroTarget->GetName() : TEXT("none"),
		TargetData.bAttackingSpaceShip ? 1 : 0);

	// 어그로 대상 발견 → 타겟 전환
	if (NearestAggroTarget)
	{
		TargetData.TargetActor         = NearestAggroTarget;
		TargetData.TargetType          = NearestType;
		TargetData.DistanceToTarget    = FMath::Sqrt(NearestDistSq);
		TargetData.PlayerTargetingTime = 0.f;
		TargetData.bTargetingPlayer    = true;

		// 터렛이면 어그로 등록
		if (NearestType == EHellunaTargetType::Turret)
		{
			FTurretAggroTracker::Register(NearestAggroTarget, ControlledPawn);
		}

		const_cast<AAIController*>(AIController)->SetFocus(NearestAggroTarget);
	}
}
