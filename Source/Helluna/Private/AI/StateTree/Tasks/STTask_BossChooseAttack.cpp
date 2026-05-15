/**
 * STTask_BossChooseAttack.cpp
 *
 * 보스 전용 거리 기반 공격 선택 Task.
 *
 * @author 김민우
 */

#include "AI/StateTree/Tasks/STTask_BossChooseAttack.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaEnemyCharacter_Boss.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_Attack.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_DashAttack.h"

namespace BossChooseAttackHelpers
{
static void FaceTarget(AAIController* AIC, APawn* Pawn, AActor* Target, float DeltaTime, float Speed)
{
	if (!AIC || !Pawn || !Target) return;

	const FVector ToTarget = (Target->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero()) return;

	const FRotator CurrentRot = AIC->GetControlRotation();
	const FRotator TargetRot(0.f, ToTarget.Rotation().Yaw, 0.f);

	const float YawDeltaDeg = FMath::Abs(FRotator::NormalizeAxis(TargetRot.Yaw - CurrentRot.Yaw));
	FRotator NewRot;
	if (YawDeltaDeg < 1.0f)
	{
		NewRot = TargetRot;
	}
	else
	{
		NewRot = FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, Speed);
	}
	AIC->SetControlRotation(NewRot);
	Pawn->SetActorRotation(NewRot);
}
} // namespace BossChooseAttackHelpers

