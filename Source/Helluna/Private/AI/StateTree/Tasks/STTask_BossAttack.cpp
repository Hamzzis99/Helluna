/**
 * STTask_BossAttack.cpp
 *
 * 보스 전용 일반 공격 Task.
 * AttackAbilities 배열에서 공격 GA를 순서/랜덤 선택하여 발동.
 *
 * @author 김민우
 */

#include "AI/StateTree/Tasks/STTask_BossAttack.h"
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

namespace BossAttackHelpers
{
static void FaceTarget(AAIController* AIC, APawn* Pawn, AActor* Target, float DeltaTime, float Speed)
{
	if (!AIC || !Pawn || !Target) return;

	const FVector ToTarget = (Target->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero()) return;

	// ControlRotation 기반 보간으로 매 틱 누적 → 끊김 없음.
	// SetFocus는 호출하지 않음: AAIController::UpdateControlRotation이 Focus 대상으로
	// ControlRotation을 스냅 덮어쓰기 하기 때문에 수동 RInterpTo가 무효화됨.
	const FRotator CurrentRot = AIC->GetControlRotation();
	const FRotator TargetRot(0.f, ToTarget.Rotation().Yaw, 0.f);

	// [RotV7][Aim] 부드러움 향상 — 스냅 임계 2.5°→1.0° (시각 위화감 제거).
	// 속도 파라미터는 호출측에서 증가 (12→30). 수학은 per-tick O(1)이라 프레임 영향 없음.
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

	// 액터 회전도 수동 보간. MaxWalkSpeed=0 잠금 상태의 CMC PhysicsRotation은 ActorYaw를
	// 업데이트하지 않음 → 수동 SetActorRotation으로 CMC 의존 제거.
	Pawn->SetActorRotation(NewRot);

	// 진단 V3 — Melee와 동일 포맷으로 끊김 원인 단계 추적.
	float RotRateY = -1.f;
	uint8 bDesired = 0, bOrient = 0;
	if (UCharacterMovementComponent* CMC = Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent()))
	{
		RotRateY = CMC->RotationRate.Yaw;
		bDesired = CMC->bUseControllerDesiredRotation ? 1 : 0;
		bOrient  = CMC->bOrientRotationToMovement ? 1 : 0;
	}
	UE_LOG(LogTemp, Warning,
		TEXT("[RotV7][Boss] CtrlYaw %.1f→%.1f | ActorYaw=%.1f | Δ=%.2f | Vel=%.0f | RotRateY=%.0f | Desired=%d Orient=%d | DT=%.4f"),
		CurrentRot.Yaw, NewRot.Yaw,
		Pawn->GetActorRotation().Yaw,
		YawDeltaDeg,
		Pawn->GetVelocity().Size2D(),
		RotRateY, bDesired, bOrient, DeltaTime);
}
}

