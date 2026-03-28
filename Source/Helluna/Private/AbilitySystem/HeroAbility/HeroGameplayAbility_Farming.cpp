// Capstone Project Helluna


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Farming.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetMathLibrary.h"
#include "DrawDebugHelpers.h"
#include "Weapon/HellunaFarmingWeapon.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"

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

	// 기본 유효성 검사

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
	AActor* Target = GetFarmingTarget(CurrentActorInfo);
	if (!Target)
	{
		if (CurrentActorInfo && CurrentActorInfo->IsLocallyControlled())
		{
			Debug::Print(TEXT("주변에 채집 대상(포커스된 광물)이 없음"), FColor::Red);
		}
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
		return;
	}

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

	// ✅ 스냅 + 회전
	SnapHeroToFarmingDistance(CurrentActorInfo);
	FaceToTarget_InstantLocalOnly(CurrentActorInfo, Target->GetActorLocation());

	// ✅ 몽타주 재생 (AbilityTask)
	FarmingTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, FarmingMontage, 1.f);
	if (!FarmingTask)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	FarmingTask->OnCompleted.AddDynamic(this, &UHeroGameplayAbility_Farming::OnFarmingFinished);
	FarmingTask->OnBlendOut.AddDynamic(this, &UHeroGameplayAbility_Farming::OnFarmingFinished);
	FarmingTask->OnInterrupted.AddDynamic(this, &UHeroGameplayAbility_Farming::OnFarmingInterrupted);
	FarmingTask->OnCancelled.AddDynamic(this, &UHeroGameplayAbility_Farming::OnFarmingInterrupted);

	FarmingTask->ReadyForActivation();

	// ✅ 몽타주 길이의 DamageTimingRatio 비율 시점에 데미지 적용 (서버만)
	if (Hero->HasAuthority() && FarmingMontage)
	{
		const float MontageLength = FarmingMontage->GetPlayLength();
		const float DamageDelay = MontageLength * DamageTimingRatio;

		Hero->GetWorldTimerManager().ClearTimer(DamageTimerHandle);
		Hero->GetWorldTimerManager().SetTimer(
			DamageTimerHandle,
			this,
			&UHeroGameplayAbility_Farming::ApplyFarmingDamage,
			DamageDelay,
			false);
	}
}

AActor* UHeroGameplayAbility_Farming::GetFarmingTarget(const FGameplayAbilityActorInfo* ActorInfo) const
{
	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(Avatar);
	if (!Hero) return nullptr;

	UHelluna_FindResourceComponent* FindComp = Hero->FindComponentByClass<UHelluna_FindResourceComponent>();
	if (!FindComp) return nullptr;

	// ✅ 핵심 분기:
	// - 로컬(클라): 포커스(하이라이트) 대상
	// - 서버: ApplyFarming에서 ServerSetCanFarming(true, Target)로 캐시된 "파밍 대상"
	if (ActorInfo && ActorInfo->IsLocallyControlled())
	{
		return FindComp->GetFocusedActor();
	}

	return FindComp->GetServerFarmingTarget();
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

	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	if (!PC) return;

	// ✅ TPS 안정 기준점
	const FVector StartLoc = Pawn->GetPawnViewLocation();

	// 1) 기본 LookAt
	const FRotator LookAt = UKismetMathLibrary::FindLookAtRotation(StartLoc, TargetLocation);

	// 2) 화면 크기 기반 "크로스헤어 X 오프셋" -> Yaw 보정 각도 계산
	int32 SizeX = 0, SizeY = 0;
	PC->GetViewportSize(SizeX, SizeY);
	if (SizeX <= 0) return;


	const float CenterX = SizeX * 0.5f;
	const float CrossX = SizeX * CrosshairXNormalized;
	const float Dx = CrossX - CenterX; // +면 오른쪽

	// FOV 기반 픽셀 오프셋을 각도로 변환
	const float FovDeg = (PC->PlayerCameraManager) ? PC->PlayerCameraManager->GetFOVAngle() : 90.f;
	const float FovRad = FMath::DegreesToRadians(FovDeg);
	const float FocalLengthPx = CenterX / FMath::Tan(FovRad * 0.5f); // f = (w/2)/tan(FOV/2)

	const float OffsetYawDeg = FMath::RadiansToDegrees(FMath::Atan2(Dx, FocalLengthPx));
	// ✅ 크로스헤어가 오른쪽이면(+Dx), 타겟을 그쪽으로 보내려면 Yaw를 "왼쪽으로"(-) 더 돌려야 함
	const float TargetYawWithOffset = LookAt.Yaw - OffsetYawDeg;

	// 3) 데드존 (미세 보정 무시)
	constexpr float YawDeadZoneDeg = 0.25f;

	const float CurrentYaw = PC->GetControlRotation().Yaw;
	const float DeltaYaw = FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYawWithOffset));
	if (DeltaYaw <= YawDeadZoneDeg)
		return;

	// Pitch 유지, Yaw만 맞추기
	FRotator NewRot = PC->GetControlRotation();
	NewRot.Yaw = TargetYawWithOffset;
	PC->SetControlRotation(NewRot);
}

void UHeroGameplayAbility_Farming::ApplyFarmingDamage()
{
	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(GetAvatarActorFromActorInfo());
	if (!Hero || !Hero->HasAuthority()) return;

	AActor* Target = GetFarmingTarget(CurrentActorInfo);
	if (!Target) return;

	AHellunaFarmingWeapon* Pickaxe = Cast<AHellunaFarmingWeapon>(Hero->GetCurrentWeapon());
	if (Pickaxe)
	{
		Pickaxe->Farm(Hero->GetController(), Target);
	}
}

void UHeroGameplayAbility_Farming::OnFarmingFinished()
{
	// ✅ 몽타주 1회 완료 → 홀딩 중이면 다음 스윙 시작
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
	AActor* Target = GetFarmingTarget(ActorInfo);

	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get());
	const FVector HeroLoc = Hero->GetActorLocation();
	const FVector TargetLoc = Target->GetActorLocation();
	const float DesiredDistance = FarmingSnapDistance;
	FVector Dir = (HeroLoc - TargetLoc);
	Dir.Z = 0.f;

	// 완전 겹쳤으면 뒤로 빠지게 (현재 바라보는 반대 방향)
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

	// ✅ 목표 위치: 타겟에서 DesiredDistance만큼 떨어진 지점
	FVector DesiredLoc = TargetLoc + Dir * DesiredDistance;

	// Z는 현재 유지 (바닥 스냅은 나중에 필요하면 추가)
	DesiredLoc.Z = HeroLoc.Z;

	// ✅ 서버 권위 + 로컬 체감 둘 다에서만 실행
	if (!(Hero->HasAuthority() || ActorInfo->IsLocallyControlled()))
	{
		return false;
	}

	FHitResult Hit;
	return Hero->SetActorLocation(
		DesiredLoc,
		true,  // bSweep: 캡슐 충돌만 사용
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
	// ✅ 데미지 타이머 정리
	if (AHellunaHeroCharacter* TimerOwner = Cast<AHellunaHeroCharacter>(GetAvatarActorFromActorInfo()))
	{
		TimerOwner->GetWorldTimerManager().ClearTimer(DamageTimerHandle);
	}

	// ✅ 홀딩 해제 시 몽타주 즉시 중지
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
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
