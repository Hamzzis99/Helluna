// Source/Helluna/Private/Minwoo/BossSummonCinematicTrigger.cpp
// LiveCoding nudge: 2026-04-24

#include "Minwoo/BossSummonCinematicTrigger.h"

#include "AIController.h"
#include "BrainComponent.h"
#include "Camera/CameraActor.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Controller/HellunaHeroController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieScene.h"
#include "MovieSceneFwd.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSequencePlaybackSettings.h"
#include "MovieSceneObjectBindingID.h"
#include "Sections/MovieSceneSubSection.h"
#include "Engine/SceneCapture.h"
#include "TimerManager.h"
#include "UI/BossDialogueWidget.h"
#include "Blueprint/UserWidget.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"

ABossSummonCinematicTrigger::ABossSummonCinematicTrigger()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);
}

void ABossSummonCinematicTrigger::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && bAutoActivateOnBeginPlay)
	{
		// 모드 1: 태그 보스 스폰 이벤트 대기 (이벤트 기반)
		if (bWaitForTaggedBossSpawn && !ExistingBossActorTag.IsNone())
		{
			// 이미 월드에 있으면 곧바로 폴백 타이머 사용 (스폰 이벤트가 안 옴)
			if (APawn* Existing = ResolveTargetBoss())
			{
				const float Delay = FMath::Max(AutoActivateDelay, 0.1f);
				GetWorldTimerManager().SetTimer(AutoActivateTimer, this,
					&ABossSummonCinematicTrigger::HandleAutoActivate, Delay, false);

				UE_LOG(LogTemp, Warning,
					TEXT("[BossSummonCinematic_LiveCodeCheck] Boss already exists (%s) — fallback timer Delay=%.2fs"),
					*Existing->GetName(), Delay);
			}
			else if (UWorld* World = GetWorld())
			{
				FOnActorSpawned::FDelegate D = FOnActorSpawned::FDelegate::CreateUObject(
					this, &ABossSummonCinematicTrigger::OnActorSpawnedInWorld);
				ActorSpawnedHandle = World->AddOnActorSpawnedHandler(D);

				UE_LOG(LogTemp, Warning,
					TEXT("[BossSummonCinematic_LiveCodeCheck] Listening for actor spawn with Tag=%s"),
					*ExistingBossActorTag.ToString());
			}
		}
		// 모드 2: 고정 타이머 폴백 (구버전 방식)
		else
		{
			const float Delay = FMath::Max(AutoActivateDelay, 0.1f);
			GetWorldTimerManager().SetTimer(AutoActivateTimer, this,
				&ABossSummonCinematicTrigger::HandleAutoActivate, Delay, false);

			UE_LOG(LogTemp, Warning,
				TEXT("[BossSummonCinematic_LiveCodeCheck] AutoActivate scheduled (timer mode) — Delay=%.2fs, Tag=%s"),
				Delay, *ExistingBossActorTag.ToString());
		}
	}
}

void ABossSummonCinematicTrigger::OnActorSpawnedInWorld(AActor* SpawnedActor)
{
	if (!HasAuthority() || bCinematicActive || !IsValid(SpawnedActor))
	{
		return;
	}

	if (ExistingBossActorTag.IsNone() || !SpawnedActor->ActorHasTag(ExistingBossActorTag))
	{
		return;
	}

	APawn* SpawnedPawn = Cast<APawn>(SpawnedActor);
	if (!SpawnedPawn)
	{
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[BossSummonCinematic_LiveCodeCheck] Tagged boss spawned: %s — scheduling cinematic in %.2fs"),
		*SpawnedPawn->GetName(), PostSpawnDelay);

	// 한 번만 발동 — 핸들러 즉시 해제
	UnregisterActorSpawnHandler();

	// 보스 BeginPlay/AI 초기화 마무리될 시간 확보 후 발동
	const float Grace = FMath::Max(PostSpawnDelay, 0.05f);
	TWeakObjectPtr<APawn> WeakBoss = SpawnedPawn;
	GetWorldTimerManager().SetTimer(AutoActivateTimer,
		FTimerDelegate::CreateLambda([this, WeakBoss]()
		{
			if (!HasAuthority()) return;
			APawn* B = WeakBoss.Get();
			if (!B) return;

			UE_LOG(LogTemp, Warning,
				TEXT("[BossSummonCinematic_LiveCodeCheck] Spawn-triggered AutoActivate firing for %s"),
				*B->GetName());
			TryActivate(B);
		}),
		Grace, false);
}

