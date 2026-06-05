// Capstone Project Helluna

#include "BossEvent/BossDeathCinematicTrigger.h"

#include "BossEvent/BossCinematicCameraUtils.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Controller/HellunaHeroController.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "InputCoreTypes.h"
#include "UI/BossDialogueWidget.h"
#include "Blueprint/UserWidget.h"
#include "GameMode/HellunaDefenseGameMode.h" // [DeathSkipInstantVictoryV1] EndGame 즉시 호출
#include "Kismet/GameplayStatics.h"
#include "Animation/AnimInstance.h"
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "CollisionQueryParams.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

ABossDeathCinematicTrigger::ABossDeathCinematicTrigger()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
}

void ABossDeathCinematicTrigger::BeginPlay()
{
	Super::BeginPlay();

	// [BPDefaultSyncV1] placement instance override 무시.
	SyncFromBPDefault();
}

// [BPDefaultSyncV1] BP CDO 의 Edit-가능 property 를 instance 에 강제 복사.
//   AActor 부모 property 는 skip — placement 위치 reset 방지.
void ABossDeathCinematicTrigger::SyncFromBPDefault()
{
	if (!bSyncFromBPDefault) return;

	UClass* MyClass = GetClass();
	if (!MyClass) return;
	UObject* CDO = MyClass->GetDefaultObject(false);
	if (!CDO || CDO == this) return;

	UClass* SyncBoundary = ABossDeathCinematicTrigger::StaticClass();

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

bool ABossDeathCinematicTrigger::TryActivate(APawn* InTargetBoss)
{
	if (!HasAuthority()) return false;
	if (!InTargetBoss) return false;
	ActiveBoss = InTargetBoss;
	bCinematicActive = true;          // [CinematicSkipVoteV1]
	SkipVoters.Reset();               // [CinematicSkipVoteV1]
	Multicast_StartCinematic(InTargetBoss);
	return true;
}

void ABossDeathCinematicTrigger::Multicast_StartCinematic_Implementation(APawn* Boss)
{
	if (!Boss) return;
	ActiveBoss = Boss;

	UWorld* World = GetWorld();
	if (!World) return;

	// [CinematicSkipVoteV1] 표 플래그 초기화 + 로컬 폰 입력 차단(점프/블록 방지). 스킵 감지는 raw key.
	bLocalSkipVoteSent = false;
	bLocalDialogue2Shown = false; // [CinematicSkipFlowV2] (사망은 미사용)
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (APlayerController* InputPC = World->GetFirstPlayerController())
		{
			if (InputPC->IsLocalController())
			{
				if (APawn* InputPawn = InputPC->GetPawn())
				{
					InputPawn->DisableInput(InputPC);
				}
			}
		}
	}

	// [BossDeathCamNearClipV1] 사망 클로즈업 동안 near clip plane 을 2cm 로 낮춤 (기본 10cm).
	//   거대 보스(스케일 2.2) 의 얼굴/몸에 카메라가 접근할 때 near-plane 이 보스를 잘라내는
	//   순간 끊김을 방지. EndCinematic 에서 10cm 로 복원. 양면 머티리얼이 못 잡는 케이스 =
	//   near-plane 클리핑(픽셀 단위 컬링이라 양면 무관).
	if (IConsoleVariable* NCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SetNearClipPlane")))
	{
		NCVar->Set(2.f);
	}

	// =========================================================
	// 카메라 spawn — 첫 Tick 위치 = head 본 기준 face close-up
	// =========================================================
	const FVector BossLoc = Boss->GetActorLocation();

	// 초기 anchor — head 본이 있으면 head, 없으면 보스 위치 + 기본 90cm.
	//   [BossDeathCamHeadFrameV1] 카메라 오프셋 basis 를 보스 액터 축이 아니라 head 본의 회전
	//   기준으로 적용 — 보스가 쓰러져 머리가 기울어도 카메라가 항상 얼굴 정면에 위치 →
	//   카메라가 쓰러진 몸통 안으로 들어가지 않음. (FaceOffset 은 head 본 로컬 프레임 기준)
	FVector Anchor = BossLoc + FVector(0.f, 0.f, 90.f);
	FQuat HeadQuat = Boss->GetActorQuat();
	if (ACharacter* BossChar = Cast<ACharacter>(Boss))
	{
		if (USkeletalMeshComponent* SkelMesh = BossChar->GetMesh())
		{
			if (HeadBoneName != NAME_None && SkelMesh->DoesSocketExist(HeadBoneName))
			{
				const FTransform HeadXform = SkelMesh->GetSocketTransform(HeadBoneName);
				Anchor = FMath::Lerp(BossLoc + FVector(0.f, 0.f, 90.f), HeadXform.GetLocation(), FMath::Clamp(HeadFollowAlpha, 0.f, 1.f));
				HeadQuat = HeadXform.GetRotation();
			}
		}
	}

	const FVector StartCamLoc = Anchor + HeadQuat.RotateVector(FaceOffset);
	const FVector StartLookTarget = Anchor + HeadQuat.RotateVector(FVector(0.f, 0.f, LookAtZOffset));
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

	AActor* ViewTarget = CamActor ? (AActor*)CamActor : (AActor*)Boss;

	// =========================================================
	// 로컬 PC 시네마틱 진입
	// =========================================================
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalPlayerController()) continue;
		if (AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(PC))
		{
			// [DeathInstantCutV1] 사망 시네마틱은 instant cut — CameraBlendIn=0 허용 (Max guard 제거).
			HeroPC->EnterBossCinematic(ViewTarget, FMath::Max(CameraBlendIn, 0.f));
		}

		// [CinematicSkipVoteV1] 스킵 카운터(+선택 대사) 위젯 — 사망 시네마틱에도 "PRESS SPACE TO CONTINUE [n/N]".
		if (DialogueWidgetClass && GetNetMode() != NM_DedicatedServer)
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
				LocalDialogueWidget->SetSkipCount(0, CountActivePlayers());
				LocalDialogueWidget->SetPromptSkipMode(true); // [CinematicSkipFlowV2] 사망은 대사 1개 → 다음 스페이스=스킵
			}
		}
		break;
	}

	// =========================================================
	// 종료 timer
	// =========================================================
	TWeakObjectPtr<ABossDeathCinematicTrigger> WeakSelf(this);
	World->GetTimerManager().SetTimer(EndCinematicTimer,
		FTimerDelegate::CreateLambda([WeakSelf]()
		{
			ABossDeathCinematicTrigger* Self = WeakSelf.Get();
			if (!Self) return;
			Self->Multicast_EndCinematic();
		}),
		FMath::Max(0.5f, TotalCinematicDuration), false);

	UE_LOG(LogTemp, Warning,
		TEXT("[DeathTimingV1][CinematicStart] Boss=%s WorldTime=%.3f Duration=%.1fs"),
		*GetNameSafe(Boss),
		World ? World->GetTimeSeconds() : -1.f,
		TotalCinematicDuration);
}

