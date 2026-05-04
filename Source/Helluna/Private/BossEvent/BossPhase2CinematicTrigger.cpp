// Capstone Project Helluna

#include "BossEvent/BossPhase2CinematicTrigger.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Controller/HellunaHeroController.h"
#include "UI/BossDialogueWidget.h"
#include "Character/HellunaEnemyCharacter_Boss.h"
#include "NiagaraComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Blueprint/UserWidget.h"

ABossPhase2CinematicTrigger::ABossPhase2CinematicTrigger()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
}

void ABossPhase2CinematicTrigger::BeginPlay()
{
	Super::BeginPlay();
}

bool ABossPhase2CinematicTrigger::TryActivate(APawn* InTargetBoss)
{
	if (!HasAuthority()) return false;
	if (!InTargetBoss) return false;
	ActiveBoss = InTargetBoss;
	Multicast_StartCinematic(InTargetBoss);
	return true;
}

void ABossPhase2CinematicTrigger::Multicast_StartCinematic_Implementation(APawn* Boss)
{
	if (!Boss) return;
	ActiveBoss = Boss;

	UWorld* World = GetWorld();
	if (!World) return;

	// =========================================================
	// 카메라 액터 spawn — 단계1a Face 위치
	// =========================================================
	const FVector BossLoc = Boss->GetActorLocation();
	const FVector Forward = Boss->GetActorForwardVector();
	const FVector Right = Boss->GetActorRightVector();
	const FVector Up = Boss->GetActorUpVector();

	const FVector StartCamLoc = BossLoc + Forward * FaceOffset.X + Right * FaceOffset.Y + Up * FaceOffset.Z;
	const FVector StartLookTarget = BossLoc + FVector(0.f, 0.f, FaceLookHeight);
	const FRotator StartLookAt = (StartLookTarget - StartCamLoc).Rotation();

	FActorSpawnParameters CamParams;
	CamParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACameraActor* CamActor = World->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), StartCamLoc, StartLookAt, CamParams);
	if (CamActor)
	{
		if (UCameraComponent* CC = CamActor->GetCameraComponent())
		{
			CC->SetConstraintAspectRatio(false);
		}
		LocalCameraActor = CamActor;
	}

	// 단계1a 동안 hold (To = Face) + 카메라 정지 (보스 추적 X)
	bCameraInterpolating = false;
	bCameraHoldStaticDuringFace = true;
	CamInterpElapsed = 0.f;
	CamLerpFromOffset = FaceOffset;
	CamLerpToOffset = FaceOffset;
	CamLerpFromLookH = FaceLookHeight;
	CamLerpToLookH = FaceLookHeight;
	CamLerpDuration = 1.f;

	AActor* ViewTarget = CamActor ? (AActor*)CamActor : (AActor*)Boss;

	// =========================================================
	// 로컬 PC 시네마틱 진입 + 대사
	// =========================================================
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalPlayerController()) continue;
		AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(PC);
		if (!HeroPC) continue;

		// [Phase2InstantCutV1] CameraBlendIn = 0 이면 SetViewTarget 직접 호출 → 즉시 점프 (0.1초 minimum 우회).
		//   그 후 EnterBossCinematic 호출은 HUD lockdown / 입력 차단만 적용 (같은 viewtarget 이라 blend 무효).
		if (CameraBlendIn <= KINDA_SMALL_NUMBER)
		{
			HeroPC->SetViewTarget(ViewTarget);
		}
		HeroPC->EnterBossCinematic(ViewTarget, FMath::Max(CameraBlendIn, 0.05f));

		// 대사 — 첫 대사는 hide timer 없음. 두 번째 대사 (Stage1b) 또는 EndCinematic 이 hide.
		//   timer 두면 race condition (DialogueLine2 표시 직전에 widget nullptr 로 set 위험).
		if (DialogueWidgetClass && !DialogueLine.IsEmpty() && GetNetMode() != NM_DedicatedServer)
		{
			if (LocalDialogueWidget)
			{
				LocalDialogueWidget->HideDialogue();
				LocalDialogueWidget = nullptr;
			}
			LocalDialogueWidget = CreateWidget<UBossDialogueWidget>(PC, DialogueWidgetClass);
			if (LocalDialogueWidget)
			{
				LocalDialogueWidget->AddToViewport(50);
				LocalDialogueWidget->PlayDialogue(SpeakerName, DialogueLine);
			}
		}
		break;
	}

	// =========================================================
	// 단계 timer 시퀀스
	// =========================================================
	TWeakObjectPtr<ABossPhase2CinematicTrigger> WeakSelf(this);

	// Stage 1b — Face → Front lerp
	World->GetTimerManager().SetTimer(Stage1bTimer,
		FTimerDelegate::CreateLambda([WeakSelf]()
		{
			ABossPhase2CinematicTrigger* Self = WeakSelf.Get();
			if (!Self) return;
			// [Phase2InstantCut1bV1] 단계1a 끝 = 단계1b 시작 — face → front 즉시 cut (lerp 없음)
			Self->CamLerpFromOffset = Self->FrontOffset;
			Self->CamLerpToOffset = Self->FrontOffset;
			Self->CamLerpFromLookH = Self->FrontLookHeight;
			Self->CamLerpToLookH = Self->FrontLookHeight;
			Self->CamLerpDuration = 1.f;
			Self->CamInterpElapsed = 0.f;
			Self->bCameraInterpolating = false;
			Self->bCameraHoldStaticDuringFace = false;
			UE_LOG(LogTemp, Warning, TEXT("[Phase2CinematicTrigger] Stage 1b — instant cut to Front"));

			// [Phase2DialogueLine2V1] 단계1b 시작 시점에 두 번째 대사 표시.
			if (Self->LocalDialogueWidget && !Self->DialogueLine2.IsEmpty())
			{
				Self->LocalDialogueWidget->PlayDialogue(Self->SpeakerName, Self->DialogueLine2);
				if (UWorld* W = Self->GetWorld())
				{
					TWeakObjectPtr<ABossPhase2CinematicTrigger> WeakSelfDlg2(Self);
					W->GetTimerManager().ClearTimer(Self->DialogueHideTimer);
					W->GetTimerManager().SetTimer(Self->DialogueHideTimer,
						FTimerDelegate::CreateLambda([WeakSelfDlg2]()
						{
							ABossPhase2CinematicTrigger* S = WeakSelfDlg2.Get();
							if (!S || !S->LocalDialogueWidget) return;
							S->LocalDialogueWidget->HideDialogue();
							S->LocalDialogueWidget = nullptr;
						}),
						FMath::Max(0.5f, Self->DialogueLine2Duration), false);
				}
				UE_LOG(LogTemp, Warning, TEXT("[Phase2CinematicTrigger] Stage 1b — DialogueLine2: %s"),
					*Self->DialogueLine2.ToString());
			}
		}),
		FMath::Max(0.1f, FaceDuration), false);

	// Stage 2 — Front → Top lerp
	World->GetTimerManager().SetTimer(Stage2Timer,
		FTimerDelegate::CreateLambda([WeakSelf]()
		{
			ABossPhase2CinematicTrigger* Self = WeakSelf.Get();
			if (!Self) return;
			Self->CamLerpFromOffset = Self->FrontOffset;
			Self->CamLerpToOffset = Self->TopOffset;
			Self->CamLerpFromLookH = Self->FrontLookHeight;
			Self->CamLerpToLookH = Self->TopLookHeight;
			Self->CamLerpDuration = FMath::Max(0.3f, Self->CameraRiseDuration);
			Self->CamInterpElapsed = 0.f;
			Self->bCameraInterpolating = true;
			UE_LOG(LogTemp, Warning, TEXT("[Phase2CinematicTrigger] Stage 2 — front→top"));
		}),
		FMath::Max(0.1f, StunDuration), false);

	// Stage 4 — Top → Front lerp
	const float Stage4Delay = FMath::Max(0.2f, TotalCinematicDuration - CameraDescentDuration - EndHoldDuration);
	World->GetTimerManager().SetTimer(Stage4Timer,
		FTimerDelegate::CreateLambda([WeakSelf]()
		{
			ABossPhase2CinematicTrigger* Self = WeakSelf.Get();
			if (!Self) return;
			Self->CamLerpFromOffset = Self->TopOffset;
			Self->CamLerpToOffset = Self->FrontOffset;
			Self->CamLerpFromLookH = Self->TopLookHeight;
			Self->CamLerpToLookH = Self->FrontLookHeight;
			Self->CamLerpDuration = FMath::Max(0.3f, Self->CameraDescentDuration);
			Self->CamInterpElapsed = 0.f;
			Self->bCameraInterpolating = true;
			UE_LOG(LogTemp, Warning, TEXT("[Phase2CinematicTrigger] Stage 4 — top→front"));
		}),
		Stage4Delay, false);

	// 시네마틱 종료
	World->GetTimerManager().SetTimer(EndCinematicTimer,
		FTimerDelegate::CreateLambda([WeakSelf]()
		{
			ABossPhase2CinematicTrigger* Self = WeakSelf.Get();
			if (!Self) return;
			Self->Multicast_EndCinematic();
		}),
		FMath::Max(0.5f, TotalCinematicDuration), false);
}