void ABossSummonCinematicTrigger::UnregisterActorSpawnHandler()
{
	if (UWorld* World = GetWorld())
	{
		if (ActorSpawnedHandle.IsValid())
		{
			World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
		}
	}
	ActorSpawnedHandle.Reset();
}

void ABossSummonCinematicTrigger::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoActivateTimer);
		World->GetTimerManager().ClearTimer(MinHoldTimer);
		World->GetTimerManager().ClearTimer(FailsafeTimer);
	}

	UnregisterActorSpawnHandler();

	if (LocalCameraActor.IsValid())
	{
		LocalCameraActor->Destroy();
		LocalCameraActor.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void ABossSummonCinematicTrigger::HandleAutoActivate()
{
	if (!HasAuthority())
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[BossSummonCinematic_LiveCodeCheck] AutoActivate fired"));

	const bool bOk = TryActivate(nullptr);
	if (!bOk)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BossSummonCinematic_LiveCodeCheck] AutoActivate TryActivate returned false (이미 진행 중이거나 보스 못 찾음)"));
	}
}

APawn* ABossSummonCinematicTrigger::ResolveTargetBoss() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// 1) 태그 검색
	if (!ExistingBossActorTag.IsNone())
	{
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			APawn* P = *It;
			if (IsValid(P) && P->ActorHasTag(ExistingBossActorTag))
			{
				return P;
			}
		}
	}

	// 2) 인스턴스 참조 폴백
	if (IsValid(TargetBossActor))
	{
		return TargetBossActor;
	}

	return nullptr;
}

