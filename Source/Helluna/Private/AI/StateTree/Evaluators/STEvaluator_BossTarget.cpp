/**
 * STEvaluator_BossTarget.cpp
 *
 * 보스 전용 타겟 선택 + 패턴 감시 Evaluator.
 *
 * ─── Tick 흐름 ───────────────────────────────────────────────
 *  1. HP 갱신 → PatternData.HPRatio
 *  2. 타겟 유효성 체크 → 소멸 시 즉시 재탐색
 *  3. RetargetInterval 경과 → 가장 가까운 플레이어로 교체
 *  4. 거리 갱신 → TargetData.DistanceToTarget
 *  5. 패턴 조건 순회: HP > Timer > ProximityTimer
 *  6. 발동 가능한 패턴 → PatternData.PendingPatternIndex에 기록
 *
 * ─── 출력 구조체 ─────────────────────────────────────────────
 *  TargetData  : FHellunaAITargetData  (기존 Chase/Attack/Condition 재활용)
 *  PatternData : FBossPatternData      (패턴 Task 전용)
 *
 * @author 김민우
 */

#include "AI/StateTree/Evaluators/STEvaluator_BossTarget.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"

// ============================================================================
// 헬퍼: 가장 가까운 플레이어 탐색
// ============================================================================
static AActor* FindNearestPlayer(const UWorld* World, const FVector& FromLocation, float& OutDistSq)
{
	AActor* Nearest = nullptr;
	float NearestDistSq = MAX_FLT;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC) continue;

		APawn* PlayerPawn = PC->GetPawn();
		if (!PlayerPawn) continue;

		const float DistSq = FVector::DistSquared(FromLocation, PlayerPawn->GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = PlayerPawn;
		}
	}

	OutDistSq = NearestDistSq;
	return Nearest;
}

// ============================================================================
// TreeStart
// ============================================================================
void FSTEvaluator_BossTarget::TreeStart(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// TargetData 초기화 (FHellunaAITargetData)
	FHellunaAITargetData& TD = Data.TargetData;
	TD.TargetActor      = nullptr;
	TD.TargetType        = EHellunaTargetType::Player;
	TD.DistanceToTarget  = 0.f;
	TD.bTargetingPlayer  = true;
	TD.bPlayerLocked     = false;
	TD.bEnraged          = false;
	TD.PlayerTargetingTime  = 0.f;
	TD.bAttackingSpaceShip  = false;

	// PatternData 초기화 (FBossPatternData)
	FBossPatternData& PD = Data.PatternData;
	PD.HPRatio             = 1.f;
	PD.CombatElapsedTime   = 0.f;
	PD.PendingPatternIndex = -1;
	PD.bPatternActive      = false;

	Data.RetargetTimer       = 0.f;
	Data.bHealthCompSearched = false;
	Data.CachedHealthComp    = nullptr;

	// 패턴 런타임 복사 (에디터 데이터 → 런타임 인스턴스)
	Data.PatternRuntime = PatternEntries;
	for (FBossPatternEntry& Entry : Data.PatternRuntime)
	{
		Entry.bTriggered        = false;
		Entry.CooldownRemaining = 0.f;
		Entry.AccumulatedTime   = 0.f;
	}

	// 초기 타겟 설정
	const UWorld* World = Context.GetWorld();
	if (!World) return;

	const AAIController* AIC = Data.AIController;
	if (!AIC) return;

	const APawn* Pawn = AIC->GetPawn();
	if (!Pawn) return;

	float DistSq = MAX_FLT;
	AActor* Nearest = FindNearestPlayer(World, Pawn->GetActorLocation(), DistSq);
	if (Nearest)
	{
		TD.TargetActor      = Nearest;
		TD.DistanceToTarget = FMath::Sqrt(DistSq);
	}
}

