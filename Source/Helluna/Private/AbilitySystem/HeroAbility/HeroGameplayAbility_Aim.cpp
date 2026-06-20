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
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

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

	// ── [Sniper Scope] 상태 초기화 ──
	bScopeActive = false;
	bExiting = false;
	bWeaponSupportsScope = false;
	AimActivationTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero) { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }

	// ── [AimTurnToCameraV3] 견착 시 '캐릭터'를 카메라(조준)방향으로 회전시킨다(사용자 요청). ──
	//   카메라를 캐릭터로 돌리는 realign 은 쓰지 않는다(반대 방향). 아래 bUseControllerDesiredRotation=true 가
	//   CharacterMovement 로 캐릭터 Yaw 를 컨트롤(카메라/조준) 방향으로 회전시킨다.

	// ── 기본값 캐싱 ──
	if (UCharacterMovementComponent* MoveComp = Hero->GetCharacterMovement())
	{
		// [MoveSpeedBaseV1] 이제 MaxWalkSpeed 를 직접 조작하지 않고 Hero 의 ActiveBaseWalkSpeed 를 통해 간접 설정.
		//   Hero::RefreshMaxWalkSpeed 가 ActiveBase * MoveSpeedMultiplier 로 즉시 계산.
		//   Aim 중에 슬로우가 걸렸다/풀렸다 해도 Hero 쪽에서 자동으로 반영됨.
		CachedDefaultMaxWalkSpeed = Hero->GetBaseWalkSpeed(); // 복원 시 돌려놓을 "기본 걷기 속도"
		Hero->SetActiveBaseWalkSpeed(AimMaxWalkSpeed);

		// ── [Aim Rotation] 조준 시 캐릭터가 카메라 방향을 따라 회전 (RE4 스타일) ──
		CachedOrientRotationToMovement = MoveComp->bOrientRotationToMovement;
		CachedUseControllerDesiredRotation = MoveComp->bUseControllerDesiredRotation;
		CachedRotationRate = MoveComp->RotationRate;

		// [AimTurnToCameraV3] 견착 시 캐릭터가 카메라(조준)방향으로 회전(사용자 요청). RotationRate
		//   360°/s. (정지 시 몸이 꺾이는 게 거슬리면 정석인 Aim Offset 애니메이션으로 개선 가능 — 별도 안내.)
		// [AimNoBodyTurnV2] 견착 중 캡슐을 카메라/조준 방향으로 안 돌림(이동방향 유지). facing 은
		//   메시 yaw(AimMeshYawOffset 30°, 시각)가 담당. 발사 시 반동 yaw 를 몸이 따라 꺾이는 튐 제거.
		// 견착 동작 원복 — 캐릭터가 카메라(조준)방향으로 회전(원래 동작, 사용자: 견착 건드리지 말 것).
		//   '발사 시 추가 회전'은 반동 좌우 yaw 를 몸이 따라가는 것이므로 무기 반동 쪽에서 처리.
		MoveComp->bOrientRotationToMovement = false;
		MoveComp->bUseControllerDesiredRotation = true;
		MoveComp->RotationRate = FRotator(0.f, 720.f, 0.f); // 견착 회전속도(원복). 발사 꺾임 원인은 AimMeshYawOffset 이었음.

		UE_LOG(LogTemp, Warning, TEXT("[Aim GA][AimNoBodyTurnV1] 견착 중 몸을 조준방향으로 안 돌림(이동방향 유지) (prev bOrientToMovement: %s, bUseControllerDesired: %s)"),
			CachedOrientRotationToMovement ? TEXT("true") : TEXT("false"),
			CachedUseControllerDesiredRotation ? TEXT("true") : TEXT("false"));
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
	if (Gun)
	{
		// 스나이퍼(스코프 토글 지원) 여부 캐싱 — 탭/홀드 분리 동작 게이트
		bWeaponSupportsScope = Gun->bSupportsScopeToggle;

		if (Gun->AimZoomFOV > 0.f)
		{
			TargetFOV = Gun->AimZoomFOV;
			InterpSpeed = Gun->AimZoomInterpSpeed;
			if (Gun->AimArmLengthMultiplier > 0.f)
			{
				TargetArmLength = CachedDefaultArmLength * Gun->AimArmLengthMultiplier;
			}
		}
	}

	// ── Phase 1: 줌인(견착=ADS 약줌) AbilityTask 시작 ──
	//   스나이퍼도 일단 견착 FOV 로 줌인 시작 → 짧게 떼면(탭) InputReleased 에서 스코프 강줌으로 전환.
	UAT_AimCameraInterp* ZoomInTask = StartCameraInterp(TargetFOV, TargetArmLength, InterpSpeed);
	ZoomInTask->OnCompleted.AddUniqueDynamic(this, &UHeroGameplayAbility_Aim::OnZoomInCompleted);
	ZoomInTask->ReadyForActivation();

	UE_LOG(LogTemp, Warning, TEXT("[Aim GA] Phase 1 시작 — 줌인 (FOV %.1f→%.1f, Arm %.1f→%.1f, Scope지원=%s)"),
		CachedDefaultFOV, TargetFOV, CachedDefaultArmLength, TargetArmLength,
		bWeaponSupportsScope ? TEXT("Y") : TEXT("N"));

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
	// 이미 줌아웃/종료 중이면 무시 (2번째 탭의 release 등)
	if (bExiting)
	{
		return;
	}

	// ── [Sniper Scope] 짧은 탭이면 스코프 강줌 토글 진입 (종료하지 않음) ──
	//   스코프 미지원 무기는 아래 기존(견착) 경로로만 동작.
	if (bWeaponSupportsScope && !bScopeActive)
	{
		const float Elapsed = GetWorld() ? (GetWorld()->GetTimeSeconds() - AimActivationTime) : 999.f;
		if (Elapsed < TapHoldThreshold)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Aim GA][Scope] 탭 감지 (%.3fs < %.3fs) — 스코프 강줌 진입"),
				Elapsed, TapHoldThreshold);
			EnterScope();
			return;
		}
	}

	// ── 견착(홀드) 해제 / 일반 줌아웃 (기존 동작) ──
	bInputReleased = true;

	if (CurrentPhase == 2)
	{
		// Phase 2에서 해제 → Phase 3 시작
		StartZoomOut();
	}
	// Phase 1 중이면 OnZoomInCompleted에서 처리
}

