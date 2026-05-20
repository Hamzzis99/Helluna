// Capstone Project Helluna

#include "BossEvent/BossPhase2CinematicTrigger.h"

#include "BossEvent/BossCinematicCameraUtils.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Controller/HellunaHeroController.h"
#include "UI/BossDialogueWidget.h"
#include "Character/HellunaEnemyCharacter_Boss.h"
#include "NiagaraComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "AIController.h"
#include "BrainComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Blueprint/UserWidget.h"

// [Phase2FaceMatchDeathV1] 단계1a 만 head 본 anchor 사용 — 사망 시네마틱과 동일 close-up 구도.
FVector ABossPhase2CinematicTrigger::ComputeFaceAnchor(const APawn* Boss) const
{
	if (!Boss) return FVector::ZeroVector;
	const FVector BossLoc = Boss->GetActorLocation();
	if (!bFaceUseHeadBone) return BossLoc;

	if (const ACharacter* BossChar = Cast<ACharacter>(Boss))
	{
		if (USkeletalMeshComponent* SK = BossChar->GetMesh())
		{
			if (FaceHeadBoneName != NAME_None && SK->DoesSocketExist(FaceHeadBoneName))
			{
				const FVector HeadLoc = SK->GetSocketLocation(FaceHeadBoneName);
				const FVector FallbackLoc = BossLoc + FVector(0.f, 0.f, 90.f);
				return FMath::Lerp(FallbackLoc, HeadLoc, FMath::Clamp(FaceHeadFollowAlpha, 0.f, 1.f));
			}
		}
	}
	return BossLoc + FVector(0.f, 0.f, 90.f);
}

ABossPhase2CinematicTrigger::ABossPhase2CinematicTrigger()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
}

void ABossPhase2CinematicTrigger::BeginPlay()
{
	Super::BeginPlay();

	// [BPDefaultSyncV1] placement instance override 무시.
	SyncFromBPDefault();
}

// [BPDefaultSyncV1] BP CDO 의 Edit-가능 property 를 instance 에 강제 복사.
//   AActor 부모 property 는 skip — placement 위치 reset 방지.
void ABossPhase2CinematicTrigger::SyncFromBPDefault()
{
	if (!bSyncFromBPDefault) return;

	UClass* MyClass = GetClass();
	if (!MyClass) return;
	UObject* CDO = MyClass->GetDefaultObject(false);
	if (!CDO || CDO == this) return;

	UClass* SyncBoundary = ABossPhase2CinematicTrigger::StaticClass();

	static const TSet<FName> SkipProps = {
		TEXT("bSyncFromBPDefault"),
	};

	int32 Synced = 0;
	for (TFieldIterator<FProperty> It(MyClass); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop) continue;
		if (!Prop->HasAnyPropertyFlags(CPF_Edit)) continue;
		if (SkipProps.Contains(Prop->GetFName())) continue;
		UClass* OwnerClass = Prop->GetOwnerClass();
		if (!OwnerClass || !OwnerClass->IsChildOf(SyncBoundary)) continue;
		Prop->CopyCompleteValue_InContainer(this, CDO);
		++Synced;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[BPDefaultSyncV1] %s synced %d properties from BP CDO"),
		*GetName(), Synced);
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
	//   [Phase2FaceMatchDeathV1] bFaceUseHeadBone 이면 anchor = head 본 위치 (사망 시네마틱과 동일).
	//   FaceLookHeight 도 anchor 기준 — head 본 위치에서 추가 Z offset.
	// =========================================================
	const FVector Forward = Boss->GetActorForwardVector();
	const FVector Right = Boss->GetActorRightVector();
	const FVector Up = Boss->GetActorUpVector();
	const FVector FaceAnchor = ComputeFaceAnchor(Boss);

	const FVector StartCamLoc = FaceAnchor + Forward * FaceOffset.X + Right * FaceOffset.Y + Up * FaceOffset.Z;
	const FVector StartLookTarget = FaceAnchor + FVector(0.f, 0.f, FaceLookHeight);
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
			UE_LOG(LogTemp, Warning,
				TEXT("[Phase2CinematicTrigger] Stage 2 — front→top, RiseDuration=%.2f (CDO=%.2f), TotalCinematic=%.2f"),
				Self->CamLerpDuration, Self->CameraRiseDuration, Self->TotalCinematicDuration);
		}),
		FMath::Max(0.1f, StunDuration), false);

	// Stage 4 — Top → Front lerp
	//   [Phase2TopHoldV1] Stage 4 시작 = Stun + Rise + TopHold (명시적 계산)
	//   TotalCinematicDuration 은 마지막 EndHold 까지 포함한 전체 길이여야 함.
	const float Stage4Delay = FMath::Max(0.2f, StunDuration + CameraRiseDuration + TopHoldDuration);
	World->GetTimerManager().SetTimer(Stage4Timer,
		FTimerDelegate::CreateLambda([WeakSelf]()
		{
			ABossPhase2CinematicTrigger* Self = WeakSelf.Get();
			if (!Self) return;
			Self->CamLerpFromOffset = Self->TopOffset;
			Self->CamLerpToOffset = Self->Stage4ReturnOffset; // Stage 1b 의 FrontOffset 과 분리 — 더 멀리.
			Self->CamLerpFromLookH = Self->TopLookHeight;
			Self->CamLerpToLookH = Self->Stage4ReturnLookHeight;
			Self->CamLerpDuration = FMath::Max(0.3f, Self->CameraDescentDuration);
			Self->CamInterpElapsed = 0.f;
			Self->bCameraInterpolating = true;
			UE_LOG(LogTemp, Warning, TEXT("[Phase2CinematicTrigger] Stage 4 — top→Stage4Return"));
		}),
		Stage4Delay, false);

	// [Phase2EndDerivedV1] 시네마틱 종료 = Stage4Delay + CameraDescentDuration + EndHoldDuration (도출).
	//   기존엔 TotalCinematicDuration UPROPERTY 를 그대로 사용 → CameraRiseDuration 늘려도 EndHold 자동 흡수.
	//   이제 EndHoldDuration 이 정확히 그 시간만큼만 hold + RiseDuration 늘려도 시퀀스 자연 확장.
	const float ComputedTotalDuration = Stage4Delay + CameraDescentDuration + EndHoldDuration;
	World->GetTimerManager().SetTimer(EndCinematicTimer,
		FTimerDelegate::CreateLambda([WeakSelf]()
		{
			ABossPhase2CinematicTrigger* Self = WeakSelf.Get();
			if (!Self) return;
			Self->Multicast_EndCinematic();
		}),
		FMath::Max(0.5f, ComputedTotalDuration), false);

	UE_LOG(LogTemp, Warning,
		TEXT("[Phase2EndDerivedV1] Stage4Delay=%.2f, ComputedTotal=%.2f (TotalCinematicDuration UPROPERTY=%.2f, ignored)"),
		Stage4Delay, ComputedTotalDuration, TotalCinematicDuration);
}

