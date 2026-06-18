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
#include "AI/HellunaAIAttackZone.h" // [SurfaceDistanceV1] 우주선 표면 거리
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Character/HellunaEnemyCharacter.h" // [PlayerOnlyHunterV1] bPlayerOnlyTarget 읽기

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

	// [PlayerOnlyHunterV1] 플레이어 전용 몹은 우주선 기본 타겟을 잡지 않는다(Tick 이 최근접 플레이어 설정).
	if (const AAIController* AIC = InstanceData.AIController)
	{
		if (const AHellunaEnemyCharacter* EC = Cast<AHellunaEnemyCharacter>(AIC->GetPawn()))
		{
			if (EC->bPlayerOnlyTarget)
			{
				return;
			}
		}
	}

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

	// [PlayerOnlyHunterV1] 플레이어 전용 헌터: 거리 무관 최근접 플레이어만 추격. 우주선/터렛/광폭화 전부 무시.
	if (const AHellunaEnemyCharacter* HunterChar = Cast<AHellunaEnemyCharacter>(ControlledPawn))
	{
		if (HunterChar->bPlayerOnlyTarget)
		{
			AActor* NearestPlayer = nullptr;
			float NearestDistSq = MAX_FLT;
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

			TargetData.bAttackingSpaceShip = false;
			TargetData.bEnraged            = false;
			TargetData.bPlayerLocked       = false;

			if (NearestPlayer)
			{
				TargetData.TargetActor      = NearestPlayer;
				TargetData.TargetType       = EHellunaTargetType::Player;
				TargetData.bTargetingPlayer = true;
				TargetData.DistanceToTarget = FMath::Sqrt(NearestDistSq);
				const_cast<AAIController*>(AIController)->SetFocus(NearestPlayer);
			}
			else
			{
				TargetData.TargetActor      = nullptr;
				TargetData.bTargetingPlayer = false;
				const_cast<AAIController*>(AIController)->ClearFocus(EAIFocusPriority::Gameplay);
			}
			return;
		}
	}

	// [AggroRangeTestV1] 임시 테스트 override — 추격(어그로) 거리·포기시간 하한 강제.
	//   StateTree 의 AggroRange(800)/EnrageDelay(3) 가 너무 작아 "공격범위 근처에서만 전환"되는 문제 →
	//   멀리서도 플레이어를 추격하고, 추격 중 너무 빨리 광폭화로 포기하지 않게 함.
	//   정식 값은 STTree_Melee_V5 evaluator 파라미터(GUI)에서 조정 — 테스트 끝나면 이 override 제거.
	const float EffAggroRange = FMath::Max(AggroRange, 1500.f);
	const float EffEnrageDelay = FMath::Max(EnrageDelay, 8.f);
	static bool bLoggedAggroOverride = false;
	if (!bLoggedAggroOverride)
	{
		bLoggedAggroOverride = true;
		UE_LOG(LogTemp, Warning,
			TEXT("[AggroRangeTestV1] 추격범위 override 적용 — AggroRange %.0f→%.0f, EnrageDelay %.1f→%.1f"),
			AggroRange, EffAggroRange, EnrageDelay, EffEnrageDelay);
	}

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
			// [SurfaceDistanceV1] 우주선은 표면 거리 (광폭화 후 우주선 고정 상태)
			TargetData.DistanceToTarget = HellunaAI::GetTargetSurfaceDistance(
				PawnLocation, TargetData.TargetActor.Get());
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

		// 어그로 범위 이탈 → 우주선 복귀 ([AggroRangeTestV1] override 적용)
		if (DistToTarget > EffAggroRange)
		{
			ResetToSpaceShip(TargetData, AIController, World, ControlledPawn);
			return;
		}

		// 플레이어 타겟일 때만 광폭화 타이머 누적 (터렛은 고정물이므로 카이팅 불가)
		if (TargetData.TargetType == EHellunaTargetType::Player)
		{
			TargetData.PlayerTargetingTime += DeltaTime;

			// 광폭화 발동
			if (TargetData.PlayerTargetingTime >= EffEnrageDelay)
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

	// 우주선까지 거리 갱신 — [SurfaceDistanceV1] 표면 거리.
	//   비원형(길쭉한) 우주선에서 중심점 거리로 판정하면 표면이 멀리 튀어나온 쪽으로 접근하는
	//   원거리 몹이 사거리 안에 들어왔다고 착각해 과하게 가까이 붙는 문제를 막는다.
	if (TargetData.TargetActor.IsValid())
	{
		TargetData.DistanceToTarget = HellunaAI::GetTargetSurfaceDistance(
			PawnLocation, TargetData.TargetActor.Get());
	}

	// 우주선 공격 범위 내 → 어그로 전환 차단 (거리 기반 유지 — 어그로 전환 차단용 게이트).
	// Attack 발동 자체는 StateTree transition의 InAttackZone Condition이 담당하므로
	// 여기서는 "공격 사거리 근처면 플레이어가 지나가도 타겟 안 바꿈" 정도의 부드러운 게이트로 충분.
	if (TargetData.TargetActor.IsValid() && TargetData.DistanceToTarget <= SpaceShipAttackRange)
	{
		TargetData.bAttackingSpaceShip = true;

		// [SwitchToPlayerNearShipV1] 우주선 공격 사거리 안이어도 플레이어 어그로 스캔을 막지 않는다.
		//   기존엔 여기서 (OnShip 아닌 경우) return 하여 근접 몹이 배에 붙으면 플레이어로 절대 전환되지
		//   않았다 — 근접만 해당: SpaceShipAttackRange(500) 안으로 들어가기 때문. 원거리(1000)는 멀리서
		//   쏘느라 이 게이트 밖이라 이미 전환됐었다.
		//   사용자 요구: "우주선을 공격/접근 중에도 플레이어를 보면 플레이어로 전환".
		//   → return 제거하고 아래 플레이어/터렛 스캔으로 계속 진행. (플레이어가 AggroRange 안에 있어야
		//     전환. 전환 후 EnrageDelay 경과 시 광폭화로 다시 우주선 고정되어 무한 카이팅은 방지된다.)
		//   bAttackingSpaceShip 플래그는 STTask_ShipJump 가 점프 게이트로 읽으므로 위에서 계속 설정한다.
		//   (이전 OnShipEnrageV1 의 OnShip 전용 예외도 이 일반화에 포함됨.)
	}
	else
	{
		TargetData.bAttackingSpaceShip = false;
	}

	// ── 어그로 범위 내 가장 가까운 플레이어/터렛 탐색 ──────────
	AActor* NearestAggroTarget = nullptr;
	EHellunaTargetType NearestType = EHellunaTargetType::SpaceShip;
	float NearestDistSq = EffAggroRange * EffAggroRange; // [AggroRangeTestV1] override

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

	// Verbose — 매 Evaluator Tick 출력이라 렉 유발. 필요 시 LogTemp Verbose 로 활성.
	UE_LOG(LogTemp, Verbose,
		TEXT("[TurretAggroV2] Pawn=%s | TurretSeen=%d InRange=%d BlockedByCap=%d | Picked=%s | bAttackingShip=%d"),
		*ControlledPawn->GetName(), TurretSeen, TurretInRange, TurretBlockedByCap,
		NearestAggroTarget ? *NearestAggroTarget->GetName() : TEXT("none"),
		TargetData.bAttackingSpaceShip ? 1 : 0);

	// [SwitchDeferWhileAttackingV1] 현재 공격 모션 중이면 타겟/포커스 전환을 미룬다.
	//   공격 도중 SetFocus(player) 로 즉시 플레이어를 바라보면 "그 자리에서 플레이어를 보며 한 번
	//   어색하게 공격하고 추격"하는 버그가 된다. 공격이 끝난 뒤(Attacking 태그 해제) 다음 틱에 전환 →
	//   "공격을 마저 끝낸 뒤 플레이어를 바라보고 추격". (공격 중엔 기존 타겟=우주선을 계속 주시.)
	bool bPawnIsAttacking = false;
	{
		static const FGameplayTag AttackingTag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking"), false);
		if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(const_cast<APawn*>(ControlledPawn)))
		{
			if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
			{
				bPawnIsAttacking = AttackingTag.IsValid() && ASC->HasMatchingGameplayTag(AttackingTag);
			}
		}
	}

	// 어그로 대상 발견 → 타겟 전환 (단, 공격 모션 중엔 미뤄 어색한 중간 회전 방지)
	if (NearestAggroTarget && !bPawnIsAttacking)
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