void UHeroGameplayAbility_Aim::InputPressed(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	// [Sniper Scope] 스코프가 켜진 상태에서 다시 우클릭(2번째 탭) → 스코프 해제(줌아웃 후 종료)
	if (bWeaponSupportsScope && bScopeActive && !bExiting)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Aim GA][Scope] 재입력 — 스코프 해제 → 줌아웃"));
		bScopeActive = false;
		HideScopeOverlay();
		StartZoomOut();
	}
}

void UHeroGameplayAbility_Aim::ForceExitScope()
{
	// 스코프(탭 토글 강줌)가 켜진 상태에서만 의미 있음. 견착(ADS 홀드)은 그대로 둔다.
	if (!bScopeActive || bExiting)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[Aim GA][Scope] 외부 강제 해제(ForceExitScope) — 줌아웃"));
	bScopeActive = false;
	HideScopeOverlay();
	StartZoomOut();
}

void UHeroGameplayAbility_Aim::EnterScope()
{
	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	AHeroWeapon_GunBase* Gun = Hero ? Cast<AHeroWeapon_GunBase>(Hero->GetCurrentWeapon()) : nullptr;
	if (!Gun) { return; }

	const float ScopeFOV = (Gun->ScopeZoomFOV > 0.f) ? Gun->ScopeZoomFOV : Gun->AimZoomFOV;
	float ScopeArm = CachedDefaultArmLength;
	if (Gun->ScopeArmLengthMultiplier > 0.f)
	{
		ScopeArm = CachedDefaultArmLength * Gun->ScopeArmLengthMultiplier;
	}
	else if (Gun->AimArmLengthMultiplier > 0.f)
	{
		ScopeArm = CachedDefaultArmLength * Gun->AimArmLengthMultiplier;
	}

	bScopeActive = true;
	CurrentPhase = 2; // 유지(사격 가능) 단계로 취급

	const float Speed = (Gun->AimZoomInterpSpeed > 0.f) ? Gun->AimZoomInterpSpeed : 12.f;
	UAT_AimCameraInterp* ScopeTask = StartCameraInterp(ScopeFOV, ScopeArm, Speed);
	ScopeTask->ReadyForActivation();

	ShowScopeOverlay();

	UE_LOG(LogTemp, Warning, TEXT("[Aim GA][Scope] 스코프 진입 — FOV→%.1f, Arm→%.1f"), ScopeFOV, ScopeArm);
}