void ABossDeathCinematicTrigger::Multicast_EndCinematic_Implementation()
{
	UWorld* World = GetWorld();
	if (!World) return;

	bCinematicActive = false; // [CinematicSkipVoteV1] (서버 가드 종료)
	World->GetTimerManager().ClearTimer(EndCinematicTimer); // 스킵 조기 종료 시 잔여 타이머 정리(정상 시 no-op)

	// [CinematicSkipVoteV1] 폰 입력 복원 + 스킵 카운터 위젯 정리.
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (APlayerController* InputPC = World->GetFirstPlayerController())
		{
			if (InputPC->IsLocalController())
			{
				if (APawn* InputPawn = InputPC->GetPawn())
				{
					InputPawn->EnableInput(InputPC);
				}
			}
		}
	}
	if (LocalDialogueWidget)
	{
		LocalDialogueWidget->HideDialogue();
		LocalDialogueWidget = nullptr;
	}

	// [BossDeathCamNearClipV1] near clip plane 을 기본값(10cm) 으로 복원.
	if (IConsoleVariable* NCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SetNearClipPlane")))
	{
		NCVar->Set(10.f);
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

	if (HasAuthority())
	{
		SetLifeSpan(FMath::Max(BlendOut + 0.5f, 1.f));
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[DeathTimingV1][CinematicEnd] WorldTime=%.3f"),
		World ? World->GetTimeSeconds() : -1.f);
}

// =========================================================================================
// [CinematicSkipVoteV1] 스킵 투표 — 전원 일치 시 카메라 종료(게임오버는 독립 진행).
// =========================================================================================
int32 ABossDeathCinematicTrigger::CountActivePlayers() const
{
	int32 Count = 0;
	if (const UWorld* World = GetWorld())
	{
		if (const AGameStateBase* GS = World->GetGameState())
		{
			Count = GS->PlayerArray.Num();
		}
		else
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				if (It->IsValid()) ++Count;
			}
		}
	}
	return FMath::Max(Count, 1);
}