bool ABossSummonCinematicTrigger::TryActivate(APawn* InTargetBoss)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BossSummonCinematic_LiveCodeCheck] TryActivate 호출되었지만 권한 없음 (서버에서만 호출 필요)"));
		return false;
	}

	if (bCinematicActive)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossSummonCinematic_LiveCodeCheck] TryActivate skip — 이미 진행 중"));
		return false;
	}

	APawn* Boss = IsValid(InTargetBoss) ? InTargetBoss : ResolveTargetBoss();
	if (!IsValid(Boss))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BossSummonCinematic_LiveCodeCheck] TryActivate 실패 — 보스 못 찾음 (Tag=%s, TargetBossActor=%s)"),
			*ExistingBossActorTag.ToString(),
			IsValid(TargetBossActor) ? *TargetBossActor->GetName() : TEXT("null"));
		return false;
	}

	bCinematicActive = true;
	ActiveBoss = Boss;
	bMontageFinishedFlag = false;
	bMinHoldElapsedFlag = false;

	UE_LOG(LogTemp, Warning,
		TEXT("[BossSummonCinematic_LiveCodeCheck] TryActivate START — Boss=%s, Loc=%s"),
		*Boss->GetName(), *Boss->GetActorLocation().ToString());

	// 1) 보스 무적 + 피격 차단
	Boss->SetCanBeDamaged(false);

	// 2) 보스 AI 정지 (HellunaEnemyCharacter일 경우 자동 재시작도 억제)
	if (AHellunaEnemyCharacter* BossEnemy = Cast<AHellunaEnemyCharacter>(Boss))
	{
		BossEnemy->bSuppressAutoBrainRestart = true;
	}

	if (AAIController* AIC = Cast<AAIController>(Boss->GetController()))
	{
		if (UBrainComponent* Brain = AIC->GetBrainComponent())
		{
			Brain->StopLogic(TEXT("BossSummonCinematicTrigger"));
			bAIStopped = true;
		}
	}

	// 3) 이동 잠금 — 루트모션 없는 몽타주 대비.
	//    MOVE_None은 중력까지 끊어 공중 스폰 시 보스가 떠있게 됨.
	//    MOVE_Walking + 속도 0으로 두면 중력은 살아있어 자연스럽게 땅에 안착.
	//    AI 자체는 StopLogic으로 이미 멈춰 있으므로 보스가 자발적으로 움직이지 않음.
	if (ACharacter* BossChar = Cast<ACharacter>(Boss))
	{
		if (UCharacterMovementComponent* Move = BossChar->GetCharacterMovement())
		{
			Move->StopMovementImmediately();
			Move->SetMovementMode(MOVE_Walking);
		}
	}

	// 4) 카메라 전환 + 입력 잠금 (모든 클라) — 몽타주 재생 전에 먼저 시작해야 카메라가 우선 자리 잡음
	Multicast_StartCinematic(Boss);

	// 5) 최소 유지 타이머 — 몽타주가 즉시 끝나도 카메라가 보스를 비추는 시간 확보
	const float MinHold = FMath::Max(MinCinematicHoldDuration, 0.5f);
	GetWorldTimerManager().SetTimer(MinHoldTimer, this,
		&ABossSummonCinematicTrigger::OnMinHoldElapsedServer, MinHold, false);

	// 6) Failsafe 타이머 — 몽타주가 너무 길거나 콜백 누락 시 절대 상한
	const float Failsafe = FMath::Max(MaxDuration, MinHold + 0.5f);
	GetWorldTimerManager().SetTimer(FailsafeTimer, this,
		&ABossSummonCinematicTrigger::HandleCinematicCompletedServer, Failsafe, false);

	// 7) 소환 몽타주 재생 + 종료 콜백 바인딩 (마지막에 — 즉시 콜백이 와도 플래그 처리)
	if (AHellunaEnemyCharacter* BossEnemy = Cast<AHellunaEnemyCharacter>(Boss))
	{
		BossEnemy->OnSummonMontageFinished.Unbind();
		BossEnemy->OnSummonMontageFinished.BindUObject(this,
			&ABossSummonCinematicTrigger::OnSummonMontageFinishedServer);
		BossEnemy->Multicast_PlaySummonMontage();
	}
	else
	{
		// HellunaEnemyCharacter 아님 → 몽타주 자체가 없으니 바로 플래그만 세팅
		bMontageFinishedFlag = true;
	}

	return true;
}

void ABossSummonCinematicTrigger::OnSummonMontageFinishedServer()
{
	if (!HasAuthority() || !bCinematicActive)
	{
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[BossSummonCinematic_LiveCodeCheck] Montage finished (or absent) — flag set"));

	bMontageFinishedFlag = true;
	TryFinishCinematic();
}

void ABossSummonCinematicTrigger::OnMinHoldElapsedServer()
{
	if (!HasAuthority() || !bCinematicActive)
	{
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[BossSummonCinematic_LiveCodeCheck] Min hold elapsed — flag set"));

	bMinHoldElapsedFlag = true;
	TryFinishCinematic();
}

void ABossSummonCinematicTrigger::TryFinishCinematic()
{
	// MinHold(=시퀀스 길이)만 경과하면 즉시 종료. 몽타주는 계속 재생되도록 두고 카메라/입력만 해제.
	// (이전: Montage && MinHold 동시 필요 → 긴 몽타주가 Failsafe까지 컨트롤 반환을 막던 버그)
	if (bMinHoldElapsedFlag)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BossSummonCinematic_LiveCodeCheck] TryFinish — MinHold reached (MontageFin=%d) → exit"),
			bMontageFinishedFlag ? 1 : 0);
		HandleCinematicCompletedServer();
	}
}

