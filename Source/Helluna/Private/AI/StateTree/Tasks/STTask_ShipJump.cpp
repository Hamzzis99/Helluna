/**
 * STTask_ShipJump.cpp
 *
 * [ShipTopV1] 상단 점프 분기 Task.
 */

#include "AI/StateTree/Tasks/STTask_ShipJump.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "AI/SpaceShipAttackSlotManager.h"
#include "Character/HellunaEnemyCharacter.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_ShipJump.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"

namespace ShipJumpLocal
{
static USpaceShipAttackSlotManager* GetSlotManager(AActor* ShipActor)
{
	return ShipActor ? ShipActor->FindComponentByClass<USpaceShipAttackSlotManager>() : nullptr;
}

static void SnapFaceShip(AAIController* AIC, APawn* Pawn, AActor* Ship)
{
	if (!AIC || !Pawn || !Ship) return;
	const FVector ToShip = (Ship->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
	if (ToShip.IsNearlyZero()) return;
	const FRotator SnapRot(0.f, ToShip.Rotation().Yaw, 0.f);
	AIC->SetControlRotation(SnapRot);
	Pawn->SetActorRotation(SnapRot);
}
}

EStateTreeRunStatus FSTTask_ShipJump::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	AAIController* AIC = Data.AIController;
	if (!AIC) return EStateTreeRunStatus::Failed;

	APawn* Pawn = AIC->GetPawn();
	if (!Pawn) return EStateTreeRunStatus::Failed;

	AActor* ShipActor = Data.TargetData.TargetActor.Get();
	if (!Cast<AResourceUsingObject_SpaceShip>(ShipActor))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ShipTopV1] EnterState Failed — target is not spaceship"));
		return EStateTreeRunStatus::Failed;
	}

	USpaceShipAttackSlotManager* SlotMgr = ShipJumpLocal::GetSlotManager(ShipActor);
	if (!SlotMgr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ShipTopV1] EnterState Failed — SlotManager missing on ship"));
		return EStateTreeRunStatus::Failed;
	}

	if (!SlotMgr->TryReserveTopSlot(Pawn))
	{
		// 슬롯이 가득 찼음 → 이 Task는 실패시켜 State Tree의 OnFailed 분기(일반 공격)로 보냄.
		return EStateTreeRunStatus::Failed;
	}
	Data.bReservedTopSlot = true;
	Data.bActivatedGA = false;
	Data.PostLandingTimer = 0.f;

	// 이동 정지 + 우주선 스냅.
	AIC->StopMovement();
	AIC->ClearFocus(EAIFocusPriority::Gameplay);
	if (UCharacterMovementComponent* CMC = Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent()))
	{
		CMC->bOrientRotationToMovement     = false;
		CMC->bUseControllerDesiredRotation = true;
	}
	ShipJumpLocal::SnapFaceShip(AIC, Pawn, ShipActor);

	UE_LOG(LogTemp, Warning, TEXT("[ShipTopV1] Enter — Monster=%s reserved slot, preparing jump"),
		*Pawn->GetName());

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_ShipJump::Tick(
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

	UHellunaAbilitySystemComponent* ASC = Cast<UHellunaAbilitySystemComponent>(
		Enemy->GetAbilitySystemComponent());
	if (!ASC) return EStateTreeRunStatus::Failed;

	AActor* ShipActor = Data.TargetData.TargetActor.Get();
	if (!ShipActor) return EStateTreeRunStatus::Failed;

	// 점프 GA 클래스 미설정 시 바로 Succeeded (State Tree에서 다음 분기로).
	TSubclassOf<UHellunaEnemyGameplayAbility> GAClass = JumpAbilityClass;
	if (!GAClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ShipTopV1] JumpAbilityClass not set — skipping"));
		return EStateTreeRunStatus::Succeeded;
	}

	// ① 첫 Tick에서 GA 1회 발동.
	if (!Data.bActivatedGA)
	{
		// 어빌리티 보장(없으면 GiveAbility).
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

		// CurrentTarget 주입(CDO + Instance 양쪽).
		for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (!Spec.Ability || Spec.Ability->GetClass() != GAClass) continue;
			if (UEnemyGameplayAbility_ShipJump* Cdo = Cast<UEnemyGameplayAbility_ShipJump>(Spec.Ability))
				Cdo->CurrentTarget = ShipActor;
			if (UGameplayAbility* Inst = Spec.GetPrimaryInstance())
			{
				if (UEnemyGameplayAbility_ShipJump* InstJump = Cast<UEnemyGameplayAbility_ShipJump>(Inst))
					InstJump->CurrentTarget = ShipActor;
			}
			break;
		}

		ShipJumpLocal::SnapFaceShip(AIC, Pawn, ShipActor);

		const bool bActivated = ASC->TryActivateAbilityByClass(GAClass);
		Data.bActivatedGA = true;
		UE_LOG(LogTemp, Warning, TEXT("[ShipTopV1] ActivateJumpGA Monster=%s Activated=%d"),
			*Pawn->GetName(), bActivated ? 1 : 0);

		if (!bActivated)
		{
			return EStateTreeRunStatus::Failed;
		}
		return EStateTreeRunStatus::Running;
	}

	// ② GA가 끝날 때까지 대기.
	bool bGAActive = false;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetClass() == GAClass)
		{
			bGAActive = Spec.IsActive();
			break;
		}
	}

	if (bGAActive)
	{
		return EStateTreeRunStatus::Running;
	}

	// ③ GA 종료 → 착지 후 유지 시간 카운트 후 Succeeded.
	Data.PostLandingTimer += DeltaTime;
	if (Data.PostLandingTimer >= PostLandingHoldTime)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ShipTopV1] Jump complete Monster=%s → Succeeded"),
			*Pawn->GetName());
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_ShipJump::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (bReleaseSlotOnExit && Data.bReservedTopSlot)
	{
		if (AAIController* AIC = Data.AIController)
		{
			if (APawn* Pawn = AIC->GetPawn())
			{
				AActor* ShipActor = Data.TargetData.TargetActor.Get();
				if (USpaceShipAttackSlotManager* SlotMgr = ShipJumpLocal::GetSlotManager(ShipActor))
				{
					SlotMgr->ReleaseTopSlot(Pawn);
				}
			}
		}
		Data.bReservedTopSlot = false;
	}
}