// ============================================================================
// Tick
// ============================================================================
void FSTEvaluator_BossTarget::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	FHellunaAITargetData& TD = Data.TargetData;
	FBossPatternData& PD = Data.PatternData;

	const AAIController* AIC = Data.AIController;
	if (!AIC) return;

	const APawn* BossPawn = AIC->GetPawn();
	if (!BossPawn) return;

	const UWorld* World = Context.GetWorld();
	if (!World) return;

	const FVector BossLocation = BossPawn->GetActorLocation();

	// ── 1. HP 갱신 ──────────────────────────────────────────
	if (!Data.bHealthCompSearched)
	{
		Data.bHealthCompSearched = true;
		if (const AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(BossPawn))
		{
			Data.CachedHealthComp = Enemy->FindComponentByClass<UHellunaHealthComponent>();
		}
	}

	if (UHellunaHealthComponent* HC = Data.CachedHealthComp.Get())
	{
		const float MaxHP = HC->GetMaxHealth();
		PD.HPRatio = (MaxHP > 0.f) ? (HC->GetHealth() / MaxHP) : 0.f;
	}

	// ── 2. 전투 시간 갱신 ───────────────────────────────────
	PD.CombatElapsedTime += DeltaTime;

	// ── 3. 타겟 유효성 + 리타겟 ─────────────────────────────
	Data.RetargetTimer += DeltaTime;

	const bool bTargetLost = !TD.HasValidTarget();
	const bool bRetargetDue = Data.RetargetTimer >= RetargetInterval;

	if (bTargetLost || bRetargetDue)
	{
		float DistSq = MAX_FLT;
		AActor* Nearest = FindNearestPlayer(World, BossLocation, DistSq);
		if (Nearest)
		{
			TD.TargetActor      = Nearest;
			TD.DistanceToTarget = FMath::Sqrt(DistSq);
		}
		Data.RetargetTimer = 0.f;
	}
	else if (TD.HasValidTarget())
	{
		TD.DistanceToTarget = FVector::Dist(BossLocation, TD.TargetActor->GetActorLocation());
	}

	// ── 4. 패턴 실행 중이면 조건 검사 스킵 ─────────────────
	if (PD.bPatternActive)
		return;

	// ── 5. 패턴 조건 순회 ───────────────────────────────────
	// 쿨다운 감소
	for (FBossPatternEntry& Entry : Data.PatternRuntime)
	{
		if (Entry.CooldownRemaining > 0.f)
			Entry.CooldownRemaining -= DeltaTime;
	}

	// 우선순위: HP > Timer > ProximityTimer
	int32 BestIndex = -1;
	int32 BestPriority = 999;

	for (int32 i = 0; i < Data.PatternRuntime.Num(); ++i)
	{
		FBossPatternEntry& Entry = Data.PatternRuntime[i];

		// 쿨다운 중이면 스킵
		if (Entry.CooldownRemaining > 0.f) continue;

		// 1회성이면서 이미 발동했으면 스킵
		if (Entry.Cooldown <= 0.f && Entry.bTriggered) continue;

		int32 Priority = 999;
		bool bConditionMet = false;

		switch (Entry.TriggerType)
		{
		case EBossPatternTriggerType::HPThreshold:
			Priority = 0;
			bConditionMet = (PD.HPRatio <= Entry.HPThreshold);
			break;

		case EBossPatternTriggerType::Timer:
			Priority = 1;
			Entry.AccumulatedTime += DeltaTime;
			bConditionMet = (Entry.AccumulatedTime >= Entry.TriggerTime);
			break;

		case EBossPatternTriggerType::ProximityTimer:
			Priority = 2;
			{
				float MinDSq = MAX_FLT;
				for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
				{
					if (APlayerController* PC = It->Get())
					{
						if (APawn* PP = PC->GetPawn())
						{
							const float DSq = FVector::DistSquared(BossLocation, PP->GetActorLocation());
							if (DSq < MinDSq) MinDSq = DSq;
						}
					}
				}
				if (MinDSq <= Entry.ProximityRange * Entry.ProximityRange)
					Entry.AccumulatedTime += DeltaTime;
				else
					Entry.AccumulatedTime = FMath::Max(0.f, Entry.AccumulatedTime - DeltaTime * 0.5f);
			}
			bConditionMet = (Entry.AccumulatedTime >= Entry.TriggerTime);
			break;
		}

		if (bConditionMet && Priority < BestPriority)
		{
			BestPriority = Priority;
			BestIndex = i;
		}
	}

	if (BestIndex >= 0)
	{
		FBossPatternEntry& Triggered = Data.PatternRuntime[BestIndex];
		PD.PendingPatternIndex = BestIndex;
		Triggered.bTriggered = true;
		Triggered.AccumulatedTime = 0.f;
		Triggered.CooldownRemaining = Triggered.Cooldown;
	}
	else
	{
		PD.PendingPatternIndex = -1;
	}
}