void ABossDeathCinematicTrigger::ServerRegisterSkipVote(APlayerController* Voter)
{
	if (!HasAuthority() || !bCinematicActive || !IsValid(Voter)) return;

	SkipVoters.Add(Voter);
	int32 Voted = 0;
	for (const TWeakObjectPtr<APlayerController>& V : SkipVoters)
	{
		if (V.IsValid()) ++Voted;
	}
	const int32 Total = CountActivePlayers();

	UE_LOG(LogTemp, Warning, TEXT("[CinematicSkipVoteV1] (Death) vote %d/%d from %s"),
		Voted, Total, *GetNameSafe(Voter));

	Multicast_UpdateSkipCount(Voted, Total);

	if (Voted >= Total)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CinematicSkipVoteV1] (Death) all voted → skip cinematic → 즉시 승리"));

		// [DeathSkipInstantVictoryV1] 사망 스킵 = 즉시 승리.
		//   보스 즉시 숨김(소멸 dissolve 연출 생략) + 사망 몽타주 정지(85% dissolve/탑다운 카메라 트리거 방지)
		//   + EndGame(Escaped) 즉시 호출(GameMode 의 5초 딜레이 우회; 자연 종료 경로의 5초 EndGame 은 bGameEnded 로 no-op).
		if (APawn* BossPawn = ActiveBoss.Get())
		{
			BossPawn->SetActorHiddenInGame(true); // 서버 → 복제. 보스 즉시 사라짐.
			if (ACharacter* BossChar = Cast<ACharacter>(BossPawn))
			{
				if (USkeletalMeshComponent* SK = BossChar->GetMesh())
				{
					if (UAnimInstance* AnimInst = SK->GetAnimInstance())
					{
						AnimInst->StopAllMontages(0.05f); // dissolve 트리거(몽타주 85%) 방지 + GA 사망 마무리 유도
					}
				}
			}
		}
		// [DeathSkipVictoryDelayV1] 스킵 직후 즉시 말고 한 박자(2초) 뒤 결과 화면 — 너무 갑작스럽지 않게.
		//   타이머는 월드 TM + GameMode 캡처라 트리거가 먼저 파괴돼도 발화. 자연종료 경로의 5초 EndGame 은 bGameEnded no-op.
		if (UWorld* VictoryWorld = GetWorld())
		{
			if (AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(UGameplayStatics::GetGameMode(this)))
			{
				TWeakObjectPtr<AHellunaDefenseGameMode> WeakGM(GM);
				FTimerHandle VictoryDelayTimer;
				VictoryWorld->GetTimerManager().SetTimer(VictoryDelayTimer,
					FTimerDelegate::CreateLambda([WeakGM]()
					{
						if (AHellunaDefenseGameMode* G = WeakGM.Get())
						{
							G->EndGame(EHellunaGameEndReason::Escaped);
						}
					}),
					2.0f, false);
			}
		}

		Multicast_EndCinematic();
	}
}

void ABossDeathCinematicTrigger::Multicast_UpdateSkipCount_Implementation(int32 Voted, int32 Total)
{
	if (LocalDialogueWidget)
	{
		LocalDialogueWidget->SetSkipCount(Voted, Total);
	}
}

