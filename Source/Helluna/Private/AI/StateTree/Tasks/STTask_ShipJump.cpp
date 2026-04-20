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
#include "AbilitySystemBlueprintLibrary.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "GameplayTagContainer.h"

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

	// [ShipJumpFailV1] 이전 점프에서 상단 착지 실패한 몬스터는 재점프 차단.
	if (AHellunaEnemyCharacter* Enemy0 = Cast<AHellunaEnemyCharacter>(Pawn))
	{
		if (UAbilitySystemComponent* ASC0 = Enemy0->GetAbilitySystemComponent())
		{
			const FGameplayTag FailTag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.ShipJumpFailed"), false);
			if (FailTag.IsValid() && ASC0->HasMatchingGameplayTag(FailTag))
			{
				return EStateTreeRunStatus::Failed;
			}
		}
	}

	// [ShipJumpV3.OnShipGate] 이미 우주선에 올라탄 몬스터는 재점프 금지.
	// Why: 기울어진 우주선 탓에 착지 후에도 어택존과 겹쳐 StateTree 가 재점프 요청.
	//      OnLanded 에서 부여한 "State.Enemy.OnShip" 태그를 보고 우회 → StateTree 는
	//      Failed 받아 다음 분기(근접 공격)로 흘러감.
	if (AHellunaEnemyCharacter* EnemyChar = Cast<AHellunaEnemyCharacter>(Pawn))
	{
		if (UAbilitySystemComponent* ASC = EnemyChar->GetAbilitySystemComponent())
		{
			const FGameplayTag OnShipTag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.OnShip"), false);
			if (OnShipTag.IsValid() && ASC->HasMatchingGameplayTag(OnShipTag))
			{
				UE_LOG(LogTemp, Warning, TEXT("[ShipJumpV3.Gate] %s 이미 OnShip — 재점프 차단"),
					*Pawn->GetName());
				return EStateTreeRunStatus::Failed;
			}
		}
	}

	// [ShipJumpQuotaV1.Gate] 소환 시점에 점프 자격(State.Enemy.CanShipJump)이 부여된 몬스터만 점프.
	//   Why: SlotManager 실시간 추적은 OnShip 타이밍 경합으로 상한 뚫림이 반복됨.
	//        Pool ActivateActor 에서 GameMode 쿼터를 Consume 하여 N마리에만 태그 부여 →
	//        여기서 차단하면 "점프 가능 몬스터 수"가 소환 시점에 결정되어 예측 가능.
	//   How to apply: 태그 없으면 Failed — StateTree OnFailed 분기(일반 공격)로 흘러감.
	if (AHellunaEnemyCharacter* EligibilityChar = Cast<AHellunaEnemyCharacter>(Pawn))
	{
		if (UAbilitySystemComponent* EligibilityASC = EligibilityChar->GetAbilitySystemComponent())
		{
			const FGameplayTag CanJumpTag =
				FGameplayTag::RequestGameplayTag(FName("State.Enemy.CanShipJump"), false);
			if (CanJumpTag.IsValid() && !EligibilityASC->HasMatchingGameplayTag(CanJumpTag))
			{
				UE_LOG(LogTemp, Verbose,
					TEXT("[ShipJumpQuotaV1.Gate] %s CanShipJump 태그 없음 — 점프 스킵 (쿼터 미보유)"),
					*Pawn->GetName());
				return EStateTreeRunStatus::Failed;
			}
		}
	}

	// [ShipJumpV8.InAttackRangeGate] 이미 우주선 공격 범위 안이면 점프 없이 그 자리에서 공격.
	// Why: 지상에서 우주선을 타격 중이던 몬스터가 광폭화하면 StateTree 가 재평가되며 ShipJump
	//      으로 흘러와 불필요한 점프가 발생.
	// Signal: Evaluator 가 SpaceShipAttackRange 기반으로 매 틱 갱신하는 bAttackingSpaceShip.
	//      enraged 분기에서는 이 플래그를 덮어쓰지 않으므로 광폭화 직전 값이 그대로 유지 →
	//      "enrage 직전에 이미 공격 중이었다" 를 정확히 반영.
	if (Data.TargetData.bAttackingSpaceShip)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShipJumpV8.InAttackRangeGate] %s bAttackingSpaceShip=true — 점프 스킵 (지상 공격 유지)"),
			*Pawn->GetName());
		return EStateTreeRunStatus::Failed;
	}

	USpaceShipAttackSlotManager* SlotMgr = ShipJumpLocal::GetSlotManager(ShipActor);
	if (!SlotMgr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ShipTopV1] EnterState Failed — SlotManager missing on ship"));
		return EStateTreeRunStatus::Failed;
	}

	int32 AssignedIndex = INDEX_NONE;
	if (!SlotMgr->TryReserveTopSlotIndexed(Pawn, AssignedIndex))
	{
		// 슬롯이 가득 찼음 → 이 Task는 실패시켜 State Tree의 OnFailed 분기(일반 공격)로 보냄.
		return EStateTreeRunStatus::Failed;
	}
	Data.bReservedTopSlot = true;
	Data.bActivatedGA = false;
	Data.PostLandingTimer = 0.f;
	Data.StaggerElapsed = 0.f;
	Data.AssignedTopSlotIndex = AssignedIndex;

	// [ShipJumpStaggerV1] 몬스터별 무작위 지연 → 동시 LaunchCharacter 복제 분산.
	const float LoBound = FMath::Max(0.f, StaggerMin);
	const float HiBound = FMath::Max(LoBound, StaggerMax);
	Data.StaggerDelay = (HiBound > 0.f) ? FMath::FRandRange(LoBound, HiBound) : 0.f;

	// 이동 정지 + 우주선 스냅.
	AIC->StopMovement();
	AIC->ClearFocus(EAIFocusPriority::Gameplay);
	if (UCharacterMovementComponent* CMC = Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent()))
	{
		CMC->bOrientRotationToMovement     = false;
		CMC->bUseControllerDesiredRotation = true;
	}
	ShipJumpLocal::SnapFaceShip(AIC, Pawn, ShipActor);

	UE_LOG(LogTemp, Warning,
		TEXT("[ShipTopV1][StaggerV1] Enter — Monster=%s reserved slot, StaggerDelay=%.2f s"),
		*Pawn->GetName(), Data.StaggerDelay);

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

	// [ShipJumpStaggerV1] 스태거 지연 소비 — 동시 활성화 분산.
	if (!Data.bActivatedGA && Data.StaggerDelay > 0.f)
	{
		Data.StaggerElapsed += DeltaTime;
		if (Data.StaggerElapsed < Data.StaggerDelay)
		{
			return EStateTreeRunStatus::Running;
		}
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

		// CurrentTarget + 점프 튜닝 값 주입 (CDO + Instance 양쪽).
		const int32 SlotIdxToInject = Data.AssignedTopSlotIndex;
		auto ApplyTuning = [this, SlotIdxToInject](UEnemyGameplayAbility_ShipJump* JumpGA, AActor* Target)
		{
			if (!JumpGA) return;
			JumpGA->CurrentTarget           = Target;
			JumpGA->OvershootHeight         = OvershootHeight;
			JumpGA->AttackRecoveryDelay     = AttackRecoveryDelay;
			JumpGA->MaxAirborneTime         = MaxAirborneTime;
			JumpGA->FallbackHorizontalSpeed = FallbackHorizontalSpeed;
			JumpGA->FallbackVerticalSpeed   = FallbackVerticalSpeed;
			// [ShipJumpSpreadV1] 슬롯 인덱스 주입 → GA 의 부채꼴 yaw 오프셋에 사용.
			JumpGA->AssignedSlotIndex       = SlotIdxToInject;
		};

		for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (!Spec.Ability || Spec.Ability->GetClass() != GAClass) continue;
			ApplyTuning(Cast<UEnemyGameplayAbility_ShipJump>(Spec.Ability), ShipActor);
			if (UGameplayAbility* Inst = Spec.GetPrimaryInstance())
			{
				ApplyTuning(Cast<UEnemyGameplayAbility_ShipJump>(Inst), ShipActor);
			}
			break;
		}

		UE_LOG(LogTemp, Warning,
			TEXT("[ShipTopV1.Tune][ShipJumpSpreadV1] Inject Monster=%s SlotIdx=%d Overshoot=%.0f RecoveryDelay=%.2f MaxAir=%.1f"),
			*Pawn->GetName(), Data.AssignedTopSlotIndex, OvershootHeight, AttackRecoveryDelay, MaxAirborneTime);

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

	// ③ GA 종료 → 착지 후 유지 시간 카운트 후 OnShip 태그 유무로 Succeeded/Failed 분기.
	Data.PostLandingTimer += DeltaTime;
	if (Data.PostLandingTimer >= PostLandingHoldTime)
	{
		// [ShipJumpV6.OnShipGate] 실제 우주선에 착지해야만 Succeeded → Attackspaceship_jump_Rage.
		const FGameplayTag OnShipTag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.OnShip"), false);
		const bool bLandedOnShip = OnShipTag.IsValid() && ASC->HasMatchingGameplayTag(OnShipTag);
		UE_LOG(LogTemp, Warning,
			TEXT("[ShipJumpV6.TaskEnd] Monster=%s OnShip=%s → %s"),
			*Pawn->GetName(),
			bLandedOnShip ? TEXT("TRUE") : TEXT("FALSE"),
			bLandedOnShip ? TEXT("Succeeded") : TEXT("Failed"));
		return bLandedOnShip ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_ShipJump::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// [ShipTopSlotRetainV2] 몬스터가 "실제로 우주선 위에 있는 동안" 슬롯을 유지해야
	// 10마리 제한이 "동시 점프 수" 가 아니라 "동시 탑재 수" 로 의미를 가진다.
	// 기존: Task 종료 즉시 Release → 몬스터는 아직 위에 있는데 슬롯 반납 → 11, 12, 13... 탑재 가능.
	// 변경: OnShip 태그 보유 중이면 Release 보류. 내려간(태그 lost) 경우만 즉시 Release.
	//       Cleanup 주기 Tick 이 나중에 OnShip 태그 없는 슬롯 자동 회수.
	if (bReleaseSlotOnExit && Data.bReservedTopSlot)
	{
		if (AAIController* AIC = Data.AIController)
		{
			if (APawn* Pawn = AIC->GetPawn())
			{
				bool bStillOnShip = false;
				if (UAbilitySystemComponent* ASC =
					UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn))
				{
					const FGameplayTag OnShipTag =
						FGameplayTag::RequestGameplayTag(FName("State.Enemy.OnShip"), false);
					bStillOnShip = OnShipTag.IsValid() &&
						ASC->HasMatchingGameplayTag(OnShipTag);
				}

				if (!bStillOnShip)
				{
					// 점프 실패/중단 등 태그가 없는 경우 → 즉시 반납
					AActor* ShipActor = Data.TargetData.TargetActor.Get();
					if (USpaceShipAttackSlotManager* SlotMgr = ShipJumpLocal::GetSlotManager(ShipActor))
					{
						SlotMgr->ReleaseTopSlot(Pawn);
					}
					Data.bReservedTopSlot = false;
				}
				// OnShip 이면 슬롯 유지 → SlotManager::CleanupTopSlotReservations 가 회수
			}
		}
	}
}
