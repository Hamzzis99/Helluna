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

#include "Character/HellunaEnemyCharacter.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_Attack.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_RangedAttack.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"

namespace HellunaAttackTarget
{
static USpaceShipAttackSlotManager* GetShipSlotManager(AActor* TargetActor)
{
	return TargetActor ? TargetActor->FindComponentByClass<USpaceShipAttackSlotManager>() : nullptr;
}

static const TCHAR* SlotStateToString(ESlotState SlotState)
{
	switch (SlotState)
	{
	case ESlotState::Free:
		return TEXT("Free");
	case ESlotState::Reserved:
		return TEXT("Reserved");
	case ESlotState::Occupied:
		return TEXT("Occupied");
	default:
		return TEXT("Unknown");
	}
}

static void FaceCurrentTarget(AAIController* AIController, APawn* Pawn, AActor* TargetActor, float DeltaTime)
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

	const FRotator CurrentRot = Pawn->GetActorRotation();
	const FRotator TargetRot(0.f, ToTarget.Rotation().Yaw, 0.f);
	const FRotator NewRot = FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, 12.f);
	AIController->SetControlRotation(NewRot);
	AIController->SetFocus(TargetActor);

	// AttackTask가 폰 회전을 직접 덮어쓰면 Chase/Movement와 충돌하므로
	// 여기서는 Controller/Focus만 갱신하고 폰 회전은 CharacterMovement에 맡긴다.
	const float YawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentRot.Yaw, TargetRot.Yaw));
	if (YawDelta > 2.f)
		Pawn->ForceNetUpdate();
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

	APawn* Pawn = InstanceData.AIController->GetPawn();
	if (UCharacterMovementComponent* MoveComp = Pawn ? Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent()) : nullptr)
	{
		MoveComp->bOrientRotationToMovement     = false;
		MoveComp->bUseControllerDesiredRotation = true;
	}

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

		if (bIsSpaceShip)
		{
			if (USpaceShipAttackSlotManager* SlotManager = HellunaAttackTarget::GetShipSlotManager(TargetActor))
			{
				int32 SlotIndex = INDEX_NONE;
				ESlotState SlotState = ESlotState::Free;
				if (SlotManager->GetMonsterSlotInfo(Pawn, SlotIndex, SlotState) && SlotState != ESlotState::Occupied)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[enemybugreport][AttackBlockedPendingSlot] Enemy=%s Slot=%d SlotState=%s Target=%s"),
						*GetNameSafe(Pawn),
						SlotIndex,
						HellunaAttackTarget::SlotStateToString(SlotState),
						*GetNameSafe(TargetActor));
					return EStateTreeRunStatus::Failed;
				}
			}
		}

		HellunaAttackTarget::FaceCurrentTarget(InstanceData.AIController, Pawn, TargetActor, 1.f / 60.f);
	}

	InstanceData.CooldownRemaining = InitialAttackDelay;
	InstanceData.MovementDiagTimer = 0.f;

	if (TargetData.HasValidTarget())
	{
		AActor* TargetActor = TargetData.TargetActor.Get();
		const FVector ToTarget = (TargetActor->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
		const float DesiredYaw = ToTarget.IsNearlyZero() ? Pawn->GetActorRotation().Yaw : ToTarget.Rotation().Yaw;
		UE_LOG(LogTemp, Warning,
			TEXT("[enemybugreport][AttackEnter] Enemy=%s Target=%s PawnRot=%s ControlRot=%s DesiredYaw=%.2f InitialDelay=%.2f"),
			*GetNameSafe(Pawn),
			*GetNameSafe(TargetActor),
			*Pawn->GetActorRotation().ToCompactString(),
			*InstanceData.AIController->GetControlRotation().ToCompactString(),
			DesiredYaw,
			InitialAttackDelay);
	}

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

	// 사용할 GA 클래스: 에디터에서 선택한 클래스, 없으면 기본 클래스 사용
	TSubclassOf<UHellunaEnemyGameplayAbility> GAClass = AttackAbilityClass.Get()
		? AttackAbilityClass
		: TSubclassOf<UHellunaEnemyGameplayAbility>(UHellunaEnemyGameplayAbility::StaticClass());

	const FHellunaAITargetData& TargetData = InstanceData.TargetData;
	InstanceData.MovementDiagTimer -= DeltaTime;

	// ① GA 활성 중(몽타주 재생 + AttackRecoveryDelay)이면 아무것도 하지 않음
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetClass() == GAClass)
		{
			if (Spec.IsActive())
			{
				if (TargetData.HasValidTarget() && InstanceData.MovementDiagTimer <= 0.f)
				{
					const FVector ToTarget = (TargetData.TargetActor->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
					const float DesiredYaw = ToTarget.IsNearlyZero() ? Pawn->GetActorRotation().Yaw : ToTarget.Rotation().Yaw;
					const float YawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(Pawn->GetActorRotation().Yaw, DesiredYaw));
					UE_LOG(LogTemp, Log,
						TEXT("[enemybugreport][AttackTick] Enemy=%s Phase=ActiveGA Target=%s PawnRot=%s ControlRot=%s DesiredYaw=%.2f YawDelta=%.2f Focus=%s"),
						*GetNameSafe(Pawn),
						*GetNameSafe(TargetData.TargetActor.Get()),
						*Pawn->GetActorRotation().ToCompactString(),
						*AIController->GetControlRotation().ToCompactString(),
						DesiredYaw,
						YawDelta,
						*GetNameSafe(AIController->GetFocusActor()));
					InstanceData.MovementDiagTimer = 0.5f;
				}

				if (TargetData.HasValidTarget())
				{
					HellunaAttackTarget::FaceCurrentTarget(AIController, Pawn, TargetData.TargetActor.Get(), DeltaTime);
				}
				return EStateTreeRunStatus::Running;
			}
			break;
		}
	}

	// ② GA 종료 후 쿨다운 대기 중
	if (InstanceData.CooldownRemaining > 0.f)
	{
		InstanceData.CooldownRemaining -= DeltaTime;

		if (TargetData.HasValidTarget() && InstanceData.MovementDiagTimer <= 0.f)
		{
			const FVector ToTarget = (TargetData.TargetActor->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
			const float DesiredYaw = ToTarget.IsNearlyZero() ? Pawn->GetActorRotation().Yaw : ToTarget.Rotation().Yaw;
			const float YawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(Pawn->GetActorRotation().Yaw, DesiredYaw));
			UE_LOG(LogTemp, Log,
				TEXT("[enemybugreport][AttackTick] Enemy=%s Phase=Cooldown Target=%s PawnRot=%s ControlRot=%s DesiredYaw=%.2f YawDelta=%.2f Cooldown=%.2f Focus=%s"),
				*GetNameSafe(Pawn),
				*GetNameSafe(TargetData.TargetActor.Get()),
				*Pawn->GetActorRotation().ToCompactString(),
				*AIController->GetControlRotation().ToCompactString(),
				DesiredYaw,
				YawDelta,
				InstanceData.CooldownRemaining,
				*GetNameSafe(AIController->GetFocusActor()));
			InstanceData.MovementDiagTimer = 0.5f;
		}

		if (TargetData.HasValidTarget())
		{
			HellunaAttackTarget::FaceCurrentTarget(AIController, Pawn, TargetData.TargetActor.Get(), DeltaTime);
		}

		return EStateTreeRunStatus::Running;
	}

	if (TargetData.HasValidTarget())
	{
		HellunaAttackTarget::FaceCurrentTarget(AIController, Pawn, TargetData.TargetActor.Get(), DeltaTime);
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

	// TryActivate 이전에 CurrentTarget 설정 (GA ActivateAbility 내부에서 즉시 참조)
	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (!Spec.Ability || Spec.Ability->GetClass() != GAClass) continue;

		if (UEnemyGameplayAbility_Attack* AttackGA = Cast<UEnemyGameplayAbility_Attack>(Spec.Ability))
			AttackGA->CurrentTarget = TargetData.TargetActor.Get();
		else if (UEnemyGameplayAbility_RangedAttack* RangedGA = Cast<UEnemyGameplayAbility_RangedAttack>(Spec.Ability))
			RangedGA->CurrentTarget = TargetData.TargetActor.Get();

		break;
	}

	const bool bActivated = ASC->TryActivateAbilityByClass(GAClass);
	if (bActivated)
	{
		const float CooldownMultiplier = Enemy->bEnraged ? Enemy->EnrageCooldownMultiplier : 1.f;
		InstanceData.CooldownRemaining = AttackCooldown * CooldownMultiplier;
		UE_LOG(LogTemp, Warning,
			TEXT("[enemybugreport][AttackActivate] Enemy=%s Target=%s Cooldown=%.2f PawnRot=%s ControlRot=%s Focus=%s"),
			*GetNameSafe(Pawn),
			*GetNameSafe(TargetData.TargetActor.Get()),
			InstanceData.CooldownRemaining,
			*Pawn->GetActorRotation().ToCompactString(),
			*AIController->GetControlRotation().ToCompactString(),
			*GetNameSafe(AIController->GetFocusActor()));
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
		if (TargetData.HasValidTarget() && !bKeepShipFocus)
		{
			if (AActor* TargetActor = TargetData.TargetActor.Get())
			{
				if (Cast<AResourceUsingObject_SpaceShip>(TargetActor))
				{
					if (USpaceShipAttackSlotManager* SlotManager = HellunaAttackTarget::GetShipSlotManager(TargetActor))
					{
						if (APawn* Pawn = AIC->GetPawn())
						{
							SlotManager->ReleaseSlot(Pawn);
						}
					}
				}
			}
		}

		UE_LOG(LogTemp, Warning,
			TEXT("[enemybugreport][AttackExit] Enemy=%s Focus=%s PawnRot=%s ControlRot=%s"),
			*GetNameSafe(AIC->GetPawn()),
			*GetNameSafe(AIC->GetFocusActor()),
			AIC->GetPawn() ? *AIC->GetPawn()->GetActorRotation().ToCompactString() : TEXT("None"),
			*AIC->GetControlRotation().ToCompactString());
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
