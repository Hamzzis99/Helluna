// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_Attack.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameplayTagContainer.h"

#include "Character/HellunaEnemyCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

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
		AttackingTag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->AddLooseGameplayTags(AttackingTag);
	}

	UAnimMontage* AttackMontage = Enemy->AttackMontage;
	if (!AttackMontage)
	{
		HandleAttackFinished();
		return;
	}
	Enemy->SetServerAttackPoseTickEnabled(true);

	// ★ 몽타주 시작 즉시 이동만 잠금 (nullptr 전달 → 회전 없음)
	// 회전은 몽타주가 완전히 끝난 OnMontageCompleted에서 처리한다.
	Enemy->LockMovementAndFaceTarget(nullptr);
	
	// 광폭화 상태이면 Enemy에 설정된 EnrageAttackMontagePlayRate 배율로 공격 애니메이션을 빠르게 재생
	const float PlayRate = Enemy->bEnraged ? Enemy->EnrageAttackMontagePlayRate : 1.f;

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
		Enemy->SetServerAttackPoseTickEnabled(false);
		Enemy->UnlockMovement();

		if (bWasCancelled)
		{
			if (UWorld* World = Enemy->GetWorld())
			{
				World->GetTimerManager().ClearTimer(DelayedReleaseTimerHandle);
			}
		}
	}

	// ★ State.Enemy.Attacking 태그 제거
	// 이 태그가 제거되어야 StateTree가 Attack → Run(Chase) 전환을 허용한다.
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer AttackingTag;
		AttackingTag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->RemoveLooseGameplayTags(AttackingTag);
	}

	CurrentTarget = nullptr;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
