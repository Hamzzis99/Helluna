/**
 * STTask_AttackTarget.cpp
 *
 * 공격 GA를 주기적으로 발동하고 쿨다운을 관리한다.
 *
 * ─── Tick 흐름 ───────────────────────────────────────────────
 *  ① GA 활성 중 → Running (GA 내부에서 몽타주 + 경직 처리)
 *  ② GA 종료 + 쿨다운 중 → 카운트다운 + 타겟 방향 회전
 *  ③ 쿨다운 완료 → GA 발동 + CooldownRemaining 초기화
 *
 *  광폭화 시: CooldownRemaining *= EnrageCooldownMultiplier (0.5 → 2배 빠름)
 *
 * @author 김민우
 */

#include "AI/StateTree/Tasks/STTask_AttackTarget.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "AI/SpaceShipAttackSlotManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

#include "Character/HellunaEnemyCharacter.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_Attack.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_RangedAttack.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_ShipJump.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"

namespace HellunaAttackTarget
{
static USpaceShipAttackSlotManager* GetShipSlotManager(AActor* TargetActor)
{
	return TargetActor ? TargetActor->FindComponentByClass<USpaceShipAttackSlotManager>() : nullptr;
}

// [ShipFaceV1] 우주선 공격 직전 스냅 — RInterpTo 대신 즉시 Yaw 정렬.
// 쿨다운 중 부드러운 회전은 유지, GA 발동 순간만 스냅 적용해 "옆 보고 공격" 방지.
static void SnapFaceTarget(AAIController* AIController, APawn* Pawn, AActor* TargetActor)
{
	if (!AIController || !Pawn || !TargetActor)
	{
		return;
	}
	const FVector ToTarget = (TargetActor->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero())
	{
		return;
	}
	const FRotator SnapRot(0.f, ToTarget.Rotation().Yaw, 0.f);
	AIController->SetControlRotation(SnapRot);
	Pawn->SetActorRotation(SnapRot);

	UE_LOG(LogTemp, Warning,
		TEXT("[ShipFaceV1] Snap-face ship Yaw=%.1f Enemy=%s"),
		SnapRot.Yaw, *Pawn->GetName());
}

static void FaceCurrentTarget(AAIController* AIController, APawn* Pawn, AActor* TargetActor, float DeltaTime, float Speed)
{
	if (!AIController || !Pawn || !TargetActor)
	{
		return;
	}

	const FVector ToTarget = (TargetActor->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero())
	{
		return;
	}

	// ControlRotation 기준으로 보간. 이전 틱의 보간 결과를 이어받아 부드럽게 누적.
	const FRotator CurrentRot = AIController->GetControlRotation();
	const FRotator TargetRot(0.f, ToTarget.Rotation().Yaw, 0.f);

	// [RotV7][Aim] 부드러움 향상:
	//  - RInterp 속도 20 → 30 (점근 곡선 초반 기울기↑ → 추격이 더 빠르고 "따라가는 느낌"이 선명)
	//  - 스냅 임계 2.5° → 1.0° (스냅 순간 시각 위화감 제거, 사람 눈에 자연스러운 수렴)
	//  - 수학 cost는 per-tick O(1) 몇 개 곱셈이라 적 수와 무관하게 프레임 영향 거의 없음.
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
	AIController->SetControlRotation(NewRot);

	// 액터 회전도 동시에 수동 보간. MaxWalkSpeed=0으로 잠긴 Pawn은 CMC PhysicsRotation의
	// DesiredRotation 추적이 사실상 정지(속도 0 → 스킵) → CtrlYaw만 돌고 ActorYaw 고정되는
	// 끊김 현상 발생. 수동 SetActorRotation으로 CMC 의존성 제거.
	Pawn->SetActorRotation(NewRot);

	// SetFocus는 호출하지 않음. AAIController::UpdateControlRotation이 Focus 기반으로
	// ControlRotation을 매 틱 스냅으로 덮어쓰기 때문 → 방금 한 RInterpTo가 무효화되어
	// 상체/머리가 끊겨 보이게 됨. Focus 없이 수동 SetControlRotation만 쓰면 부드러움 유지.

	// 진단 V3 — ControlRotation 보간 결과 + ActorRotation + Velocity + CMC 설정값을 모두 출력.
	// 끊김의 진짜 원인이 어느 단계인지 좁히기 위함:
	//   - CtrlYaw가 점프하면: SetControlRotation 직후 누군가 덮어쓰기 (Focus, Evaluator, etc.)
	//   - CtrlYaw 부드러운데 ActorYaw 점프: CMC RotationRate가 너무 빨라 "딱딱" (혹은 너무 느려 lag)
    //   - ActorYaw 부드러운데 시각적으로 끊김: AnimBP Aim Offset 보간 문제
    //   - Velocity가 매 틱 0/200/0/200 패턴: PathFollowing이 멈췄다 재개 반복
	float RotRateY = -1.f;
	uint8 bDesired = 0, bOrient = 0;
	if (UCharacterMovementComponent* CMC = Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent()))
	{
		RotRateY = CMC->RotationRate.Yaw;
		bDesired = CMC->bUseControllerDesiredRotation ? 1 : 0;
		bOrient  = CMC->bOrientRotationToMovement ? 1 : 0;
	}
	UE_LOG(LogTemp, Warning,
		TEXT("[RotV7][Melee] CtrlYaw %.1f→%.1f | ActorYaw=%.1f | Δ=%.2f | Vel=%.0f | RotRateY=%.0f | Desired=%d Orient=%d | DT=%.4f"),
		CurrentRot.Yaw, NewRot.Yaw,
		Pawn->GetActorRotation().Yaw,
		YawDeltaDeg,
		Pawn->GetVelocity().Size2D(),
		RotRateY, bDesired, bOrient, DeltaTime);
}
}