// ============================================================================
// EnterState
// ============================================================================
EStateTreeRunStatus FSTTask_BossAttack::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.AIController)
		return EStateTreeRunStatus::Failed;

	Data.AIController->StopMovement();

	// Chase/Evaluator가 남겨둔 Focus를 즉시 해제 — 타겟이 바뀌었을 때 이전 Focus의
	// UpdateControlRotation 스냅이 한 틱 끼어들어 상체가 엉뚱한 방향을 바라보는 현상 제거.
	AActor* PrevFocus = Data.AIController->GetFocusActor();
	Data.AIController->ClearFocus(EAIFocusPriority::Gameplay);

	APawn* Pawn = Data.AIController->GetPawn();
	if (UCharacterMovementComponent* CMC = Pawn ? Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent()) : nullptr)
	{
		CMC->bOrientRotationToMovement     = false;
		CMC->bUseControllerDesiredRotation = true;
	}

	Data.CooldownRemaining = InitialDelay;

	UE_LOG(LogTemp, Warning,
		TEXT("[RotV2][Boss] Enter ClearedFocus=%s"),
		PrevFocus ? *PrevFocus->GetName() : TEXT("none"));

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// Tick
// ============================================================================
EStateTreeRunStatus FSTTask_BossAttack::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	AAIController* AIC = Data.AIController;
	if (!AIC) return EStateTreeRunStatus::Failed;

	APawn* Pawn = AIC->GetPawn();
	if (!Pawn) return EStateTreeRunStatus::Failed;

	// 매 틱 Focus 강제 해제 — Evaluator의 매 틱 SetFocus가 ControlRotation을 스냅
	// 덮어쓰는 현상 차단 (멜리 Task와 동일 처리).
	AIC->ClearFocus(EAIFocusPriority::Gameplay);

	// 매 틱 CMC 회전 플래그 재강제. UEnemyGameplayAbility_Attack::OnMontageCompleted의
	// AttackRecoveryDelay 타이머 UnlockMovement() 복구로 Desired=0,Orient=1 되돌아가
	// 쿨다운 중 액터가 안 돌다가 다음 LockMovement 스냅으로 점프 → 끊김. AttackTask
	// 러닝 동안 Desired=1,Orient=0 유지해 RInterpTo가 매 틱 액터에 반영되게 함.
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
	const FBossPatternData& PD = Data.PatternData;

	// 패턴 실행 중이면 공격 중단
	if (PD.bPatternActive)
		return EStateTreeRunStatus::Running;

	// 공격 목록이 비어있으면 대기
	if (AttackAbilities.Num() == 0)
		return EStateTreeRunStatus::Running;

	// ① 몽타주가 실제 재생 중일 때만 회전 스킵.
	// AttackRecoveryDelay 단계까지 스킵하면 액터가 수 초간 정지→점프 반복으로 끊겨 보임.
	bool bMontagePlaying = false;
	if (USkeletalMeshComponent* Mesh = Enemy->GetMesh())
	{
		if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
		{
			if (Enemy->AttackMontage && AnimInst->Montage_IsPlaying(Enemy->AttackMontage))
			{
				bMontagePlaying = true;
			}
		}
	}
	if (bMontagePlaying)
		return EStateTreeRunStatus::Running;

	// 몽타주 외 상태(쿨다운 / Recovery Delay 포함)에서는 타겟 방향 회전 실행.
	if (TD.HasValidTarget())
	{
		BossAttackHelpers::FaceTarget(AIC, Pawn, TD.TargetActor.Get(), DeltaTime, RotationSpeed);
	}

	// Recovery Delay 중엔 GA가 아직 active → 다음 GA 발동 금지.
	bool bAnyGAActive = false;
	for (const TSubclassOf<UHellunaEnemyGameplayAbility>& GAClass : AttackAbilities)
	{
		if (!GAClass.Get()) continue;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (Spec.Ability && Spec.Ability->GetClass() == GAClass && Spec.IsActive())
			{
				bAnyGAActive = true;
				break;
			}
		}
		if (bAnyGAActive) break;
	}
	if (bAnyGAActive)
		return EStateTreeRunStatus::Running;

	// ② 쿨다운 대기
	if (Data.CooldownRemaining > 0.f)
	{
		Data.CooldownRemaining -= DeltaTime;
		return EStateTreeRunStatus::Running;
	}

	// ③ 사거리 체크
	if (!TD.HasValidTarget() || TD.DistanceToTarget > AttackRange)
		return EStateTreeRunStatus::Running;

	// ④ 공격 GA 선택
	int32 AttackIdx = 0;
	if (SelectionMode == EBossAttackSelection::Sequential)
	{
		AttackIdx = Data.NextAttackIndex % AttackAbilities.Num();
		Data.NextAttackIndex = (Data.NextAttackIndex + 1) % AttackAbilities.Num();
	}
	else
	{
		AttackIdx = FMath::RandRange(0, AttackAbilities.Num() - 1);
	}

	TSubclassOf<UHellunaEnemyGameplayAbility> GAClass = AttackAbilities[AttackIdx];
	if (!GAClass.Get())
		return EStateTreeRunStatus::Running;

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

	// CurrentTarget 설정 — CDO와 Instance 둘 다 설정해야 InstancedPerActor GA가 올바른 타겟 사용
	AActor* BossChosenTarget = TD.TargetActor.Get();
	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (!Spec.Ability || Spec.Ability->GetClass() != GAClass) continue;

		if (UEnemyGameplayAbility_Attack* CdoAttack = Cast<UEnemyGameplayAbility_Attack>(Spec.Ability))
			CdoAttack->CurrentTarget = BossChosenTarget;

		if (UGameplayAbility* Instance = Spec.GetPrimaryInstance())
		{
			if (UEnemyGameplayAbility_Attack* InstAttack = Cast<UEnemyGameplayAbility_Attack>(Instance))
				InstAttack->CurrentTarget = BossChosenTarget;
		}

		break;
	}

	// ⑤ GA 발동
	const bool bActivated = ASC->TryActivateAbilityByClass(GAClass);
	if (bActivated)
	{
		Data.CooldownRemaining = AttackCooldown;
	}

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// ExitState
// ============================================================================
void FSTTask_BossAttack::ExitState(
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
