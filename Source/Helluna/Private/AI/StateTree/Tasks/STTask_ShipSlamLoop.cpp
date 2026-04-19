/**
 * STTask_ShipSlamLoop.cpp
 *
 * [ShipSlamV1] 우주선 상단 내려찍기 루프 Task.
 *
 * Tick 흐름:
 *  ① 우주선 이탈 검사 (ShipCheckInterval 주기, 매 틱 X)
 *     - CMC MovementMode == Falling 2회 연속 감지 → OnShip 태그 제거 + Failed 리턴
 *  ② 몬타주 재생 중 → Running (회전 생략)
 *  ③ 쿨다운 감소 + 타겟 방향 회전
 *  ④ 쿨다운 0 + GA 비활성 → SlamAbilityClass 발동
 */

#include "AI/StateTree/Tasks/STTask_ShipSlamLoop.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameplayTagContainer.h"

#include "Character/HellunaEnemyCharacter.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_Attack.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_ShipJump.h"

namespace ShipSlamLocal
{
static void FaceTarget(AAIController* AIC, APawn* Pawn, AActor* Target, float DeltaTime, float Speed)
{
	if (!AIC || !Pawn || !Target) return;
	const FVector ToTarget = (Target->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero()) return;

	const FRotator CurrentRot = AIC->GetControlRotation();
	const FRotator TargetRot(0.f, ToTarget.Rotation().Yaw, 0.f);
	const float YawDeltaDeg = FMath::Abs(FRotator::NormalizeAxis(TargetRot.Yaw - CurrentRot.Yaw));

	const FRotator NewRot = (YawDeltaDeg < 1.0f)
		? TargetRot
		: FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, Speed);

	AIC->SetControlRotation(NewRot);
	Pawn->SetActorRotation(NewRot);
}

static void SnapFaceTarget(AAIController* AIC, APawn* Pawn, AActor* Target)
{
	if (!AIC || !Pawn || !Target) return;
	const FVector ToTarget = (Target->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero()) return;
	const FRotator SnapRot(0.f, ToTarget.Rotation().Yaw, 0.f);
	AIC->SetControlRotation(SnapRot);
	Pawn->SetActorRotation(SnapRot);
}

static bool DetectFellOffShip(ACharacter* Character, int32& FallStreak)
{
	if (!Character) return true;
	UCharacterMovementComponent* CMC = Character->GetCharacterMovement();
	if (!CMC) return false;

	if (CMC->MovementMode == MOVE_Falling)
	{
		++FallStreak;
		if (FallStreak >= 2)
		{
			return true;
		}
		return false;
	}

	FallStreak = 0;
	return false;
}

static void RemoveOnShipTag(UAbilitySystemComponent* ASC)
{
	if (!ASC) return;
	const FGameplayTag OnShipTag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.OnShip"), false);
	if (!OnShipTag.IsValid()) return;
	FGameplayTagContainer Container;
	Container.AddTag(OnShipTag);
	ASC->RemoveLooseGameplayTags(Container);
}
}

