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
	// [HitVFXV1] 타격 판정 시 스폰할 VFX를 캐릭터에 캐싱
	Enemy->SetCachedHitVFX(HitVFX, HitVFXScale);

	// [HitVFX.Diag] GA 발동 시점에 HitVFX 가 실제로 설정되어 있는지 확인
	UE_LOG(LogTemp, Warning,
		TEXT("[HitVFX.Diag][GA.Activate] Monster=%s HitVFX=%s Scale=%.2f"),
		*Enemy->GetName(),
		HitVFX ? *HitVFX->GetName() : TEXT("NULL(assign in BP!)"),
		HitVFXScale);

	// 건패링 윈도우 열기 (bOpensParryWindow + bCanBeParried 체크는 내부에서 처리)
	TryOpenParryWindow();

	// TODO [Step 12] 패링 힌트 이펙트
	// 패링 윈도우가 열릴 때 Niagara 이펙트 스폰
	// 게임 옵션에서 On/Off 토글 가능하게
	// Enemy->Multicast_PlayParryHintVFX() 같은 함수로 처리 예정

	UAnimMontage* EffectiveMontage = GetEffectiveAttackMontage();
	if (!EffectiveMontage)
	{
		HandleAttackFinished();
		return;
	}
	AActor* AttackTarget = HellunaEnemyAttackUtils::ResolveAttackTarget(Enemy, CurrentTarget.Get());

	// 광폭화 상태이면 Enemy에 설정된 EnrageAttackMontagePlayRate 배율로 공격 애니메이션을 빠르게 재생
	const float PlayRate = Enemy->bEnraged ? Enemy->EnrageAttackMontagePlayRate : 1.f;
	// [EnrageDiag] 광폭화 반영 검증용 — BP의 PlayRate override/flag 전달 여부 확인.
	UE_LOG(LogTemp, Warning,
		TEXT("[EnrageDiag][Attack.GA] Monster=%s bEnraged=%d EnragedRate=%.2f → FinalPlayRate=%.2f Montage=%s"),
		*Enemy->GetName(),
		Enemy->bEnraged ? 1 : 0,
		Enemy->EnrageAttackMontagePlayRate,
		PlayRate,
		EffectiveMontage ? *EffectiveMontage->GetName() : TEXT("NULL"));
	const FVector ToTarget = AttackTarget
		? (AttackTarget->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal2D()
		: FVector::ForwardVector;
	const FRotator FacingRotation = ToTarget.IsNearlyZero()
		? Enemy->GetActorRotation()
		: FRotator(0.f, ToTarget.Rotation().Yaw, 0.f);

	Enemy->LockMovementAndFaceTarget(AttackTarget);

	// 파라노이드 스냅: Multicast 직전에 서버 회전을 FacingRotation으로 강제 재설정.
	// LockMovement과 Multicast 사이에 틱/물리가 끼어들어 회전이 밀릴 가능성 차단.
	Enemy->SetActorRotation(FacingRotation, ETeleportType::TeleportPhysics);

	// [ServerAnimTick] 서버는 기본적으로 SkelMesh Pose Tick 이 꺼져있어 Montage Notify
	// (AttackCollisionStart/End) 가 서버에서 발동되지 않음 → 서버가 박스 활성화 못 함 → Overlap 판정 0.
	// Montage 재생 직전 서버 Pose Tick 을 켜서 공격 구간 동안만 Notify 가 서버에 도달하도록.
	// OnMontageCompleted/Cancelled 에서 다시 끈다 (최적화 — 공격 안 할 때 서버 Pose Tick 비용 0).
	Enemy->SetServerAttackPoseTickEnabled(true);

	Enemy->Multicast_PlayAttackMontage(EffectiveMontage, PlayRate, FacingRotation);
	PlayAttackSound();

	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, EffectiveMontage, PlayRate, NAME_None, false
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

		// [ServerAnimTick] 안전망 — GA 종료 시점에 항상 서버 Pose Tick 복원.
		// Notify End 가 누락되거나 Montage 취소로 SetAttackBoxActive(false) 가 안 불렸을 때도
		// 서버 Pose Tick 이 계속 켜져있는 일이 없도록.
		Enemy->SetServerAttackPoseTickEnabled(false);

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