void UHeroGameplayAbility_Aim::ShowScopeOverlay()
{
	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero || !Hero->IsLocallyControlled()) { return; } // 로컬 전용

	AHeroWeapon_GunBase* Gun = Cast<AHeroWeapon_GunBase>(Hero->GetCurrentWeapon());
	if (!Gun || !Gun->ScopeOverlayWidgetClass) { return; }

	if (ScopeOverlayWidget) { return; } // 이미 표시 중

	APlayerController* PC = Cast<APlayerController>(Hero->GetController());
	if (!PC) { return; }

	ScopeOverlayWidget = CreateWidget<UUserWidget>(PC, Gun->ScopeOverlayWidgetClass);
	if (ScopeOverlayWidget)
	{
		ScopeOverlayWidget->AddToViewport(50);

		// [Scope HUD] readout 라이브 갱신 시작 — 한 번 즉시 + 0.06s 주기 (로컬 전용)
		UpdateScopeHUD();
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				ScopeHUDUpdateTimer, this, &UHeroGameplayAbility_Aim::UpdateScopeHUD, 0.06f, true);
		}
	}
}

void UHeroGameplayAbility_Aim::HideScopeOverlay()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ScopeHUDUpdateTimer);
	}

	if (ScopeOverlayWidget)
	{
		ScopeOverlayWidget->RemoveFromParent();
		ScopeOverlayWidget = nullptr;
	}
}

void UHeroGameplayAbility_Aim::UpdateScopeHUD()
{
	if (!ScopeOverlayWidget) { return; }

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero) { return; }

	AHeroWeapon_GunBase* Gun = Cast<AHeroWeapon_GunBase>(Hero->GetCurrentWeapon());
	UCameraComponent* Cam = Hero->GetFollowCamera();

	// ── 줌 배율 = 기본 FOV / 현재 FOV (강줌일수록 배율 ↑) ──
	if (Cam && CachedDefaultFOV > 1.f)
	{
		const float CurFOV = FMath::Max(Cam->FieldOfView, 1.f);
		const float Zoom = CachedDefaultFOV / CurFOV;
		if (UTextBlock* T = Cast<UTextBlock>(ScopeOverlayWidget->GetWidgetFromName(TEXT("Txt_Zoom"))))
		{
			T->SetText(FText::FromString(FString::Printf(TEXT("%.1fx"), Zoom)));
		}
	}

	// ── 탄약 (현재/최대) + 막대 표시 ──
	if (Gun)
	{
		const int32 Cur = Gun->CurrentMag;
		const int32 Max = FMath::Max(Gun->MaxMag, 1);
		if (UTextBlock* T = Cast<UTextBlock>(ScopeOverlayWidget->GetWidgetFromName(TEXT("Txt_AmmoValue"))))
		{
			T->SetText(FText::FromString(FString::Printf(TEXT("%02d"), Cur)));
		}
		if (UTextBlock* T = Cast<UTextBlock>(ScopeOverlayWidget->GetWidgetFromName(TEXT("Txt_AmmoMax"))))
		{
			T->SetText(FText::FromString(FString::Printf(TEXT("/ %02d"), Gun->MaxMag)));
		}
		// 탄약 막대(■ 채움 / □ 빈칸) — 최대 8칸으로 정규화
		if (UTextBlock* T = Cast<UTextBlock>(ScopeOverlayWidget->GetWidgetFromName(TEXT("Txt_AmmoBar"))))
		{
			const int32 Slots = 8;
			const int32 Filled = FMath::Clamp(FMath::RoundToInt((float)Cur / (float)Max * Slots), 0, Slots);
			FString Bar;
			for (int32 i = 0; i < Slots; ++i) { Bar += (i < Filled) ? TEXT("■") : TEXT("□"); }
			T->SetText(FText::FromString(Bar));
		}
	}

	// ── 타겟 거리 (카메라 정면 라인트레이스, m 단위) ──
	if (Cam)
	{
		const FVector Start = Cam->GetComponentLocation();
		const float TraceRange = (Gun && Gun->Range > 0.f) ? Gun->Range : 20000.f;
		const FVector End = Start + Cam->GetForwardVector() * TraceRange;

		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ScopeRangefinder), true, Hero);
		Params.AddIgnoredActor(Hero);
		if (Gun) { Params.AddIgnoredActor(Gun); }

		FString DistText = TEXT("---");
		if (UWorld* World = GetWorld())
		{
			if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
			{
				const float Meters = Hit.Distance / 100.f;
				DistText = FString::Printf(TEXT("%.0f"), Meters);
			}
		}
		if (UTextBlock* T = Cast<UTextBlock>(ScopeOverlayWidget->GetWidgetFromName(TEXT("Txt_DistValue"))))
		{
			T->SetText(FText::FromString(DistText));
		}
	}
}