EStateTreeRunStatus FSTTask_AttackTarget::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.AIController)
		return EStateTreeRunStatus::Failed;

	// Attack State 진입: 이동 정지 + 타겟 방향 회전 모드 전환
	InstanceData.AIController->StopMovement();

	// Chase/Evaluator가 남겨둔 Focus를 즉시 해제 — 타겟이 바뀌었을 때 이전 Focus의
	// UpdateControlRotation 스냅이 한 틱 끼어들어 상체가 엉뚱한 방향을 바라보는 현상 제거.
	AActor* PrevFocus = InstanceData.AIController->GetFocusActor();
	InstanceData.AIController->ClearFocus(EAIFocusPriority::Gameplay);

	APawn* Pawn = InstanceData.AIController->GetPawn();
	if (UCharacterMovementComponent* MoveComp = Pawn ? Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent()) : nullptr)
	{
		MoveComp->bOrientRotationToMovement     = false;
		MoveComp->bUseControllerDesiredRotation = true;
	}

	// 검증용 로그
	UE_LOG(LogTemp, Warning,
		TEXT("[RotV2][Melee] Enter ClearedFocus=%s"),
		PrevFocus ? *PrevFocus->GetName() : TEXT("none"));

	const FHellunaAITargetData& TargetData = Context.GetInstanceData(*this).TargetData;

	// 광폭화 상태인데 타겟이 플레이어로 잘못 설정된 경우 → SpaceShip으로 강제 교정
	if (TargetData.HasValidTarget())
	{
		AActor* TargetActor = TargetData.TargetActor.Get();
		const bool bIsSpaceShip = (Cast<AResourceUsingObject_SpaceShip>(TargetActor) != nullptr);

		// 광폭화 후에는 우주선만 공격 (플레이어가 타겟이면 포커스 설정 스킵)
		if (TargetData.bEnraged && !bIsSpaceShip)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[enemybugreport][AttackEnterFail] Reason=EnragedTargetMismatch"));
			return EStateTreeRunStatus::Failed;
		}

		// [ShipFaceV1] 우주선 타겟 진입 시에는 즉시 스냅(옆 보고 공격 방지).
		// 그 외 타겟은 기존 방식대로 보간으로 진입.
		if (bIsSpaceShip)
		{
			HellunaAttackTarget::SnapFaceTarget(InstanceData.AIController, Pawn, TargetActor);
		}
		else
		{
			HellunaAttackTarget::FaceCurrentTarget(InstanceData.AIController, Pawn, TargetActor, 1.f / 60.f, RotationSpeed);
		}
	}

	InstanceData.CooldownRemaining = InitialAttackDelay;

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_AttackTarget::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	AAIController* AIController = InstanceData.AIController;
	if (!AIController) return EStateTreeRunStatus::Failed;

	APawn* Pawn = AIController->GetPawn();
	if (!Pawn) return EStateTreeRunStatus::Failed;

	AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Pawn);
	if (!Enemy) return EStateTreeRunStatus::Failed;

	UHellunaAbilitySystemComponent* ASC = Cast<UHellunaAbilitySystemComponent>(
		Enemy->GetAbilitySystemComponent());
	if (!ASC) return EStateTreeRunStatus::Failed;

	// 매 틱 Focus 강제 해제. STEvaluator_TargetSelector가 어그로 타겟 발견 시 매 틱
	// SetFocus를 다시 걸어 AIC::UpdateControlRotation이 ControlRotation을 스냅 덮어씀
	// → 직전 틱 RInterpTo 결과가 무효화되어 머리/상체가 끊겨 보임. EnterState 1회 ClearFocus
	// 만으론 부족해서 Tick에서도 매 틱 차단.
	AIController->ClearFocus(EAIFocusPriority::Gameplay);

	// 매 틱 CMC 회전 플래그 재강제. UEnemyGameplayAbility_Attack::OnMontageCompleted의
	// AttackRecoveryDelay 타이머가 UnlockMovement()를 호출하면 Desired=0,Orient=1로
	// Chase 모드 복구됨 → 쿨다운 중 SetControlRotation이 액터에 반영되지 않아
	// 다음 공격 LockMovementAndFaceTarget이 큰 각도를 한번에 스냅 → 끊김 발생.
	// AttackTask 실행 중엔 항상 Desired=1,Orient=0 유지해 RInterpTo가 액터에 적용되게 함.
	if (UCharacterMovementComponent* CMC = Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent()))
	{
		CMC->bOrientRotationToMovement     = false;
		CMC->bUseControllerDesiredRotation = true;
	}

	// 사용할 GA 클래스: 에디터에서 선택한 클래스, 없으면 기본 클래스 사용
	TSubclassOf<UHellunaEnemyGameplayAbility> GAClass = AttackAbilityClass.Get()
		? AttackAbilityClass
		: TSubclassOf<UHellunaEnemyGameplayAbility>(UHellunaEnemyGameplayAbility::StaticClass());

	const FHellunaAITargetData& TargetData = InstanceData.TargetData;
	// ① 몽타주가 실제로 재생 중일 때만 회전 스킵 — 이전엔 Spec.IsActive()로
	// AttackRecoveryDelay까지 묶어서 스킵했으나 그 시간 동안 액터가 정지 상태로
	// 누적돼 다음 공격 SetActorRotation 스냅으로 큰 각도 점프 → 20~30fps처럼 끊김.
	// Recovery 중엔 회전 허용해 부드럽게 다음 공격 각도를 추적.
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
	{
		return EStateTreeRunStatus::Running;
	}
	// GA가 active지만 몽타주 끝난 상태(AttackRecoveryDelay) → 아래로 계속 진행해 회전.
	bool bGAActive = false;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetClass() == GAClass)
		{
			bGAActive = Spec.IsActive();
			break;
		}
	}

	// ② GA 종료 후 쿨다운 대기 중
	if (InstanceData.CooldownRemaining > 0.f)
	{
		InstanceData.CooldownRemaining -= DeltaTime;

		if (TargetData.HasValidTarget())
		{
			HellunaAttackTarget::FaceCurrentTarget(AIController, Pawn, TargetData.TargetActor.Get(), DeltaTime, RotationSpeed);
		}

		return EStateTreeRunStatus::Running;
	}

	if (TargetData.HasValidTarget())
	{
		HellunaAttackTarget::FaceCurrentTarget(AIController, Pawn, TargetData.TargetActor.Get(), DeltaTime, RotationSpeed);
	}

	// GA가 Recovery Delay 단계로 아직 active면 다음 GA 발동 금지 (중복 활성화 방지).
	// 회전은 위에서 이미 수행됨.
	if (bGAActive)
	{
		return EStateTreeRunStatus::Running;
	}

	// ③ 쿨다운 완료 → GA 발동
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

	// TryActivate 이전에 CurrentTarget 설정 (GA ActivateAbility 내부에서 즉시 참조).
	// InstancedPerActor GA는 CDO(Spec.Ability)와 별개로 Instance가 존재하므로
	// Instance에도 반드시 설정해야 함. CDO만 설정하면 두 번째 활성화부터 stale 값(null)을
	// 읽어 ResolveAttackTarget 폴백이 Spaceship을 반환하는 버그 발생.
	AActor* ChosenTarget = TargetData.TargetActor.Get();
	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (!Spec.Ability || Spec.Ability->GetClass() != GAClass) continue;

		// CDO 설정 — 이후 새 Instance가 생성될 때의 기본값
		if (UEnemyGameplayAbility_Attack* CdoAttack = Cast<UEnemyGameplayAbility_Attack>(Spec.Ability))
			CdoAttack->CurrentTarget = ChosenTarget;
		else if (UEnemyGameplayAbility_RangedAttack* CdoRanged = Cast<UEnemyGameplayAbility_RangedAttack>(Spec.Ability))
			CdoRanged->CurrentTarget = ChosenTarget;
		else if (UEnemyGameplayAbility_ShipJump* CdoShipJump = Cast<UEnemyGameplayAbility_ShipJump>(Spec.Ability))
			CdoShipJump->CurrentTarget = ChosenTarget;

		// Instance 설정 — 실제 ActivateAbility가 호출되는 객체
		if (UGameplayAbility* Instance = Spec.GetPrimaryInstance())
		{
			if (UEnemyGameplayAbility_Attack* InstAttack = Cast<UEnemyGameplayAbility_Attack>(Instance))
				InstAttack->CurrentTarget = ChosenTarget;
			else if (UEnemyGameplayAbility_RangedAttack* InstRanged = Cast<UEnemyGameplayAbility_RangedAttack>(Instance))
				InstRanged->CurrentTarget = ChosenTarget;
			else if (UEnemyGameplayAbility_ShipJump* InstShipJump = Cast<UEnemyGameplayAbility_ShipJump>(Instance))
				InstShipJump->CurrentTarget = ChosenTarget;
		}

		break;
	}

	// [ShipFaceV1] 우주선 공격 GA 발동 직전 스냅 — 옆 보고 공격하는 케이스 원천 차단.
	// 쿨다운 중 RInterpTo가 완료되지 않은 프레임에서도 공격 순간 반드시 우주선 정면을 향하게 함.
	if (Cast<AResourceUsingObject_SpaceShip>(ChosenTarget) != nullptr)
	{
		HellunaAttackTarget::SnapFaceTarget(AIController, Pawn, ChosenTarget);
	}

	const bool bActivated = ASC->TryActivateAbilityByClass(GAClass);
	if (bActivated)
	{
		const float CooldownMultiplier = Enemy->bEnraged ? Enemy->EnrageCooldownMultiplier : 1.f;
		InstanceData.CooldownRemaining = AttackCooldown * CooldownMultiplier;
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_AttackTarget::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (AAIController* AIC = InstanceData.AIController)
	{
		const FHellunaAITargetData& TargetData = InstanceData.TargetData;
		const bool bKeepShipFocus = TargetData.HasValidTarget()
			&& Cast<AResourceUsingObject_SpaceShip>(TargetData.TargetActor.Get()) != nullptr;
		if (TargetData.HasValidTarget())
		{
			if (AActor* TargetActor = TargetData.TargetActor.Get())
			{
				if (Cast<AResourceUsingObject_SpaceShip>(TargetActor))
				{
					if (USpaceShipAttackSlotManager* SlotManager = HellunaAttackTarget::GetShipSlotManager(TargetActor))
					{
						if (APawn* Pawn = AIC->GetPawn())
						{
							SlotManager->ReleaseEngagementReservation(Pawn);
						}
					}
				}
			}
		}

		if (bKeepShipFocus)
		{
			AIC->SetFocus(TargetData.TargetActor.Get());
		}
		else
		{
			AIC->ClearFocus(EAIFocusPriority::Gameplay);
		}

		if (APawn* Pawn = AIC->GetPawn())
		{
			if (UCharacterMovementComponent* MoveComp = Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent()))
			{
				// 원래 설정으로 복원
				MoveComp->bOrientRotationToMovement     = true;
				MoveComp->bUseControllerDesiredRotation = false;
			}
		}
	}
}
