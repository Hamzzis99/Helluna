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
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_Attack.h"

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

	// InitialDelay는 최초 진입에만 강제. 이후 재진입(Walk 후 복귀 등)에서는
	// 이미 WalkReentryDelay 등으로 CooldownRemaining이 세팅되어 있을 수 있어 보존.
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

	// Per-entry 쿨다운 배열 크기 동기화 (풀이 수정됐을 수 있음)
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

	AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Pawn);
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

	// 몽타주 재생 여부 감지 (진단용 — 이전 버그: 재생 중이면 회전 스킵 → 공격 중 엉뚱한 방향)
	// 수정: 몬타지 중에도 회전 계속 수행. 어떤 몬타지든 (Enemy->AttackMontage 뿐 아니라
	// GA별 오버라이드 AM_Boss_Ranged 등) 모두 감지하려면 GetActiveMontageInstance 사용.
	bool bMontagePlaying = false;
	if (USkeletalMeshComponent* Mesh = Enemy->GetMesh())
	{
		if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
		{
			bMontagePlaying = (AnimInst->GetActiveMontageInstance() != nullptr);
		}
	}

	// 타겟 있으면 무조건 회전 (몬타지 재생 중이어도) — 사용자 요구: "공격 몬타지 중에도 나를 바라봐"
	bool bFaceCalled = false;
	if (TD.HasValidTarget())
	{
		BossChooseAttackHelpers::FaceTarget(AIC, Pawn, TD.TargetActor.Get(), DeltaTime, RotationSpeed);
		bFaceCalled = true;
	}

	// ═══ [AttackAim] 회전 이슈 분석용 진단 로그 ═══
	// Phase=Montage → 공격 몬타지 재생 중 (멜리 스트라이크 등)
	// Phase=Idle    → 쿨다운/선택 대기 상태
	// Yaw          → 현재 보스 액터 Yaw
	// Tgt          → 플레이어 방향 Yaw (목표)
	// D            → 둘의 각도 차 (양수, 절댓값)
	// Face=1       → 이번 틱에 FaceTarget 호출됨
	// Face=0       → 타겟 없거나 조건 미충족으로 회전 스킵
	{
		const float CurYaw = Pawn->GetActorRotation().Yaw;
		float TgtYaw = 0.f;
		float Delta  = 0.f;
		if (TD.HasValidTarget())
		{
			if (AActor* T = TD.TargetActor.Get())
			{
				const FVector ToT = (T->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
				if (!ToT.IsNearlyZero())
				{
					TgtYaw = ToT.Rotation().Yaw;
					Delta = FMath::Abs(FRotator::NormalizeAxis(TgtYaw - CurYaw));
				}
			}
		}
		UE_LOG(LogTemp, Warning,
			TEXT("[AttackAim] Phase=%s Yaw=%.1f Tgt=%.1f D=%.1f Face=%d Tgt=%s"),
			bMontagePlaying ? TEXT("Montage") : TEXT("Idle"),
			CurYaw, TgtYaw, Delta, bFaceCalled ? 1 : 0,
			TD.HasValidTarget() && TD.TargetActor.Get()
				? *TD.TargetActor->GetName() : TEXT("null"));
	}

	// 활성 GA가 있으면 대기 (AttackRecoveryDelay 포함)
	bool bAnyGAActive = false;
	for (const FBossAttackEntry& Entry : AttackPool)
	{
		if (!Entry.AttackAbility.Get()) continue;
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
	for (int32 i = 0; i < Data.PerEntryCooldowns.Num(); ++i)
	{
		if (Data.PerEntryCooldowns[i] > 0.f)
		{
			Data.PerEntryCooldowns[i] = FMath::Max(0.f, Data.PerEntryCooldowns[i] - DeltaTime);
		}
	}
	if (bGlobalCooldown)
	{
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
	// 거리 범위 + per-entry 쿨다운 + 연속 상한 기준으로 사용 가능한 엔트리만 수집.
	TArray<int32> Candidates;
	Candidates.Reserve(AttackPool.Num());
	for (int32 i = 0; i < AttackPool.Num(); ++i)
	{
		const FBossAttackEntry& E = AttackPool[i];
		if (Dist < E.MinRange || Dist > E.MaxRange) continue;

		// 개별 쿨다운 남아있으면 제외
		if (Data.PerEntryCooldowns.IsValidIndex(i) && Data.PerEntryCooldowns[i] > 0.f)
			continue;

		// 연속 상한 초과 시 제외 (Sequential 모드는 예외)
		if (SelectionMode != EBossChooseAttackMode::Sequential
			&& i == Data.LastSelectedIndex
			&& Data.SameAttackStreak >= MaxSameAttackStreak)
		{
			continue;
		}
		Candidates.Add(i);
	}

	if (Candidates.Num() == 0)
	{
		if (bEndTaskIfNoValidCandidate)
			return EStateTreeRunStatus::Failed;

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