void ABossPhase2CinematicTrigger::Multicast_EndCinematic_Implementation()
{
	UWorld* World = GetWorld();
	if (!World) return;

	bCameraInterpolating = false;
	bCameraHoldStaticDuringFace = false;

	// [Phase2RefactorV2] 보스 NC + Montage 정리 — 모든 머신 (Multicast 라 각 머신 호출).
	if (APawn* Boss = ActiveBoss.Get())
	{
		TArray<UNiagaraComponent*> NCs;
		Boss->GetComponents<UNiagaraComponent>(NCs);
		int32 DeactivatedNCs = 0;
		for (UNiagaraComponent* NC : NCs)
		{
			if (NC && NC->ComponentTags.Contains(FName(TEXT("Phase2Descent"))) && NC->IsActive())
			{
				NC->Deactivate();
				++DeactivatedNCs;
			}
		}

		if (AHellunaEnemyCharacter_Boss* BossCh = Cast<AHellunaEnemyCharacter_Boss>(Boss))
		{
			if (USkeletalMeshComponent* SkelMesh = BossCh->GetMesh())
			{
				if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
				{
					if (BossCh->EnrageMontage && AnimInst->Montage_IsPlaying(BossCh->EnrageMontage))
					{
						AnimInst->Montage_Stop(0.3f, BossCh->EnrageMontage);
					}
				}
			}
		}
		UE_LOG(LogTemp, Warning,
			TEXT("[Phase2RefactorV2] EndCinematic boss cleanup — DeactivatedNCs=%d"),
			DeactivatedNCs);
	}

	// 로컬 PC ExitBossCinematic
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalPlayerController()) continue;
		if (AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(PC))
		{
			HeroPC->ExitBossCinematic(CameraBlendOut);
		}
		break;
	}

	// 카메라 액터 destroy (블렌드 아웃 후)
	TWeakObjectPtr<ACameraActor> WeakCam = LocalCameraActor;
	const float BlendOut = CameraBlendOut;
	FTimerHandle DestroyTimer;
	World->GetTimerManager().SetTimer(DestroyTimer,
		FTimerDelegate::CreateLambda([WeakCam]()
		{
			if (ACameraActor* C = WeakCam.Get()) C->Destroy();
		}),
		FMath::Max(BlendOut + 0.1f, 0.2f), false);
	LocalCameraActor.Reset();

	// 대사 위젯 정리
	if (LocalDialogueWidget)
	{
		LocalDialogueWidget->HideDialogue();
		LocalDialogueWidget = nullptr;
	}

	// 트리거 자체 — 서버에서 destroy (일회용)
	if (HasAuthority())
	{
		SetLifeSpan(FMath::Max(BlendOut + 0.5f, 1.f));
	}

	UE_LOG(LogTemp, Warning, TEXT("[Phase2CinematicTrigger] EndCinematic"));
}