void ABossSummonCinematicTrigger::HandleCinematicCompletedServer()
{
	if (!HasAuthority())
	{
		return;
	}

	if (!bCinematicActive)
	{
		// 몽타주 콜백 + Failsafe 둘 다 발화될 수 있음 — 첫 번째만 처리
		return;
	}

	bCinematicActive = false;
	GetWorldTimerManager().ClearTimer(FailsafeTimer);
	GetWorldTimerManager().ClearTimer(MinHoldTimer);

	UE_LOG(LogTemp, Warning,
		TEXT("[BossSummonCinematic_LiveCodeCheck] Server cinematic completed — restoring boss"));

	if (APawn* Boss = ActiveBoss.Get())
	{
		// 1) 델리게이트 해제 + 자동 재시작 억제 해제 + 소환 몽타주 즉시 종료
		if (AHellunaEnemyCharacter* BossEnemy = Cast<AHellunaEnemyCharacter>(Boss))
		{
			BossEnemy->OnSummonMontageFinished.Unbind();
			BossEnemy->bSuppressAutoBrainRestart = false;

			// 시네마틱 종료 순간에 소환 몽타주도 블렌드 아웃으로 정리
			if (USkeletalMeshComponent* BossMesh = BossEnemy->GetMesh())
			{
				if (UAnimInstance* AnimInst = BossMesh->GetAnimInstance())
				{
					if (BossEnemy->SummonMontage && AnimInst->Montage_IsPlaying(BossEnemy->SummonMontage))
					{
						constexpr float BlendOutTime = 0.2f;
						AnimInst->Montage_Stop(BlendOutTime, BossEnemy->SummonMontage);
						UE_LOG(LogTemp, Warning,
							TEXT("[BossSummonCinematic_LiveCodeCheck] SummonMontage stopped (blend=%.2fs) on cinematic end"),
							BlendOutTime);
					}
				}
			}
		}

		// 2) 무적 해제
		Boss->SetCanBeDamaged(true);

		// 3) 이동 복원
		if (ACharacter* BossChar = Cast<ACharacter>(Boss))
		{
			if (UCharacterMovementComponent* Move = BossChar->GetCharacterMovement())
			{
				Move->SetMovementMode(MOVE_Walking);
			}
		}

		// 4) AI 재개 (우리가 멈춘 경우만)
		if (bAIStopped)
		{
			if (AAIController* AIC = Cast<AAIController>(Boss->GetController()))
			{
				if (UBrainComponent* Brain = AIC->GetBrainComponent())
				{
					Brain->StartLogic();
				}
			}
			bAIStopped = false;
		}
	}

	ActiveBoss.Reset();

	// 5) 카메라 복귀 + 입력 복원 (모든 클라)
	Multicast_EndCinematic();
}

