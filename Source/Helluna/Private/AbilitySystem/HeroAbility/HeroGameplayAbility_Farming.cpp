// Capstone Project Helluna
// [RevertInstantV1][LCv10] 즉시 회전 복귀 — 회전은 1프레임 스냅, 곧바로 몽타주 재생

#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Farming.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "Kismet/KismetMathLibrary.h"
#include "DrawDebugHelpers.h"
#include "Weapon/HellunaFarmingWeapon.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"

#include "Character/HellunaHeroCharacter.h"
#include "Character/HeroComponent/Helluna_FindResourceComponent.h"

#include "DebugHelper.h"

UHeroGameplayAbility_Farming::UHeroGameplayAbility_Farming()
{
	AbilityActivationPolicy = EHellunaAbilityActivationPolicy::OnTriggered;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	// ✅ ASC Release 로직이 이걸 보고 Cancel 해줌 (홀딩 방식)
	InputActionPolicy = EHellunaInputActionPolicy::Hold;

	bRetriggerInstancedAbility = false;
}

void UHeroGameplayAbility_Farming::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get());
	if (!Hero)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (ActorInfo->IsLocallyControlled())
	{
		Hero->LockMoveInput();
		Hero->LockLookInput();
	}

	Hero->PlayFullBody = true;
	CachedFarmingTarget = ResolveFarmingTarget(ActorInfo);

	// ✅ 첫 스윙 시작 (이후 OnFarmingFinished에서 반복)
	PlayFarmingMontage();
}

void UHeroGameplayAbility_Farming::PlayFarmingMontage()
{
	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(GetAvatarActorFromActorInfo());
	if (!Hero)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	// ✅ 타겟 검증
	AActor* Target = ResolveFarmingTarget(CurrentActorInfo);
	if (!Target || !IsTargetWithinFarmingRange(CurrentActorInfo, Target))
	{
		if (CurrentActorInfo && CurrentActorInfo->IsLocallyControlled())
		{
			Debug::Print(TEXT("주변에 채집 대상(포커스된 광물)이 없음"), FColor::Red);
		}
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
		return;
	}

	// ✅ 스냅 + 즉시 회전 (원래 동작)
	SnapHeroToFarmingDistance(CurrentActorInfo);
	FaceToTarget_InstantLocalOnly(CurrentActorInfo, Target->GetActorLocation());

	// [RevertInstantV1][LCv10] 라이브 코딩 적용 검증용 로그
	UE_LOG(LogTemp, Warning, TEXT("[RevertInstantV1][LCv10] PlayFarmingMontage: instant face → montage"));

	// ✅ 몽타주 가져오기
	UAnimMontage* FarmingMontage = nullptr;
	if (AHellunaHeroWeapon* CurrentWeapon = Cast<AHellunaHeroWeapon>(Hero->GetCurrentWeapon()))
	{
		FarmingMontage = CurrentWeapon->GetAnimSet().Attack;
	}

	if (!FarmingMontage)
	{
		Debug::Print(TEXT("[GA_Farming] FarmingMontage null (check weapon AnimSet)"), FColor::Red);
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	// ✅ 몽타주 재생 (AbilityTask)
	FarmingTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, FarmingMontage, 1.f);
	if (!FarmingTask)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	// [DmgSyncV1] OnBlendOut 중복 바인딩 제거 — 한 몽타주당 OnFarmingFinished가 두 번 호출되어
	// 타이밍이 밀리고 스윙 주기가 불규칙해지던 버그 수정. OnCompleted만 사용.
	FarmingTask->OnCompleted.AddDynamic(this, &UHeroGameplayAbility_Farming::OnFarmingFinished);
	FarmingTask->OnInterrupted.AddDynamic(this, &UHeroGameplayAbility_Farming::OnFarmingInterrupted);
	FarmingTask->OnCancelled.AddDynamic(this, &UHeroGameplayAbility_Farming::OnFarmingInterrupted);

	FarmingTask->ReadyForActivation();

	// [DmgSyncV2] 스윙 시작마다 데미지 적용 플래그 리셋
	bDamageFiredThisSwing = false;

	// [DmgSyncV2] 타격 AnimNotify 바인딩 — HitNotifyName이 비어있지 않으면 프레임 정확한 타격 적용
	if (!HitNotifyName.IsNone())
	{
		if (USkeletalMeshComponent* Mesh = Hero->GetMesh())
		{
			if (UAnimInstance* Anim = Mesh->GetAnimInstance())
			{
				Anim->OnPlayMontageNotifyBegin.RemoveDynamic(this, &UHeroGameplayAbility_Farming::OnMontageNotifyBegin);
				Anim->OnPlayMontageNotifyBegin.AddDynamic(this, &UHeroGameplayAbility_Farming::OnMontageNotifyBegin);
			}
		}
	}

	// [DmgSyncV2] 데미지 지연 결정 — 절대 시간(DamageImpactTime) 우선, 0이면 비율 폴백
	{
		const float MontageLength = FarmingMontage->GetPlayLength();
		float DamageDelay = (DamageImpactTime > 0.f)
			? DamageImpactTime
			: FMath::Max(0.01f, MontageLength * DamageTimingRatio);
		DamageDelay = FMath::Max(0.01f, FMath::Min(DamageDelay, MontageLength - 0.01f));

		if (DamageDelayTask)
		{
			DamageDelayTask->EndTask();
			DamageDelayTask = nullptr;
		}

		DamageDelayTask = UAbilityTask_WaitDelay::WaitDelay(this, DamageDelay);
		if (DamageDelayTask)
		{
			DamageDelayTask->OnFinish.AddDynamic(this, &UHeroGameplayAbility_Farming::ApplyFarmingDamage);
			DamageDelayTask->ReadyForActivation();
		}
	}
}