void ABossPhase2CinematicTrigger::Multicast_EndCinematic_Implementation()
{
	UWorld* World = GetWorld();
	if (!World) return;

	bCameraInterpolating = false;
	bCameraHoldStaticDuringFace = false;

	// [Phase2ShakeCleanupV2] 시네마틱 종료 즉시 보스 카메라 쉐이크 정리 (모든 머신).
	//   각 머신의 자기 보스 instance 의 RepeatTimer + 진행 중 instance fade out.
	if (APawn* Boss = ActiveBoss.Get())
	{
		if (AHellunaEnemyCharacter_Boss* BossEnemy = Cast<AHellunaEnemyCharacter_Boss>(Boss))
		{
			BossEnemy->StopPhase2Shakes();
		}
	}

	// [Phase2RefactorV2] 보스 NC + Montage 정리는 cinematic_end + 0.5s (Brain 재가동과 같은 시점)
	//   에 통합 — 보스가 움직일 수 있는 시점에 강하 VFX 도 같이 사라짐.

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

	// [Phase2BrainResumeV1] 시네마틱 종료 0.5초 후 정리 + AI 재개 — 모든 머신 (각자 local timer).
	//   - 모든 머신: Phase2Descent NC 비활성화 + EnrageMontage 정지 (visual cleanup)
	//   - 서버만: Brain 재가동 (StateTree/AI 재개)
	//   동일 시점 통합 — 보스가 움직일 수 있는 시점에 강하 VFX 도 함께 사라짐.
	{
		TWeakObjectPtr<APawn> WeakBoss = ActiveBoss;
		FTimerHandle BrainResumeTimer;
		World->GetTimerManager().SetTimer(BrainResumeTimer,
			FTimerDelegate::CreateLambda([WeakBoss]()
			{
				APawn* B = WeakBoss.Get();
				if (!B) return;

				// [Phase2DescentAftermathV1] 강하 VFX — deactivate 대신 레이저 제거 변형으로 swap.
				//   회오리는 페이즈2 동안 루프 유지 (보스 가시성 테스트). 변형 미지정 시 helper 가 Deactivate fallback.
				if (AHellunaEnemyCharacter_Boss* BossCh = Cast<AHellunaEnemyCharacter_Boss>(B))
				{
					BossCh->SwapDescentVFXToAftermath();
					if (USkeletalMeshComponent* SK = BossCh->GetMesh())
					{
						if (UAnimInstance* AI = SK->GetAnimInstance())
						{
							if (BossCh->EnrageMontage && AI->Montage_IsPlaying(BossCh->EnrageMontage))
							{
								AI->Montage_Stop(0.3f, BossCh->EnrageMontage);
							}
						}
					}
				}

				// 서버만: Brain 재가동
				if (B->HasAuthority())
				{
					if (AAIController* AIC = Cast<AAIController>(B->GetController()))
					{
						if (UBrainComponent* Brain = AIC->GetBrainComponent())
						{
							Brain->StartLogic();
						}
					}
				}

				UE_LOG(LogTemp, Warning,
					TEXT("[Phase2BrainResumeV1] +0.5s cleanup on %s — descent VFX→aftermath swap, BrainResume=%d"),
					*GetNameSafe(B), B->HasAuthority() ? 1 : 0);
			}),
			0.5f, false);
	}

	// 트리거 자체 — 서버에서 destroy (일회용)
	if (HasAuthority())
	{
		// BrainResumeTimer 가 0.5s 후 fire 해야 하므로 lifespan 충분히 길게.
		SetLifeSpan(FMath::Max(BlendOut + 1.0f, 1.5f));
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
		const FVector RawCamLoc = FMath::Lerp(StartCamLoc, EndCamLoc, Alpha);

		const FVector StartLook = BossLoc + FVector(0.f, 0.f, CamLerpFromLookH);
		const FVector EndLook = BossLoc + FVector(0.f, 0.f, CamLerpToLookH);
		const FVector NewLook = FMath::Lerp(StartLook, EndLook, Alpha);

		// [CinematicCameraOcclusionV1] A — 액터 occluder push-back. NewLook 을 target 으로.
		TArray<AActor*> IgnoreActors;
		IgnoreActors.Reserve(3);
		IgnoreActors.Add(Boss);
		IgnoreActors.Add(LocalCameraActor.Get());
		IgnoreActors.Add(this);
		const FVector NewCamLoc = BossCinematicCameraUtils::PushCameraOutOfActorOccluders(
			this, RawCamLoc, NewLook, IgnoreActors, /*Margin=*/30.f);

		const FRotator NewRot = (NewLook - NewCamLoc).Rotation();
		LocalCameraActor->SetActorLocationAndRotation(NewCamLoc, NewRot);

		// [Phase2LerpDiag] 매 1초 단위로 Alpha 진행 로그 (lerp 가 정말 Duration 동안 진행되는지 검증).
		if (FMath::FloorToInt(CamInterpElapsed) > FMath::FloorToInt(CamInterpElapsed - DeltaTime))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Phase2LerpDiag] elapsed=%.2f / duration=%.2f, alpha=%.3f, dt=%.4f, camZ=%.0f"),
				CamInterpElapsed, Duration, Alpha, DeltaTime, NewCamLoc.Z);
		}

		if (Alpha >= 1.f)
		{
			bCameraInterpolating = false;
			UE_LOG(LogTemp, Warning,
				TEXT("[Phase2LerpDiag] Lerp DONE — total elapsed=%.2f (target duration=%.2f)"),
				CamInterpElapsed, Duration);
		}
	}
	else
	{
		// 단계1a — head bone anchor 추적 (사망 시네마틱과 동일 close-up).
		//   [Phase2FaceMatchDeathV1] bFaceUseHeadBone=true 면 매 Tick head 위치 따라감.
		//   false 면 spawn 위치 그대로 (기존 정적 동작).
		if (bCameraHoldStaticDuringFace)
		{
			if (!bFaceUseHeadBone) return;

			// [Phase2CamLocalOnlyV1] 로컬 PC viewport 가 있는 머신에서만 카메라 update.
			//   PIE listen-server 에서 server world + client world 두 인스턴스가 동시 Tick 하며
			//   각자 카메라 set → 한 viewport 에서 두 위치가 oscillate, head 추적이 부분적으로만 됨.
			//   해결: 자기 world 에 local player 가 있을 때만 카메라 update (해당 viewport 의 카메라만 갱신).
			//   data flow: server-only world (dedicated server) 는 어차피 viewport X.
			//              listen-server 머신 = local player 있음 = 카메라 update O.
			//              client 머신 = local player 있음 = 카메라 update O.
			if (UWorld* W = GetWorld())
			{
				bool bHasLocal = false;
				for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
				{
					if (APlayerController* PC = It->Get())
					{
						if (PC->IsLocalController())
						{
							bHasLocal = true;
							break;
						}
					}
				}
				if (!bHasLocal) return;
			}

			const FVector FaceAnchor = ComputeFaceAnchor(Boss);
			const FVector RawFaceCamLoc = FaceAnchor + Forward * FaceOffset.X + Right * FaceOffset.Y + Up * FaceOffset.Z;
			const FVector FaceLook = FaceAnchor + FVector(0.f, 0.f, FaceLookHeight);

			// [CinematicCameraOcclusionV1] A — 액터 occluder push-back.
			TArray<AActor*> FaceIgnore;
			FaceIgnore.Reserve(3);
			FaceIgnore.Add(Boss);
			FaceIgnore.Add(LocalCameraActor.Get());
			FaceIgnore.Add(this);
			const FVector FaceCamLoc = BossCinematicCameraUtils::PushCameraOutOfActorOccluders(
				this, RawFaceCamLoc, FaceLook, FaceIgnore, /*Margin=*/30.f);

			const FRotator FaceRot = (FaceLook - FaceCamLoc).Rotation();
			LocalCameraActor->SetActorLocationAndRotation(FaceCamLoc, FaceRot);

			// [Phase2FaceCam_Diag] 0.2s 마다 head bone, anchor, 카메라 set 위치, 실제 카메라 위치 비교.
			//   set 직후 GetActorLocation 이 set 한 값과 같은지 — 다르면 다른 곳에서 카메라 위치 덮어쓰기.
			//   head bone 의 vs Boss ActorLocation Z 차이 — head 가 보스 머리 높이만큼 (~150) 위에 있어야 정상.
			{
				FaceCamDiagAccum += DeltaTime;
				if (FaceCamDiagAccum >= 0.2f)
				{
					FaceCamDiagAccum = 0.f;
					FVector HeadSocketLoc = FVector::ZeroVector;
					bool bHasHeadSocket = false;
					if (const ACharacter* BossChar = Cast<ACharacter>(Boss))
					{
						if (USkeletalMeshComponent* SK = BossChar->GetMesh())
						{
							if (FaceHeadBoneName != NAME_None && SK->DoesSocketExist(FaceHeadBoneName))
							{
								HeadSocketLoc = SK->GetSocketLocation(FaceHeadBoneName);
								bHasHeadSocket = true;
							}
						}
					}
					const FVector ActualCam = LocalCameraActor->GetActorLocation();
					const ENetMode NM = GetWorld() ? GetWorld()->GetNetMode() : NM_MAX;
					const TCHAR* NMStr = (NM == NM_Standalone) ? TEXT("Stand")
						: (NM == NM_Client) ? TEXT("Client")
						: (NM == NM_DedicatedServer) ? TEXT("DedSrv")
						: (NM == NM_ListenServer) ? TEXT("Listen") : TEXT("?");
					UE_LOG(LogTemp, Warning,
						TEXT("[Phase2FaceCam_Diag] NM=%s BossLoc=(%.0f,%.0f,%.0f) HeadOK=%d HeadLoc=(%.0f,%.0f,%.0f) FaceAnchor=(%.0f,%.0f,%.0f) SetCam=(%.0f,%.0f,%.0f) ActualCam=(%.0f,%.0f,%.0f) Δ=(%.1f,%.1f,%.1f) UseHead=%d Alpha=%.2f"),
						NMStr,
						BossLoc.X, BossLoc.Y, BossLoc.Z,
						bHasHeadSocket ? 1 : 0,
						HeadSocketLoc.X, HeadSocketLoc.Y, HeadSocketLoc.Z,
						FaceAnchor.X, FaceAnchor.Y, FaceAnchor.Z,
						FaceCamLoc.X, FaceCamLoc.Y, FaceCamLoc.Z,
						ActualCam.X, ActualCam.Y, ActualCam.Z,
						(ActualCam.X - FaceCamLoc.X), (ActualCam.Y - FaceCamLoc.Y), (ActualCam.Z - FaceCamLoc.Z),
						bFaceUseHeadBone ? 1 : 0,
						FaceHeadFollowAlpha);
				}
			}
			return;
		}

		// 단계3 (Top hold) + EndHold — ToOffset 위치 유지 + 보스 추적.
		const FVector RawStaticCamLoc = BossLoc + Forward * CamLerpToOffset.X + Right * CamLerpToOffset.Y + Up * CamLerpToOffset.Z;
		const FVector StaticLook = BossLoc + FVector(0.f, 0.f, CamLerpToLookH);

		// [CinematicCameraOcclusionV1] A — 액터 occluder push-back.
		TArray<AActor*> IgnoreActors;
		IgnoreActors.Reserve(3);
		IgnoreActors.Add(Boss);
		IgnoreActors.Add(LocalCameraActor.Get());
		IgnoreActors.Add(this);
		const FVector StaticCamLoc = BossCinematicCameraUtils::PushCameraOutOfActorOccluders(
			this, RawStaticCamLoc, StaticLook, IgnoreActors, /*Margin=*/30.f);

		const FRotator StaticRot = (StaticLook - StaticCamLoc).Rotation();
		LocalCameraActor->SetActorLocationAndRotation(StaticCamLoc, StaticRot);
	}
}
