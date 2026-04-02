// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_Attack.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameplayTagContainer.h"

#include "Character/HellunaEnemyCharacter.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"

namespace HellunaEnemyAttackUtils
{
static FGameplayTag GetAttackingStateTag()
{
	return FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking"), false);
}

static AActor* ResolveAttackTarget(const AHellunaEnemyCharacter* Enemy, AActor* TargetActor)
{
	if (TargetActor || !Enemy)
	{
		return TargetActor;
	}

	UWorld* World = Enemy->GetWorld();
	AHellunaDefenseGameState* DefenseGameState = World ? World->GetGameState<AHellunaDefenseGameState>() : nullptr;
	return DefenseGameState ? DefenseGameState->GetSpaceShip() : nullptr;
}
}

UEnemyGameplayAbility_Attack::UEnemyGameplayAbility_Attack()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UEnemyGameplayAbility_Attack::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 공격 중 상태 태그 추가 (이동 방지)
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (ASC)
	{
		FGameplayTagContainer AttackingTag;
		AttackingTag.AddTag(HellunaEnemyAttackUtils::GetAttackingStateTag());
		ASC->AddLooseGameplayTags(AttackingTag);
	}

	Enemy->SetCachedMeleeAttackDamage(AttackDamage);

	// 건패링 윈도우 열기 (bOpensParryWindow + bCanBeParried 체크는 내부에서 처리)
	TryOpenParryWindow();

	// TODO [Step 12] 패링 힌트 이펙트
	// 패링 윈도우가 열릴 때 Niagara 이펙트 스폰
	// 게임 옵션에서 On/Off 토글 가능하게
	// Enemy->Multicast_PlayParryHintVFX() 같은 함수로 처리 예정

	UAnimMontage* AttackMontage = Enemy->AttackMontage;
	if (!AttackMontage)
	{
		HandleAttackFinished();
		return;
	}
	AActor* AttackTarget = HellunaEnemyAttackUtils::ResolveAttackTarget(Enemy, CurrentTarget.Get());
	Enemy->LockMovementAndFaceTarget(AttackTarget);
	
	// 광폭화 상태이면 Enemy에 설정된 EnrageAttackMontagePlayRate 배율로 공격 애니메이션을 빠르게 재생
	const float PlayRate = Enemy->bEnraged ? Enemy->EnrageAttackMontagePlayRate : 1.f;
	const FVector ToTarget = AttackTarget
		? (AttackTarget->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal2D()
		: FVector::ForwardVector;
	const FRotator FacingRotation = ToTarget.IsNearlyZero()
		? Enemy->GetActorRotation()
		: FRotator(0.f, ToTarget.Rotation().Yaw, 0.f);

	Enemy->Multicast_PlayAttackMontage(AttackMontage, PlayRate, FacingRotation);

	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, AttackMontage, PlayRate, NAME_None, false
	);

	if (!MontageTask)
	{
		HandleAttackFinished();
		return;
	}

	MontageTask->OnCompleted.AddDynamic(this, &UEnemyGameplayAbility_Attack::OnMontageCompleted);
	MontageTask->OnCancelled.AddDynamic(this, &UEnemyGameplayAbility_Attack::OnMontageCancelled);
	MontageTask->OnInterrupted.AddDynamic(this, &UEnemyGameplayAbility_Attack::OnMontageCancelled);

	MontageTask->ReadyForActivation();
}

void UEnemyGameplayAbility_Attack::OnMontageCompleted()
{
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		HandleAttackFinished();
		return;
	}

	UWorld* World = Enemy->GetWorld();
	if (!World)
	{
		HandleAttackFinished();
		return;
	}

	// 몽타주 완료 후 AttackRecoveryDelay 동안 대기
	// 이 대기 시간 동안 RotationRate를 PostAttackRotationRate로 높여서
	// 타겟 방향으로 빠르게 회전하게 한다.
	// 대기가 끝나면 원래 RotationRate로 복원 후 이동 잠금 해제.
	UCharacterMovementComponent* MoveComp = Enemy->GetCharacterMovement();
	if (MoveComp && PostAttackRotationRate > 0.f)
	{
		// 원래 RotationRate 저장 후 빠른 회전 속도 적용
		SavedRotationRate = MoveComp->RotationRate;
		MoveComp->RotationRate = FRotator(0.f, PostAttackRotationRate, 0.f);
	}

	World->GetTimerManager().SetTimer(
		DelayedReleaseTimerHandle,
		[this]()
		{
			AHellunaEnemyCharacter* DelayedEnemy = GetEnemyCharacterFromActorInfo();
			if (!DelayedEnemy)
			{
				HandleAttackFinished();
				return;
			}

			// RotationRate 원복
			if (UCharacterMovementComponent* MoveComp = DelayedEnemy->GetCharacterMovement())
			{
				if (PostAttackRotationRate > 0.f)
				{
					MoveComp->RotationRate = SavedRotationRate;
				}
			}

			DelayedEnemy->UnlockMovement();
			HandleAttackFinished();
		},
		AttackRecoveryDelay,
		false
	);
}

void UEnemyGameplayAbility_Attack::OnMontageCancelled()
{
	// 취소 시에도 RotationRate 복원
	if (AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo())
	{
		if (UCharacterMovementComponent* MoveComp = Enemy->GetCharacterMovement())
		{
			if (PostAttackRotationRate > 0.f)
			{
				MoveComp->RotationRate = SavedRotationRate;
			}
		}
	}

	const FGameplayAbilitySpecHandle Handle = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo ActivationInfo = GetCurrentActivationInfo();
	EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
}

void UEnemyGameplayAbility_Attack::HandleAttackFinished()
{
	// 데미지는 AnimNotify 콜리전 시스템이 처리
	// GA는 몽타주 재생과 상태 관리만 담당
	
	const FGameplayAbilitySpecHandle Handle = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo ActivationInfo = GetCurrentActivationInfo();
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UEnemyGameplayAbility_Attack::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo())
	{
		Enemy->UnlockMovement();
		Enemy->SetCachedMeleeAttackDamage(0.f);

		if (bWasCancelled)
		{
			if (UWorld* World = Enemy->GetWorld())
			{
				World->GetTimerManager().ClearTimer(DelayedReleaseTimerHandle);
			}
		}
	}

	// 건패링 윈도우 정리 (안전장치)
	CloseParryWindow();

	// ★ State.Enemy.Attacking 태그 제거
	// 이 태그가 제거되어야 StateTree가 Attack → Run(Chase) 전환을 허용한다.
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer AttackingTag;
		AttackingTag.AddTag(HellunaEnemyAttackUtils::GetAttackingStateTag());
		ASC->RemoveLooseGameplayTags(AttackingTag);
	}

	CurrentTarget = nullptr;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