EStateTreeRunStatus FSTTask_ShipSlamLoop::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	AAIController* AIC = Data.AIController;
	if (!AIC) return EStateTreeRunStatus::Failed;

	AIC->StopMovement();
	AIC->ClearFocus(EAIFocusPriority::Gameplay);

	APawn* Pawn = AIC->GetPawn();
	if (UCharacterMovementComponent* CMC = Pawn ? Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent()) : nullptr)
	{
		CMC->bOrientRotationToMovement = false;
		CMC->bUseControllerDesiredRotation = true;
	}

	Data.CooldownRemaining = InitialAttackDelay;
	Data.FallDetectStreak = 0;
	Data.NextShipCheckTime = 0.f;

	AActor* EnterTarget = Data.TargetData.TargetActor.Get();
	if (EnterTarget)
	{
		ShipSlamLocal::SnapFaceTarget(AIC, Pawn, EnterTarget);
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_ShipSlamLoop::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	AAIController* AIC = Data.AIController;
	if (!AIC) return EStateTreeRunStatus::Failed;

	APawn* Pawn = AIC->GetPawn();
	if (!Pawn) return EStateTreeRunStatus::Failed;

	AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Pawn);
	if (!Enemy) return EStateTreeRunStatus::Failed;

	UWorld* World = Pawn->GetWorld();
	if (!World) return EStateTreeRunStatus::Failed;

	UHellunaAbilitySystemComponent* ASC = Cast<UHellunaAbilitySystemComponent>(Enemy->GetAbilitySystemComponent());
	if (!ASC) return EStateTreeRunStatus::Failed;

	// ① 우주선 이탈 검사 (ShipCheckInterval 주기)
	const float Now = World->GetTimeSeconds();
	if (Now >= Data.NextShipCheckTime)
	{
		Data.NextShipCheckTime = Now + ShipCheckInterval;
		if (ShipSlamLocal::DetectFellOffShip(Enemy, Data.FallDetectStreak))
		{
			ShipSlamLocal::RemoveOnShipTag(ASC);
			return EStateTreeRunStatus::Failed;
		}
	}

	AIC->ClearFocus(EAIFocusPriority::Gameplay);

	// ② 몬타주 재생 중이면 회전 생략
	bool bMontagePlaying = false;
	if (USkeletalMeshComponent* Mesh = Enemy->GetMesh())
	{
		if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
		{
			UAnimMontage* ActiveMontage = AnimInst->GetCurrentActiveMontage();
			if (ActiveMontage && AnimInst->Montage_IsPlaying(ActiveMontage))
			{
				bMontagePlaying = true;
			}
		}
	}
	if (bMontagePlaying)
	{
		return EStateTreeRunStatus::Running;
	}

	if (UCharacterMovementComponent* CMC = Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent()))
	{
		CMC->bOrientRotationToMovement = false;
		CMC->bUseControllerDesiredRotation = true;
	}

	TSubclassOf<UHellunaEnemyGameplayAbility> GAClass = SlamAbilityClass.Get()
		? SlamAbilityClass
		: TSubclassOf<UHellunaEnemyGameplayAbility>(UHellunaEnemyGameplayAbility::StaticClass());

	bool bGAActive = false;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetClass() == GAClass)
		{
			bGAActive = Spec.IsActive();
			break;
		}
	}

	// ③ 쿨다운 감소 + 회전
	const FHellunaAITargetData& TargetData = Data.TargetData;
	if (Data.CooldownRemaining > 0.f)
	{
		Data.CooldownRemaining -= DeltaTime;
		if (TargetData.HasValidTarget())
		{
			ShipSlamLocal::FaceTarget(AIC, Pawn, TargetData.TargetActor.Get(), DeltaTime, RotationSpeed);
		}
		return EStateTreeRunStatus::Running;
	}

	if (TargetData.HasValidTarget())
	{
		ShipSlamLocal::FaceTarget(AIC, Pawn, TargetData.TargetActor.Get(), DeltaTime, RotationSpeed);
	}

	if (bGAActive)
	{
		return EStateTreeRunStatus::Running;
	}

	// ④ 쿨다운 완료 → GA 발동
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

	// [ShipSlamV2.TargetInject] GA 발동 직전 CurrentTarget 주입 (CDO + Instance 양쪽).
	AActor* ChosenTarget = TargetData.TargetActor.Get();
	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (!Spec.Ability || Spec.Ability->GetClass() != GAClass) continue;
		if (UEnemyGameplayAbility_Attack* CdoAttack = Cast<UEnemyGameplayAbility_Attack>(Spec.Ability))
			CdoAttack->CurrentTarget = ChosenTarget;
		if (UGameplayAbility* Instance = Spec.GetPrimaryInstance())
		{
			if (UEnemyGameplayAbility_Attack* InstAttack = Cast<UEnemyGameplayAbility_Attack>(Instance))
				InstAttack->CurrentTarget = ChosenTarget;
		}
		break;
	}

	if (ChosenTarget)
	{
		ShipSlamLocal::SnapFaceTarget(AIC, Pawn, ChosenTarget);
	}

	const bool bActivated = ASC->TryActivateAbilityByClass(GAClass);
	if (bActivated)
	{
		const float CooldownMultiplier = Enemy->bEnraged ? Enemy->EnrageCooldownMultiplier : 1.f;
		Data.CooldownRemaining = AttackCooldown * CooldownMultiplier;
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_ShipSlamLoop::ExitState(
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
				CMC->bOrientRotationToMovement = true;
				CMC->bUseControllerDesiredRotation = false;
			}
		}
	}
}