AActor* UHeroGameplayAbility_Farming::GetFarmingTarget(const FGameplayAbilityActorInfo* ActorInfo) const
{
	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(Avatar);
	if (!Hero) return nullptr;

	UHelluna_FindResourceComponent* FindComp = Hero->FindComponentByClass<UHelluna_FindResourceComponent>();
	if (!FindComp) return nullptr;

	// - 로컬(클라): 포커스(하이라이트) 대상
	// - 서버: ApplyFarming에서 ServerSetCanFarming(true, Target)로 캐시된 "파밍 대상"
	if (ActorInfo && ActorInfo->IsLocallyControlled())
	{
		return FindComp->GetFocusedActor();
	}

	return FindComp->GetServerFarmingTarget();
}

AActor* UHeroGameplayAbility_Farming::ResolveFarmingTarget(const FGameplayAbilityActorInfo* ActorInfo)
{
	if (CachedFarmingTarget.IsValid() && IsTargetWithinFarmingRange(ActorInfo, CachedFarmingTarget.Get()))
	{
		return CachedFarmingTarget.Get();
	}

	AActor* NewTarget = GetFarmingTarget(ActorInfo);
	if (IsTargetWithinFarmingRange(ActorInfo, NewTarget))
	{
		CachedFarmingTarget = NewTarget;
		return NewTarget;
	}

	CachedFarmingTarget = nullptr;
	return nullptr;
}

bool UHeroGameplayAbility_Farming::IsTargetWithinFarmingRange(
	const FGameplayAbilityActorInfo* ActorInfo,
	AActor* Target) const
{
	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid() || !IsValid(Target))
	{
		return false;
	}

	const AActor* Avatar = ActorInfo->AvatarActor.Get();
	const float MaxDistance = FMath::Max(FarmingSnapDistance, 200.f) + 80.f;
	return FVector::DistSquared2D(Avatar->GetActorLocation(), Target->GetActorLocation()) <= FMath::Square(MaxDistance);
}

void UHeroGameplayAbility_Farming::FaceToTarget_InstantLocalOnly(
	const FGameplayAbilityActorInfo* ActorInfo,
	const FVector& TargetLocation) const
{
	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid())
		return;

	APawn* Pawn = Cast<APawn>(ActorInfo->AvatarActor.Get());
	if (!Pawn || !Pawn->IsLocallyControlled())
		return;

	const FVector PawnLoc = Pawn->GetActorLocation();
	const FRotator LookAt = UKismetMathLibrary::FindLookAtRotation(PawnLoc, TargetLocation);

	FRotator NewRot = Pawn->GetActorRotation();
	NewRot.Yaw = LookAt.Yaw;
	Pawn->SetActorRotation(NewRot);

	// [BodyOnlyV1][LCv11] Controller(=Camera) Yaw는 건드리지 않는다.
	// 캐릭터 몸만 타겟 쪽으로 돌리고, 카메라는 사용자가 보던 방향을 유지.
	UE_LOG(LogTemp, Warning, TEXT("[BodyOnlyV1][LCv11] FaceToTarget: pawn-only yaw=%.1f (camera untouched)"), LookAt.Yaw);
}