void ABossSummonCinematicTrigger::Multicast_StartCinematic_Implementation(APawn* Boss)
{
	UE_LOG(LogTemp, Warning,
		TEXT("[BossSummonCinematic_LiveCodeCheck] Multicast_Start — Boss=%s"),
		Boss ? *Boss->GetName() : TEXT("NULL"));

	if (IsRunningDedicatedServer())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !IsValid(Boss))
	{
		return;
	}

	// ── 모드 1: LevelSequence 사용 (Possessable 카메라를 보스에 부착) ──
	if (CameraSequence)
	{
		// 1) CineCameraActor를 직접 스폰 — 종횡비 강제 해제 (검정 바 방지)
		FActorSpawnParameters CamSpawnParam;
		CamSpawnParam.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACineCameraActor* CineCam = World->SpawnActor<ACineCameraActor>(
			ACineCameraActor::StaticClass(), Boss->GetActorLocation(), FRotator::ZeroRotator, CamSpawnParam);

		if (CineCam)
		{
			if (UCineCameraComponent* CineComp = CineCam->GetCineCameraComponent())
			{
				CineComp->SetConstraintAspectRatio(false);
			}

			// 2) 보스에 부착 — KeepRelative: 시퀀스 transform 키프레임이 로컬 좌표로 해석됨
			FAttachmentTransformRules AttachRules(
				EAttachmentRule::KeepRelative,
				EAttachmentRule::KeepRelative,
				EAttachmentRule::KeepRelative,
				false);
			CineCam->AttachToActor(Boss, AttachRules);

			LocalCameraActor = CineCam;
		}

		// 3) LevelSequencePlayer 생성 + Possessable 바인딩을 우리 카메라로 오버라이드
		FMovieSceneSequencePlaybackSettings Settings;
		Settings.bAutoPlay = false;
		Settings.LoopCount.Value = 0;
		Settings.bPauseAtEnd = true;

		ALevelSequenceActor* OutActor = nullptr;
		ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(
			World, CameraSequence, Settings, OutActor);

		if (Player && CineCam)
		{
			LocalSequencePlayer = Player;
			LocalSequenceActor = OutActor;

			// Possessable 바인딩 → 우리가 스폰한 CineCam으로 교체
			if (UMovieScene* MS = CameraSequence->GetMovieScene())
			{
				const int32 PossessableCount = MS->GetPossessableCount();
				if (PossessableCount > 0 && OutActor && OutActor->BindingOverrides)
				{
					const FMovieScenePossessable& Poss = MS->GetPossessable(0);
					FMovieSceneObjectBindingID BindingID;
					BindingID.SetGuid(Poss.GetGuid());

					TArray<UObject*> NewBoundObjects;
					NewBoundObjects.Add(CineCam);
					OutActor->BindingOverrides->SetBinding(BindingID, NewBoundObjects);

					UE_LOG(LogTemp, Warning,
						TEXT("[BossSummonCinematic_LiveCodeCheck] Possessable binding overridden — CineCam attached to %s"),
						*Boss->GetName());
				}
			}

			Player->Play();

			// ViewTarget 직접 설정 (Camera Cuts가 담당하지만 명시적 전환으로 블렌드 활용)
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				APlayerController* PC = It->Get();
				if (!PC || !PC->IsLocalPlayerController()) continue;
				if (AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(PC))
				{
					HeroPC->EnterBossCinematic(CineCam, CameraBlendIn);

					// [BossDialogueV1] 로컬 플레이어에게 대사 자막 생성 + 재생
					if (DialogueWidgetClass && !BossDialogueLine.IsEmpty())
					{
						LocalDialogueWidget = CreateWidget<UBossDialogueWidget>(HeroPC, DialogueWidgetClass);
						if (LocalDialogueWidget)
						{
							LocalDialogueWidget->AddToViewport(50); // HUD보다 위
							LocalDialogueWidget->PlayDialogue(BossSpeakerName, BossDialogueLine);
							UE_LOG(LogTemp, Warning,
								TEXT("[BossDialogueV1] Widget spawned + PlayDialogue — Class=%s"),
								*DialogueWidgetClass->GetName());
						}
					}
					break;
				}
			}

			UE_LOG(LogTemp, Warning,
				TEXT("[BossSummonCinematic_LiveCodeCheck] Sequence playback started: %s"),
				*CameraSequence->GetName());
			return;
		}

		UE_LOG(LogTemp, Warning,
			TEXT("[BossSummonCinematic_LiveCodeCheck] Sequence player or camera creation failed — falling back"));
	}

	// ── 모드 2 (폴백): 보스 로컬 좌표 기준으로 카메라 초기 배치 (정면이 항상 잡히도록) ──
	const FVector BossLoc = Boss->GetActorLocation();
	const FVector Forward = Boss->GetActorForwardVector();
	const FVector Right   = Boss->GetActorRightVector();
	const FVector Up      = Boss->GetActorUpVector();

	const FVector CameraLoc =
		BossLoc
		+ Forward * CameraOffset.X
		+ Right   * CameraOffset.Y
		+ Up      * CameraOffset.Z;

	// 카메라 시선 타겟: 보스 중심부 (전신 프레이밍용 — 가슴(Z+80)보다 약간 낮은 골반/허리)
	const FVector LookTarget = BossLoc + FVector(0.f, 0.f, 50.f);
	const FRotator LookAt = (LookTarget - CameraLoc).Rotation();

	FActorSpawnParameters SpawnParam;
	SpawnParam.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACameraActor* CamActor = World->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), CameraLoc, LookAt, SpawnParam);

	AActor* ViewTarget = Boss;
	if (CamActor)
	{
		LocalCameraActor = CamActor;

		// 보스에 부착 → 보스가 움직이거나 회전하면 카메라도 함께 이동/회전
		// KeepWorld: 현재 월드 위치/회전 유지하면서 부착 (계산해 둔 시점/위치가 보존됨)
		FAttachmentTransformRules AttachRules(
			EAttachmentRule::KeepWorld,   // Location
			EAttachmentRule::KeepWorld,   // Rotation
			EAttachmentRule::KeepWorld,   // Scale
			false);                       // bWeldSimulatedBodies
		CamActor->AttachToActor(Boss, AttachRules);

		ViewTarget = CamActor;
	}

	// 로컬 PlayerController에 ViewTarget 블렌드 + 입력 잠금
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalPlayerController())
		{
			continue;
		}
		if (AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(PC))
		{
			HeroPC->EnterBossCinematic(ViewTarget, CameraBlendIn);

			// [BossDialogueV1] 폴백 모드에서도 동일하게 자막 재생
			if (DialogueWidgetClass && !BossDialogueLine.IsEmpty())
			{
				LocalDialogueWidget = CreateWidget<UBossDialogueWidget>(HeroPC, DialogueWidgetClass);
				if (LocalDialogueWidget)
				{
					LocalDialogueWidget->AddToViewport(50);
					LocalDialogueWidget->PlayDialogue(BossSpeakerName, BossDialogueLine);
					UE_LOG(LogTemp, Warning,
						TEXT("[BossDialogueV1] (fallback) Widget spawned + PlayDialogue"));
				}
			}
			break;
		}
	}
}

