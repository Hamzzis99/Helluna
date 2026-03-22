// File: Source/Helluna/Private/AbilitySystem/HeroAbility/HeroGameplayAbility_Aim.cpp
#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Aim.h"
#include "AbilitySystem/AbilityTask/AT_AimCameraInterp.h"
#include "AbilitySystemComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Weapon/HeroWeapon_GunBase.h"
#include "HellunaGameplayTags.h"

UHeroGameplayAbility_Aim::UHeroGameplayAbility_Aim()
{
	InputActionPolicy = EHellunaInputActionPolicy::Hold;
	AbilityTags.AddTag(HellunaGameplayTags::Player_Ability_Aim);
	ActivationRequiredTags.AddTag(HellunaGameplayTags::Player_Weapon_Gun);
	ActivationOwnedTags.AddTag(HellunaGameplayTags::Player_status_Aim);
	CancelAbilitiesWithTag.AddTag(HellunaGameplayTags::Player_Ability_Run);
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UHeroGameplayAbility_Aim::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	bInputReleased = false;
	CurrentPhase = 1;

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero) { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }

	// ── 기본값 캐싱 ──
	if (UCharacterMovementComponent* MoveComp = Hero->GetCharacterMovement())
	{
		CachedDefaultMaxWalkSpeed = MoveComp->MaxWalkSpeed;
		// 이동속도는 서버에서만 변경 (MaxWalkSpeed는 서버→클라 복제됨)
		if (Hero->HasAuthority())
		{
			MoveComp->MaxWalkSpeed = AimMaxWalkSpeed;
		}
	}

	if (UCameraComponent* Cam = Hero->GetFollowCamera())
	{
		CachedDefaultFOV = Cam->FieldOfView;
	}
	if (USpringArmComponent* Boom = Hero->GetCameraBoom())
	{
		CachedDefaultArmLength = Boom->TargetArmLength;
		CachedDefaultSocketOffset = Boom->SocketOffset;
	}

	// ── 무기에서 줌 목표값 읽기 ──
	float TargetFOV = CachedDefaultFOV;
	float TargetArmLength = CachedDefaultArmLength;
	FVector TargetSocketOffset = CachedDefaultSocketOffset;
	float InterpSpeed = 10.f;

	AHeroWeapon_GunBase* Gun = Cast<AHeroWeapon_GunBase>(Hero->GetCurrentWeapon());
	if (Gun && Gun->AimZoomFOV > 0.f)
	{
		TargetFOV = Gun->AimZoomFOV;
		InterpSpeed = Gun->AimZoomInterpSpeed;
		if (Gun->AimArmLengthMultiplier > 0.f)
		{
			TargetArmLength = CachedDefaultArmLength * Gun->AimArmLengthMultiplier;
		}
	}

	// ── Phase 1: 줌인 AbilityTask 시작 ──
	UAT_AimCameraInterp* ZoomInTask = UAT_AimCameraInterp::CreateTask(
		this, TargetFOV, TargetArmLength, TargetSocketOffset, InterpSpeed);
	ZoomInTask->OnCompleted.AddDynamic(this, &UHeroGameplayAbility_Aim::OnZoomInCompleted);
	ZoomInTask->ReadyForActivation();

	UE_LOG(LogTemp, Warning, TEXT("[Aim GA] Phase 1 시작 — 줌인 (FOV %.1f→%.1f, Arm %.1f→%.1f)"),
		CachedDefaultFOV, TargetFOV, CachedDefaultArmLength, TargetArmLength);

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UHeroGameplayAbility_Aim::OnZoomInCompleted()
{
	// Phase 1 중에 이미 입력이 해제됐으면 바로 Phase 3
	if (bInputReleased)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Aim GA] Phase 1 완료 — 이미 입력 해제됨, Phase 3으로 즉시 전환"));
		StartZoomOut();
		return;
	}

	CurrentPhase = 2;
	UE_LOG(LogTemp, Warning, TEXT("[Aim GA] Phase 2 진입 — 조준 유지 (사격 가능)"));
}

void UHeroGameplayAbility_Aim::InputReleased(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	bInputReleased = true;

	if (CurrentPhase == 2)
	{
		// Phase 2에서 해제 → Phase 3 시작
		StartZoomOut();
	}
	// Phase 1 중이면 OnZoomInCompleted에서 처리
}

void UHeroGameplayAbility_Aim::StartZoomOut()
{
	CurrentPhase = 3;

	// ── 이동속도 복원 (서버에서만) ──
	if (CachedDefaultMaxWalkSpeed > 0.f)
	{
		if (AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo())
		{
			if (Hero->HasAuthority())
			{
				if (UCharacterMovementComponent* MoveComp = Hero->GetCharacterMovement())
				{
					MoveComp->MaxWalkSpeed = CachedDefaultMaxWalkSpeed;
				}
			}
		}
	}

	// ── Phase 3: 줌아웃 AbilityTask ──
	ZoomOutTask = UAT_AimCameraInterp::CreateTask(
		this, CachedDefaultFOV, CachedDefaultArmLength, CachedDefaultSocketOffset, 12.f);
	ZoomOutTask->OnCompleted.AddDynamic(this, &UHeroGameplayAbility_Aim::OnZoomOutCompleted);
	ZoomOutTask->ReadyForActivation();

	UE_LOG(LogTemp, Warning, TEXT("[Aim GA] Phase 3 시작 — 줌아웃 (FOV→%.1f, Arm→%.1f)"),
		CachedDefaultFOV, CachedDefaultArmLength);
}

void UHeroGameplayAbility_Aim::OnZoomOutCompleted()
{
	UE_LOG(LogTemp, Warning, TEXT("[Aim GA] Phase 3 완료 — EndAbility"));
	K2_EndAbility();
}

void UHeroGameplayAbility_Aim::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	// GA 취소(무기 교체 등)로 EndAbility가 호출된 경우
	// → 줌아웃이 안 끝났을 수 있으므로 카메라 즉시 복원
	if (bWasCancelled && CurrentPhase < 3)
	{
		if (AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo())
		{
			if (UCameraComponent* Cam = Hero->GetFollowCamera())
			{
				Cam->SetFieldOfView(CachedDefaultFOV);
			}
			if (USpringArmComponent* Boom = Hero->GetCameraBoom())
			{
				Boom->TargetArmLength = CachedDefaultArmLength;
				Boom->SocketOffset = CachedDefaultSocketOffset;
			}
			if (UCharacterMovementComponent* MoveComp = Hero->GetCharacterMovement())
			{
				if (CachedDefaultMaxWalkSpeed > 0.f && Hero->HasAuthority())
				{
					MoveComp->MaxWalkSpeed = CachedDefaultMaxWalkSpeed;
				}
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("[Aim GA] 취소됨 — 카메라/속도 즉시 복원"));
	}

	CurrentPhase = 0;
	UE_LOG(LogTemp, Warning, TEXT("[Aim GA] EndAbility (Cancelled=%s)"),
		bWasCancelled ? TEXT("Y") : TEXT("N"));

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