// ============================================================================
// EnterState
// ============================================================================
EStateTreeRunStatus FSTTask_BossChooseAttack::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.AIController)
		return EStateTreeRunStatus::Failed;

	Data.AIController->StopMovement();

	AActor* PrevFocus = Data.AIController->GetFocusActor();
	Data.AIController->ClearFocus(EAIFocusPriority::Gameplay);

	APawn* Pawn = Data.AIController->GetPawn();
	if (UCharacterMovementComponent* CMC = Pawn ? Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent()) : nullptr)
	{
		CMC->bOrientRotationToMovement     = false;
		CMC->bUseControllerDesiredRotation = true;
	}

	// [BossAttackCooldownPersistV1] Per-entry 쿨다운은 Enemy 캐릭터의 BossAttackCooldowns 맵에 저장되어
	//   Attack 상태 Exit/Re-enter 에도 살아남는다. 여기서는 리셋하지 않음.
	//   - Data.PerEntryCooldowns (legacy) 는 ActiveInstanceData 특성상 매 enter 마다 초기화되어 버그 원인이었음.
	// InitialDelay 는 글로벌 쿨에만 영향 (최초 진입에만 강제).
	if (Data.bFirstEnter)
	{
		Data.CooldownRemaining   = InitialDelay;
		Data.NextSequentialIndex = 0;
		Data.LastSelectedIndex   = -1;
		Data.SameAttackStreak    = 0;
		Data.PerEntryCooldowns.Reset();
		Data.bFirstEnter         = false;
	}
	else
	{
		// 재진입 시 이미 걸린 글로벌 쿨타임은 유지. InitialDelay는 하한선으로만 적용.
		Data.CooldownRemaining = FMath::Max(Data.CooldownRemaining, InitialDelay);
	}

	// Per-entry 쿨다운 배열 크기 동기화 (legacy — 아래에서 Enemy 맵을 우선 사용)
	if (Data.PerEntryCooldowns.Num() != AttackPool.Num())
	{
		Data.PerEntryCooldowns.SetNumZeroed(AttackPool.Num());
	}

	// Suppress unused variable warning (was used by removed Enter log)
	(void)PrevFocus;

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// Tick
// ============================================================================
EStateTreeRunStatus FSTTask_BossChooseAttack::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	AAIController* AIC = Data.AIController;
	if (!AIC) return EStateTreeRunStatus::Failed;

	APawn* Pawn = AIC->GetPawn();
	if (!Pawn) return EStateTreeRunStatus::Failed;

	// STTask_BossAttack과 동일: 매 틱 Focus 해제 + CMC 플래그 유지.
	AIC->ClearFocus(EAIFocusPriority::Gameplay);

	if (UCharacterMovementComponent* CMC = Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent()))
	{
		CMC->bOrientRotationToMovement     = false;
		CMC->bUseControllerDesiredRotation = true;
	}

	AHellunaEnemyCharacter_Boss* Enemy = Cast<AHellunaEnemyCharacter_Boss>(Pawn);
	if (!Enemy) return EStateTreeRunStatus::Failed;

	UHellunaAbilitySystemComponent* ASC = Cast<UHellunaAbilitySystemComponent>(
		Enemy->GetAbilitySystemComponent());
	if (!ASC) return EStateTreeRunStatus::Failed;

	const FHellunaAITargetData& TD = Data.TargetData;
	const FBossPatternData&     PD = Data.PatternData;

	// 패턴 실행 중이면 공격 중단
	if (PD.bPatternActive)
		return EStateTreeRunStatus::Running;

	// 풀이 비어있으면 즉시 대기 (설정 실수 방지)
	if (AttackPool.Num() == 0)
		return EStateTreeRunStatus::Running;

	// [DashDirLockV2] 대쉬 GA 활성 중이면 회전 스킵 — 준비/돌진 동안 방향 고정 (사용자 요구).
	// DashAttack 가 EndAbility 호출 직전 자체적으로 플레이어 face 함 ([DashEndV1] 로그).
	bool bDashActive = false;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.IsActive() && Spec.Ability && Spec.Ability->IsA<UEnemyGameplayAbility_DashAttack>())
		{
			bDashActive = true;
			break;
		}
	}

	// 타겟 있으면 무조건 회전 (몬타지 재생 중이어도) — 단 대쉬 중엔 스킵
	if (TD.HasValidTarget() && !bDashActive)
	{
		BossChooseAttackHelpers::FaceTarget(AIC, Pawn, TD.TargetActor.Get(), DeltaTime, RotationSpeed);
	}
	else if (bDashActive)
	{
		static int32 s_DashSkipLogCount = 0;
		if ((++s_DashSkipLogCount % 30) == 1)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[DashDirLockV2] BossChooseAttack — DashAttack GA active, rotation skipped"));
		}
	}

	// 활성 GA가 있으면 대기 (AttackRecoveryDelay 포함)
	// [ParallelPatternV1] bParallelToOtherAttacks=true 엔트리는 active 여도 block 하지 않음
	// → 오래 지속되는 소환 패턴(예: 시간 왜곡 존)이 돌아가는 동안에도 근접/원거리 공격 계속 가능.
	bool bAnyGAActive = false;
	for (const FBossAttackEntry& Entry : AttackPool)
	{
		if (!Entry.AttackAbility.Get()) continue;
		if (Entry.bParallelToOtherAttacks) continue; // 병행 허용 → 블록 대상 아님
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (Spec.Ability && Spec.Ability->GetClass() == Entry.AttackAbility && Spec.IsActive())
			{
				bAnyGAActive = true;
				break;
			}
		}
		if (bAnyGAActive) break;
	}
	if (bAnyGAActive)
		return EStateTreeRunStatus::Running;

	// 글로벌 쿨다운 대기 + per-entry 쿨다운 일괄 감소
	const bool bGlobalCooldown = Data.CooldownRemaining > 0.f;
	if (bGlobalCooldown)
	{
		Data.CooldownRemaining -= DeltaTime;
	}
	// [BossAttackCooldownPersistV1] Enemy 캐릭터의 맵을 기준으로 decrement.
	// 0 이하가 되면 맵에서 제거 (빈 맵일 때는 아무 비용 없음).
	{
		TArray<TSubclassOf<UHellunaEnemyGameplayAbility>, TInlineAllocator<8>> KeysToRemove;
		for (TPair<TSubclassOf<UHellunaEnemyGameplayAbility>, float>& Pair : Enemy->BossAttackCooldowns)
		{
			Pair.Value -= DeltaTime;
			if (Pair.Value <= 0.f)
			{
				KeysToRemove.Add(Pair.Key);
			}
		}
		for (const TSubclassOf<UHellunaEnemyGameplayAbility>& K : KeysToRemove)
		{
			Enemy->BossAttackCooldowns.Remove(K);
		}
	}
	// legacy 배열도 동기화 (BP / 디버그 뷰 용도 — 필터링엔 쓰지 않음)
	for (int32 i = 0; i < Data.PerEntryCooldowns.Num(); ++i)
	{
		if (Data.PerEntryCooldowns[i] > 0.f)
		{
			Data.PerEntryCooldowns[i] = FMath::Max(0.f, Data.PerEntryCooldowns[i] - DeltaTime);
		}
	}
	if (bGlobalCooldown)
	{
		// [ChaseFallbackV1] 글로벌 쿨다운 대기 중에도 플레이어로 접근.
		if (bChaseFallbackWhenIdle && TD.HasValidTarget())
		{
			AActor* TargetActor = TD.TargetActor.Get();
			if (TargetActor && TD.DistanceToTarget > ChaseFallbackStopRange)
			{
				const FVector Dir = (TargetActor->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
				if (!Dir.IsNearlyZero())
				{
					Pawn->AddMovementInput(Dir, 1.0f);
				}
			}
		}
		return EStateTreeRunStatus::Running;
	}

	// 타겟 검증
	if (!TD.HasValidTarget())
		return EStateTreeRunStatus::Running;

	const float Dist = TD.DistanceToTarget;

	// Per-entry 배열 크기 동기화 (풀이 수정됐을 수 있음 — 런타임 방어)
	if (Data.PerEntryCooldowns.Num() != AttackPool.Num())
	{
		Data.PerEntryCooldowns.SetNumZeroed(AttackPool.Num());
	}

	// ── 후보 필터링 ───────────────────────────────────────────────────
	// 거리 범위 + per-entry 쿨다운 + 연속 상한 + HP 비율 기준으로 사용 가능한 엔트리만 수집.
	// [BossWalkPriorityV1] GA 후보와 Walk 후보를 분리해 우선순위를 적용한다.

	// [HpRatioFilterV1] 보스 현재 HP 비율 (0.0~1.0). HealthComponent 없으면 1.0 으로 간주.
	float BossHpRatio = 1.f;
	if (UHellunaHealthComponent* HPComp = Enemy->FindComponentByClass<UHellunaHealthComponent>())
	{
		BossHpRatio = HPComp->GetHealthNormalized();
	}

	TArray<int32> AbilityCandidates;
	TArray<int32> WalkCandidates;
	AbilityCandidates.Reserve(AttackPool.Num());
	WalkCandidates.Reserve(AttackPool.Num());
	for (int32 i = 0; i < AttackPool.Num(); ++i)
	{
		const FBossAttackEntry& E = AttackPool[i];
		if (Dist < E.MinRange || Dist > E.MaxRange) continue;

		// [HpRatioFilterV1] HP 비율 조건 체크 — 엔트리 범위 밖이면 제외.
		// 기본값 Min=0, Max=1 이면 항상 통과 (HP 무관). GA_Boss_Time 처럼 특정 HP 구간 전용은
		// Max=0.5 등으로 설정하면 HP 50% 이하에서만 후보가 됨.
		if (BossHpRatio < E.HpRatioMin || BossHpRatio > E.HpRatioMax)
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[HpRatioFilterV1] entry #%d (%s) skipped — hp=%.2f not in [%.2f..%.2f]"),
				i, *E.DebugTag.ToString(), BossHpRatio, E.HpRatioMin, E.HpRatioMax);
			continue;
		}

		// [BossPhaseGateV1] 페이즈 게이트 — Phase1/Phase2 한정 엔트리 필터.
		//   bPhase1Only=true 면 보스가 Phase2 진입 후 자동 제외.
		//   bPhase2Only=true 면 Phase1 동안 자동 제외.
		//   둘 다 false 면 페이즈 무관 (기존 동작).
		if (E.bPhase1Only || E.bPhase2Only)
		{
			const bool bIsPhase2 = Enemy->bInPhase2;
			if (E.bPhase1Only && bIsPhase2)
			{
				UE_LOG(LogTemp, Verbose,
					TEXT("[BossPhaseGateV1] entry #%d (%s) skipped — Phase1Only but boss in Phase2"),
					i, *E.DebugTag.ToString());
				continue;
			}
			if (E.bPhase2Only && !bIsPhase2)
			{
				UE_LOG(LogTemp, Verbose,
					TEXT("[BossPhaseGateV1] entry #%d (%s) skipped — Phase2Only but boss in Phase1"),
					i, *E.DebugTag.ToString());
				continue;
			}
		}

		// [BossAttackCooldownPersistV1] 개별 쿨다운 남아있으면 제외 — Enemy 캐릭터의 맵 조회.
		// Walk 슬롯(AttackAbility=null) 은 class key 가 없어 자동 통과.
		if (E.AttackAbility.Get())
		{
			if (const float* RemainCd = Enemy->BossAttackCooldowns.Find(E.AttackAbility))
			{
				if (*RemainCd > 0.f)
				{
					UE_LOG(LogTemp, Verbose,
						TEXT("[BossAttackCooldownPersistV1] entry #%d (%s) skipped — cd=%.2fs remaining"),
						i, *E.DebugTag.ToString(), *RemainCd);
					continue;
				}
			}
		}

		// 연속 상한 초과 시 제외 (Sequential 모드는 예외)
		if (SelectionMode != EBossChooseAttackMode::Sequential
			&& i == Data.LastSelectedIndex
			&& Data.SameAttackStreak >= MaxSameAttackStreak)
		{
			continue;
		}

		if (E.AttackAbility.Get())
		{
			AbilityCandidates.Add(i);
		}
		else
		{
			WalkCandidates.Add(i);
		}
	}

	// [BossWalkPriorityV1] GA 후보 있으면 GA 만, 없으면 Walk 폴백.
	// false 면 기존 동작 (둘을 합쳐 가중치/랜덤 선택).
	TArray<int32> Candidates;
	if (bPreferAbilityOverWalk)
	{
		if (AbilityCandidates.Num() > 0)
		{
			Candidates = MoveTemp(AbilityCandidates);
		}
		else
		{
			Candidates = MoveTemp(WalkCandidates);
		}
	}
	else
	{
		Candidates.Append(AbilityCandidates);
		Candidates.Append(WalkCandidates);
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[BossWalkPriorityV1] Boss=%s Dist=%.0f AbilityCands=%d WalkCands=%d FinalCands=%d Prefer=%d"),
		*Pawn->GetName(), Dist,
		AbilityCandidates.Num(), WalkCandidates.Num(), Candidates.Num(),
		bPreferAbilityOverWalk ? 1 : 0);

	if (Candidates.Num() == 0)
	{
		if (bEndTaskIfNoValidCandidate)
			return EStateTreeRunStatus::Failed;

		// [ChaseFallbackV1] 후보 자체가 없으면 (모든 GA 가 거리/쿨/HP 조건으로 탈락 + Walk 슬롯 없음)
		// 보스를 그냥 플레이어 쪽으로 걷게 한다 — freeze 방지.
		if (bChaseFallbackWhenIdle && Dist > ChaseFallbackStopRange)
		{
			if (AActor* TargetActor = TD.TargetActor.Get())
			{
				const FVector Dir = (TargetActor->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
				if (!Dir.IsNearlyZero())
				{
					Pawn->AddMovementInput(Dir, 1.0f);
				}
			}
		}

		// 후보가 없으면 짧게만 대기 후 재시도 — 쿨다운 중복으로 멈춤 상태가 길어지지 않도록.
		Data.CooldownRemaining = FMath::Min(AttackCooldown, 0.25f);
		return EStateTreeRunStatus::Running;
	}

	// ── 선택 ──────────────────────────────────────────────────────────
	int32 ChosenIndex = INDEX_NONE;

	if (SelectionMode == EBossChooseAttackMode::Sequential)
	{
		// 필터된 후보 안에서 NextSequentialIndex가 가리키는 순번.
		const int32 SeqSlot = Data.NextSequentialIndex % Candidates.Num();
		ChosenIndex = Candidates[SeqSlot];
		Data.NextSequentialIndex = (Data.NextSequentialIndex + 1) % FMath::Max(1, Candidates.Num());
	}
	else if (SelectionMode == EBossChooseAttackMode::Random)
	{
		ChosenIndex = Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
	}
	else // WeightedRandom
	{
		// 각 후보의 유효 가중치 산출 — 연속 사용된 엔트리는 RepeatPenalty 곱.
		TArray<float> EffectiveWeights;
		EffectiveWeights.Reserve(Candidates.Num());
		float TotalWeight = 0.f;
		for (int32 Idx : Candidates)
		{
			float W = FMath::Max(0.f, AttackPool[Idx].Weight);
			if (Idx == Data.LastSelectedIndex && Data.SameAttackStreak > 0)
			{
				W *= RepeatPenalty;
			}
			EffectiveWeights.Add(W);
			TotalWeight += W;
		}

		if (TotalWeight <= KINDA_SMALL_NUMBER)
		{
			// 가중치가 전부 0이면 균등 랜덤으로 폴백.
			ChosenIndex = Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
		}
		else
		{
			float Roll = FMath::FRandRange(0.f, TotalWeight);
			int32 Pick = Candidates.Last();
			for (int32 k = 0; k < Candidates.Num(); ++k)
			{
				Roll -= EffectiveWeights[k];
				if (Roll <= 0.f)
				{
					Pick = Candidates[k];
					break;
				}
			}
			ChosenIndex = Pick;
		}
	}

	if (!AttackPool.IsValidIndex(ChosenIndex))
		return EStateTreeRunStatus::Running;

	// 연속 통계 업데이트
	if (ChosenIndex == Data.LastSelectedIndex)
	{
		Data.SameAttackStreak += 1;
	}
	else
	{
		Data.LastSelectedIndex = ChosenIndex;
		Data.SameAttackStreak  = 1;
	}

	const FBossAttackEntry& Chosen = AttackPool[ChosenIndex];

	// ── 발동 (또는 Walk 처리) ─────────────────────────────────────────
	TSubclassOf<UHellunaEnemyGameplayAbility> GAClass = Chosen.AttackAbility;
	if (!GAClass.Get())
	{
		// 걷기 슬롯: WalkReentryDelay만큼 글로벌 쿨다운 세팅 → 복귀 후 잠시 공격 금지.
		// bWalkExitsToChase=true 면 Succeeded 반환 → OnStateCompleted 전환으로 Chase 복귀.
		Data.CooldownRemaining = FMath::Max(AttackCooldown, WalkReentryDelay);

		if (bWalkExitsToChase)
		{
			return EStateTreeRunStatus::Succeeded;
		}
		return EStateTreeRunStatus::Running;
	}

	// GA 부여 (없으면)
	bool bAlreadyHas = false;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetClass() == GAClass)
		{
			bAlreadyHas = true;
			break;
		}
	}
	if (!bAlreadyHas)
	{
		FGameplayAbilitySpec Spec(GAClass);
		Spec.SourceObject = Enemy;
		Spec.Level = 1;
		ASC->GiveAbility(Spec);
	}

	// CurrentTarget 주입 — 모든 파생 GA가 오버라이드한 SetCurrentTarget 다형 호출.
	// 이전엔 UEnemyGameplayAbility_Attack만 Cast → RangedAttack/DashAttack 미상속이라
	// CurrentTarget 누락 → 원거리가 Z축 없이 수평 발사되는 버그 원인이었음.
	AActor* BossChosenTarget = TD.TargetActor.Get();
	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (!Spec.Ability || Spec.Ability->GetClass() != GAClass) continue;

		if (UHellunaEnemyGameplayAbility* CdoGA = Cast<UHellunaEnemyGameplayAbility>(Spec.Ability))
			CdoGA->SetCurrentTarget(BossChosenTarget);

		if (UGameplayAbility* Instance = Spec.GetPrimaryInstance())
		{
			if (UHellunaEnemyGameplayAbility* InstGA = Cast<UHellunaEnemyGameplayAbility>(Instance))
				InstGA->SetCurrentTarget(BossChosenTarget);
		}
		break;
	}

	const bool bActivated = ASC->TryActivateAbilityByClass(GAClass);
	if (bActivated)
	{
		Data.CooldownRemaining = AttackCooldown;

		// [BossAttackCooldownPersistV1] Attack 상태 재진입에도 살아남도록 Enemy 맵에 쿨 저장.
		if (Chosen.Cooldown > 0.f)
		{
			Enemy->BossAttackCooldowns.Add(GAClass, Chosen.Cooldown);
			UE_LOG(LogTemp, Warning,
				TEXT("[BossAttackCooldownPersistV1] entry '%s' activated — cd=%.2fs set on Enemy map"),
				*Chosen.DebugTag.ToString(), Chosen.Cooldown);
		}
		// legacy 배열도 동기화 (디버그 뷰 용)
		if (Data.PerEntryCooldowns.IsValidIndex(ChosenIndex))
		{
			Data.PerEntryCooldowns[ChosenIndex] = Chosen.Cooldown > 0.f ? Chosen.Cooldown : 0.f;
		}
	}
	else
	{
		Data.CooldownRemaining = FMath::Min(AttackCooldown, 0.25f);
	}

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// ExitState
// ============================================================================
void FSTTask_BossChooseAttack::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (AAIController* AIC = Data.AIController)
	{
		AIC->ClearFocus(EAIFocusPriority::Gameplay);

		if (APawn* Pawn = AIC->GetPawn())
		{
			if (UCharacterMovementComponent* CMC = Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent()))
			{
				CMC->bOrientRotationToMovement     = true;
				CMC->bUseControllerDesiredRotation = false;
			}
		}
	}
}