void ABossSummonCinematicTrigger::Multicast_EndCinematic_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[BossSummonCinematic_LiveCodeCheck] Multicast_End"));

	if (IsRunningDedicatedServer())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 로컬 PlayerController 카메라 복귀 + 입력 복원
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalPlayerController())
		{
			continue;
		}
		if (AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(PC))
		{
			HeroPC->ExitBossCinematic(CameraBlendOut);
			break;
		}
	}

	// [BossDialogueV1] 대사 위젯 페이드 아웃 → 페이드 완료 후 자체 RemoveFromParent
	if (LocalDialogueWidget)
	{
		LocalDialogueWidget->HideDialogue();
		LocalDialogueWidget = nullptr; // 위젯은 자체 Tick에서 페이드 후 Remove 처리
	}

	// 시퀀스 플레이어 정리 (있다면)
	if (LocalSequencePlayer)
	{
		LocalSequencePlayer->Stop();
		LocalSequencePlayer = nullptr;
	}
	if (LocalSequenceActor)
	{
		LocalSequenceActor->Destroy();
		LocalSequenceActor = nullptr;
	}

	// 블렌드 아웃이 끝난 뒤 카메라 액터 파괴 (블렌드 중 사라지면 화면 튐)
	if (LocalCameraActor.IsValid())
	{
		ACameraActor* CamPtr = LocalCameraActor.Get();
		const float DestroyDelay = FMath::Max(CameraBlendOut + 0.1f, 0.2f);

		FTimerHandle DestroyTimer;
		World->GetTimerManager().SetTimer(DestroyTimer,
			[CamPtr]()
			{
				if (IsValid(CamPtr))
				{
					CamPtr->Destroy();
				}
			},
			DestroyDelay, false);

		LocalCameraActor.Reset();
	}
}