void UHeroGameplayAbility_Aim::StartZoomOut()
{
	CurrentPhase = 3;
	bExiting = true; // 줌아웃 이후 들어오는 입력 무시

	// 이동속도 복원은 EndAbility에서 처리 (서버/클라 양쪽에서 호출되므로)

	// ── Phase 3: 줌아웃 AbilityTask (이전 태스크 자동 종료) ──
	ZoomOutTask = StartCameraInterp(CachedDefaultFOV, CachedDefaultArmLength, 12.f);
	ZoomOutTask->OnCompleted.AddUniqueDynamic(this, &UHeroGameplayAbility_Aim::OnZoomOutCompleted);
	ZoomOutTask->ReadyForActivation();

	UE_LOG(LogTemp, Warning, TEXT("[Aim GA] Phase 3 시작 — 줌아웃 (FOV→%.1f, Arm→%.1f)"),
		CachedDefaultFOV, CachedDefaultArmLength);
}

UAT_AimCameraInterp* UHeroGameplayAbility_Aim::StartCameraInterp(float TargetFOV, float TargetArmLength, float InterpSpeed)
{
	// 이전 보간 태스크가 살아 있으면 종료 (두 태스크가 FOV 를 두고 경쟁하지 않도록)
	if (ActiveInterpTask)
	{
		ActiveInterpTask->EndTask();
		ActiveInterpTask = nullptr;
	}

	ActiveInterpTask = UAT_AimCameraInterp::CreateTask(
		this, TargetFOV, TargetArmLength, CachedDefaultSocketOffset, InterpSpeed);
	return ActiveInterpTask;
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
	if (AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo())
	{
		// ── 이동속도 + 회전 복원: 서버/클라 양쪽에서 동일하게 (LocalPredicted) ──
		if (UCharacterMovementComponent* MoveComp = Hero->GetCharacterMovement())
		{
			// [MoveSpeedBaseV1] Aim 종료 시 ActiveBaseWalkSpeed 를 기본(BaseWalkSpeed)로 돌려놓음.
			//   Hero 가 MaxWalkSpeed 를 ActiveBase * MoveSpeedMultiplier 로 재계산 → 슬로우가 남아 있어도 정확.
			Hero->SetActiveBaseWalkSpeed(Hero->GetBaseWalkSpeed());

			// ── [Aim Rotation] 회전 방식 복원 ──
			MoveComp->bOrientRotationToMovement = CachedOrientRotationToMovement;
			MoveComp->bUseControllerDesiredRotation = CachedUseControllerDesiredRotation;
			MoveComp->RotationRate = CachedRotationRate;

			UE_LOG(LogTemp, Warning, TEXT("[Aim GA] Rotation Restored → bOrientToMovement=%s, bUseControllerDesired=%s"),
				CachedOrientRotationToMovement ? TEXT("true") : TEXT("false"),
				CachedUseControllerDesiredRotation ? TEXT("true") : TEXT("false"));
		}

		// ── 카메라 즉시 복원: 취소 시 보간 미완료 상태면 스냅 (로컬만) ──
		// Phase 1~3 모두 취소 가능 — Phase 3(줌아웃 중) 취소 시에도 FOV 복원 필수
		if (bWasCancelled && Hero->IsLocallyControlled())
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
			UE_LOG(LogTemp, Warning, TEXT("[Aim GA] Phase %d 취소됨 — 카메라 즉시 복원 (FOV=%.1f)"),
				CurrentPhase, CachedDefaultFOV);
		}

		ZoomOutTask = nullptr;
	}

	// ── [Sniper Scope] 스코프 오버레이/태스크 정리 (취소·정상 종료 모두) ──
	HideScopeOverlay();
	if (ActiveInterpTask)
	{
		ActiveInterpTask->EndTask();
		ActiveInterpTask = nullptr;
	}
	bScopeActive = false;
	bExiting = false;

	CurrentPhase = 0;
	UE_LOG(LogTemp, Warning, TEXT("[Aim GA] EndAbility (Cancelled=%s)"),
		bWasCancelled ? TEXT("Y") : TEXT("N"));

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