void UHeroGameplayAbility_Farming::ApplyFarmingDamage()
{
	// [DmgSyncV2] Notify/Timer 둘 중 먼저 도달한 쪽만 적용 (이중 데미지 방지)
	if (bDamageFiredThisSwing) return;

	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(GetAvatarActorFromActorInfo());
	if (!Hero || !Hero->HasAuthority()) return;

	AActor* Target = ResolveFarmingTarget(CurrentActorInfo);
	if (!Target) return;

	AHellunaFarmingWeapon* Pickaxe = Cast<AHellunaFarmingWeapon>(Hero->GetCurrentWeapon());
	if (Pickaxe)
	{
		Pickaxe->Farm(Hero->GetController(), Target);
		bDamageFiredThisSwing = true;
	}
}

void UHeroGameplayAbility_Farming::OnMontageNotifyBegin(FName NotifyName, const FBranchingPointNotifyPayload& /*Payload*/)
{
	// [DmgSyncV2] 몽타주 Notify가 설정된 이름이면 즉시 데미지 — 프레임 정확
	if (HitNotifyName.IsNone() || NotifyName != HitNotifyName) return;
	ApplyFarmingDamage();
}

void UHeroGameplayAbility_Farming::OnFarmingFinished()
{
	// ✅ 몽타주 1회 완료 → 홀딩 중이면 다음 스윙 시작
	if (!ResolveFarmingTarget(CurrentActorInfo))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
		return;
	}

	PlayFarmingMontage();
}

void UHeroGameplayAbility_Farming::OnFarmingInterrupted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

bool UHeroGameplayAbility_Farming::SnapHeroToFarmingDistance(
	const FGameplayAbilityActorInfo* ActorInfo
) const
{
	AActor* Target = CachedFarmingTarget.Get();
	if (!Target)
	{
		Target = GetFarmingTarget(ActorInfo);
	}
	if (!Target)
	{
		return false;
	}

	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get());
	const FVector HeroLoc = Hero->GetActorLocation();
	const FVector TargetLoc = Target->GetActorLocation();
	const float DesiredDistance = FarmingSnapDistance;
	FVector Dir = (HeroLoc - TargetLoc);
	Dir.Z = 0.f;

	if (Dir.IsNearlyZero())
	{
		Dir = -Hero->GetActorForwardVector();
		Dir.Z = 0.f;
	}

	Dir = Dir.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return false;
	}

	FVector DesiredLoc = TargetLoc + Dir * DesiredDistance;
	DesiredLoc.Z = HeroLoc.Z;

	if (!(Hero->HasAuthority() || ActorInfo->IsLocallyControlled()))
	{
		return false;
	}

	FHitResult Hit;
	return Hero->SetActorLocation(
		DesiredLoc,
		true,
		&Hit,
		ETeleportType::TeleportPhysics
	);
}

void UHeroGameplayAbility_Farming::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 데미지 타이머 정리
	if (AHellunaHeroCharacter* TimerOwner = Cast<AHellunaHeroCharacter>(GetAvatarActorFromActorInfo()))
	{
		TimerOwner->GetWorldTimerManager().ClearTimer(DamageTimerHandle);
	}

	// [DmgSyncV1] WaitDelay Task 정리
	if (DamageDelayTask)
	{
		DamageDelayTask->EndTask();
		DamageDelayTask = nullptr;
	}

	// [DmgSyncV2] AnimNotify 바인딩 해제
	if (AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(GetAvatarActorFromActorInfo()))
	{
		if (USkeletalMeshComponent* Mesh = Hero->GetMesh())
		{
			if (UAnimInstance* Anim = Mesh->GetAnimInstance())
			{
				Anim->OnPlayMontageNotifyBegin.RemoveDynamic(this, &UHeroGameplayAbility_Farming::OnMontageNotifyBegin);
			}
		}
	}

	// 홀딩 해제 시 몽타주 즉시 중지
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->CurrentMontageStop();
	}

	if (AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(GetAvatarActorFromActorInfo()))
	{
		if (CurrentActorInfo && CurrentActorInfo->IsLocallyControlled())
		{
			Hero->UnlockMoveInput();
			Hero->UnlockLookInput();
		}

		Hero->PlayFullBody = false;
	}
	CachedFarmingTarget = nullptr;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