void ABossDeathCinematicTrigger::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// [CinematicSkipVoteV1] 로컬 스페이스바 → 서버에 스킵 1표. (점프/블록은 DisableInput 으로 안 나감)
	if (LocalCameraActor.IsValid() && !bLocalSkipVoteSent)
	{
		if (UWorld* SkipW = GetWorld())
		{
			if (APlayerController* SkipPC = SkipW->GetFirstPlayerController())
			{
				if (SkipPC->IsLocalController() && SkipPC->WasInputKeyJustPressed(EKeys::SpaceBar))
				{
					// [CinematicSkipFlowV2] 사망은 대사 1개 → ① 타이핑 완성 → ② 시네마틱 스킵.
					if (LocalDialogueWidget && LocalDialogueWidget->IsTyping())
					{
						LocalDialogueWidget->CompleteTyping();
					}
					else
					{
						bLocalSkipVoteSent = true;
						if (AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(SkipPC))
						{
							HeroPC->Server_VoteBossSummonSkip();
						}
					}
				}
			}
		}
	}

	if (!LocalCameraActor.IsValid()) return;
	APawn* Boss = ActiveBoss.Get();
	if (!Boss) return;

	// [BossDeathCamHeadFrameV1] 매 Tick 카메라를 head bone 의 위치+회전에 맞춰 갱신 —
	//   오프셋 basis 가 head 본 회전이라 보스가 어떻게 쓰러져도 카메라가 얼굴 정면 유지.
	const FVector BossLoc = Boss->GetActorLocation();

	FVector Anchor = BossLoc + FVector(0.f, 0.f, 90.f);
	FQuat HeadQuat = Boss->GetActorQuat();
	if (ACharacter* BossChar = Cast<ACharacter>(Boss))
	{
		if (USkeletalMeshComponent* SkelMesh = BossChar->GetMesh())
		{
			if (HeadBoneName != NAME_None && SkelMesh->DoesSocketExist(HeadBoneName))
			{
				const FTransform HeadXform = SkelMesh->GetSocketTransform(HeadBoneName);
				Anchor = FMath::Lerp(BossLoc + FVector(0.f, 0.f, 90.f), HeadXform.GetLocation(), FMath::Clamp(HeadFollowAlpha, 0.f, 1.f));
				HeadQuat = HeadXform.GetRotation();
			}
		}
	}

	const FVector RawCamLoc = Anchor + HeadQuat.RotateVector(FaceOffset);
	const FVector LookTarget = Anchor + HeadQuat.RotateVector(FVector(0.f, 0.f, LookAtZOffset));

	// [CinematicCameraOcclusionV1] G — Ground Z floor.
	//   사망 시네마틱이 head 본을 따라가다 보스가 쓰러지면 카메라가 land 와 같은 높이로 내려가
	//   지형을 뚫는 케이스 → BossLoc 에서 down trace 후 ground+margin Z 로 clamp.
	//   actor occluder 는 의도된 close-up 망가뜨릴 수 있어 G 만 적용 (A 미적용).
	TArray<AActor*> IgnoreActors;
	IgnoreActors.Reserve(2);
	IgnoreActors.Add(Boss);
	IgnoreActors.Add(LocalCameraActor.Get());
	// [BossDeathGroundMarginV1] MarginZ 를 100 → 25 로 축소. 100 은 머리가 바닥 근처(Z~80) 로
	//   떨어지는 사망 클로즈업에서 카메라를 머리 위 ~1m 까지 강제로 들어올려, 의도한 face
	//   close-up 대신 멀리서 보는 탑다운이 돼 보스가 작게 보이고 잠깐씩 가림에도 "사라진"
	//   것처럼 보임 (패키지 로그 확인). 25 면 머리를 따라 내려갈 수 있으면서 지면 침투는 방지.
	FVector CamLoc = BossCinematicCameraUtils::ClampCameraAboveGround(
		this, RawCamLoc, BossLoc, IgnoreActors, /*MarginZ=*/25.f);

	// [BossDeathCamOccluderV1] 머리→카메라 라인에 지형/액터가 끼면 카메라를 그 앞쪽으로 당김.
	//   ClampCameraAboveGround 는 카메라 Z 만 처리 → 카메라가 언덕/벽 뒤로 들어가는 경우는 못 잡음 →
	//   패키지에서 맵에 따라 보스가 간헐적으로 가려지는 문제 발생. ECC_Visibility 트레이스로
	//   지형(Landscape)+액터 가림을 잡아 카메라를 가림 직전으로 끌어옴. 너무 보스에 붙지 않게 최소
	//   거리 보장. (양면 머티리얼이 못 잡는 케이스 = 카메라가 보스 안이 아니라 지형 뒤에 있을 때)
	if (UWorld* OccWorld = GetWorld())
	{
		FCollisionQueryParams OccParams(SCENE_QUERY_STAT(BossDeathCamOccluder), false);
		OccParams.bTraceComplex = false;
		OccParams.AddIgnoredActor(Boss);
		if (AActor* CamAct = LocalCameraActor.Get()) OccParams.AddIgnoredActor(CamAct);
		FHitResult OccHit;
		if (OccWorld->LineTraceSingleByChannel(OccHit, LookTarget, CamLoc, ECC_Visibility, OccParams))
		{
			constexpr float OccMargin = 15.f;
			constexpr float MinDistFromTarget = 30.f;
			const FVector OccDir = (CamLoc - LookTarget).GetSafeNormal();
			FVector Pushed = OccHit.ImpactPoint - OccDir * OccMargin;
			if (FVector::Dist(Pushed, LookTarget) < MinDistFromTarget)
			{
				Pushed = LookTarget + OccDir * MinDistFromTarget;
			}
			CamLoc = Pushed;
		}
	}

	const FRotator NewRot = (LookTarget - CamLoc).Rotation();
	LocalCameraActor->SetActorLocationAndRotation(CamLoc, NewRot);

	// [BossDeathCamDiagV1] 사망 카메라 진단 — 보스 투명화가 카메라 클램프/near-clip 과
	//   연관 있는지 확인. ClampCameraAboveGround 가 카메라를 보스 메시 박스 안으로 밀어넣으면
	//   카메라 near-clip 평면이 보스를 잘라내 "완전 투명" 으로 보임.
	//   CamInMeshBox=1 또는 DistToMeshBox 가 near-clip(~10cm) 이하면 그게 원인.
	if (UWorld* DiagWorld = GetWorld())
	{
		const double Now = DiagWorld->GetTimeSeconds();
		static double LastDeathCamDiag = -100.0;
		if (Now - LastDeathCamDiag >= 0.2)
		{
			LastDeathCamDiag = Now;
			const bool bClamped = !FMath::IsNearlyEqual(RawCamLoc.Z, CamLoc.Z, 1.0f);
			float CamInMeshBox = 0.f;
			float DistToMeshBox = -1.f;
			if (ACharacter* DiagChar = Cast<ACharacter>(Boss))
			{
				if (USkeletalMeshComponent* DiagSK = DiagChar->GetMesh())
				{
					const FBox MeshBox = DiagSK->Bounds.GetBox();
					CamInMeshBox = MeshBox.IsInside(CamLoc) ? 1.f : 0.f;
					DistToMeshBox = FMath::Sqrt(MeshBox.ComputeSquaredDistanceToPoint(CamLoc));
				}
			}
			UE_LOG(LogTemp, Warning,
				TEXT("[BossDeathCamDiag] t=%.2f Clamped=%d Cam->Head=%.1f Cam->Boss=%.1f CamInMeshBox=%.0f DistToMeshBox=%.1f HeadZ=%.1f CamZ=%.1f RawCamZ=%.1f"),
				Now, bClamped ? 1 : 0,
				FVector::Dist(CamLoc, Anchor), FVector::Dist(CamLoc, BossLoc),
				CamInMeshBox, DistToMeshBox, Anchor.Z, CamLoc.Z, RawCamLoc.Z);
		}
	}
}