void ABossPhase2CinematicTrigger::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!LocalCameraActor.IsValid()) return;
	APawn* Boss = ActiveBoss.Get();
	if (!Boss) return;

	const FVector BossLoc = Boss->GetActorLocation();
	const FVector Forward = Boss->GetActorForwardVector();
	const FVector Right = Boss->GetActorRightVector();
	const FVector Up = Boss->GetActorUpVector();

	if (bCameraInterpolating)
	{
		CamInterpElapsed += DeltaTime;
		const float Duration = FMath::Max(0.3f, CamLerpDuration);
		const float Alpha = FMath::Clamp(CamInterpElapsed / Duration, 0.f, 1.f);

		const FVector StartCamLoc = BossLoc + Forward * CamLerpFromOffset.X + Right * CamLerpFromOffset.Y + Up * CamLerpFromOffset.Z;
		const FVector EndCamLoc = BossLoc + Forward * CamLerpToOffset.X + Right * CamLerpToOffset.Y + Up * CamLerpToOffset.Z;
		const FVector NewCamLoc = FMath::Lerp(StartCamLoc, EndCamLoc, Alpha);

		const FVector StartLook = BossLoc + FVector(0.f, 0.f, CamLerpFromLookH);
		const FVector EndLook = BossLoc + FVector(0.f, 0.f, CamLerpToLookH);
		const FVector NewLook = FMath::Lerp(StartLook, EndLook, Alpha);

		const FRotator NewRot = (NewLook - NewCamLoc).Rotation();
		LocalCameraActor->SetActorLocationAndRotation(NewCamLoc, NewRot);

		if (Alpha >= 1.f)
		{
			bCameraInterpolating = false;
		}
	}
	else
	{
		// 단계1a — 카메라 spawn 시 위치 그대로 (보스 추적 X). 사용자 의도: 카메라 가만히.
		if (bCameraHoldStaticDuringFace) return;

		// 단계3 (Top hold) + EndHold — ToOffset 위치 유지 + 보스 추적.
		const FVector StaticCamLoc = BossLoc + Forward * CamLerpToOffset.X + Right * CamLerpToOffset.Y + Up * CamLerpToOffset.Z;
		const FVector StaticLook = BossLoc + FVector(0.f, 0.f, CamLerpToLookH);
		const FRotator StaticRot = (StaticLook - StaticCamLoc).Rotation();
		LocalCameraActor->SetActorLocationAndRotation(StaticCamLoc, StaticRot);
	}
}
