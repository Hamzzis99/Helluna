// Source/Helluna/Private/Minwoo/BossSummonCinematicTrigger.cpp
// LiveCoding nudge: 2026-04-24

#include "Minwoo/BossSummonCinematicTrigger.h"

#include "AIController.h"
#include "BrainComponent.h"
#include "Camera/CameraActor.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaEnemyCharacter_Boss.h"
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
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "UObject/UnrealType.h"

ABossSummonCinematicTrigger::ABossSummonCinematicTrigger()
{
	// [CinematicWalkV1] Tick 가 필요. 평소엔 비활성, TryActivate 에서 켜고 종료 시 끔.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	SetReplicateMovement(false);
}

// [CinematicWalkV1] 시네마틱 동안 보스를 매 Tick 전진시킨다.
//   AM_Boss_Walk 가 in-place 라 root motion 으로 못 움직이는 문제 우회 — 직접 입력으로 걷기 시각화.
//   서버에서만 동작 (HasAuthority). 클라는 FRepMovement 로 위치 sync.
void ABossSummonCinematicTrigger::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!HasAuthority() || !bCinematicActive) return;
	if (CinematicBossWalkSpeed <= 0.f) return;

	APawn* Boss = ActiveBoss.Get();
	if (!Boss) return;

	FVector Direction;
	if (bCinematicWalkUseBossForward)
	{
		Direction = Boss->GetActorForwardVector();
	}
	else
	{
		// 가장 가까운 PlayerPawn 방향 (없으면 정면 fallback).
		AActor* TargetActor = nullptr;
		float ClosestDistSq = TNumericLimits<float>::Max();
		const FVector BossLoc = Boss->GetActorLocation();
		if (UWorld* World = GetWorld())
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				if (APlayerController* PC = It->Get())
				{
					if (APawn* PlayerPawn = PC->GetPawn())
					{
						const float DistSq = FVector::DistSquared(BossLoc, PlayerPawn->GetActorLocation());
						if (DistSq < ClosestDistSq)
						{
							ClosestDistSq = DistSq;
							TargetActor = PlayerPawn;
						}
					}
				}
			}
		}
		Direction = TargetActor
			? (TargetActor->GetActorLocation() - BossLoc).GetSafeNormal2D()
			: Boss->GetActorForwardVector();

		// [CinematicWalkV1.NoShake] 보스 회전은 CMC 의 bOrientRotationToMovement 가 자동으로 처리하게 둠.
		//   여기서 SetActorRotation 으로 매 Tick 덮어쓰면 CMC 의 회전 보간과 경쟁해 yaw 진동 →
		//   보스에 부착된 시네마틱 카메라가 같이 흔들림. 회전 직접 제어 제거.
	}

	if (Direction.IsNearlyZero()) return;
	Boss->AddMovementInput(Direction, 1.0f);
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
	if (AHellunaEnemyCharacter_Boss* BossEnemy = Cast<AHellunaEnemyCharacter_Boss>(Boss))
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
	if (ACharacter* BossChar = Cast<ACharacter>(Boss))
	{
		if (UCharacterMovementComponent* Move = BossChar->GetCharacterMovement())
		{
			Move->StopMovementImmediately();
			Move->SetMovementMode(MOVE_Walking);

			// [CinematicWalkV1] 시네마틱 중 Tick AddMovementInput 으로 전진할 수 있도록
			//   MaxWalkSpeed 를 CinematicBossWalkSpeed 로 잠시 덮어쓰고, 종료 시 복원.
			if (CinematicBossWalkSpeed > 0.f)
			{
				SavedBossMaxWalkSpeed = Move->MaxWalkSpeed;
				Move->MaxWalkSpeed = CinematicBossWalkSpeed;
				SetActorTickEnabled(true);
			}
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
	if (AHellunaEnemyCharacter_Boss* BossEnemy = Cast<AHellunaEnemyCharacter_Boss>(Boss))
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
		if (AHellunaEnemyCharacter_Boss* BossEnemy = Cast<AHellunaEnemyCharacter_Boss>(Boss))
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

					// [RootMotionRestoreV1] 서버 사이드 안전장치 — OnSummonMontageEnded 콜백이 누락되거나
					// 0.2s blend out 동안 보스가 멈춰있는 시간을 없애기 위해 시네마틱 종료 즉시 복원.
					// 클라 사이드는 OnSummonMontageEnded (Multicast 컨텍스트에서 서버+클라 양쪽 바인딩) 가 처리.
					AnimInst->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
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

				// [CinematicWalkV1] TryActivate 에서 덮어쓴 MaxWalkSpeed 복원 + Tick 종료.
				if (SavedBossMaxWalkSpeed >= 0.f)
				{
					Move->MaxWalkSpeed = SavedBossMaxWalkSpeed;
					SavedBossMaxWalkSpeed = -1.f;
				}
				SetActorTickEnabled(false);
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

	// BP OnCinematicEndedClient에서 쓸 수 있게 보스 참조 캐시
	LocalCinematicBoss = Boss;

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

	// ── 모드 2 (폴백): 보스 로컬 좌표 기준으로 카메라 초기 배치 ──
	const FVector BossLoc = Boss->GetActorLocation();
	const FVector Forward = Boss->GetActorForwardVector();
	const FVector Right   = Boss->GetActorRightVector();
	const FVector Up      = Boss->GetActorUpVector();

	const FVector CameraLoc =
		BossLoc
		+ Forward * CameraOffset.X
		+ Right   * CameraOffset.Y
		+ Up      * CameraOffset.Z;

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

		// 보스에 부착 — KeepWorld
		FAttachmentTransformRules AttachRules(
			EAttachmentRule::KeepWorld,
			EAttachmentRule::KeepWorld,
			EAttachmentRule::KeepWorld,
			false);
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

void ABossSummonCinematicTrigger::OnBossDiedClient(AActor* DeadActor, AActor* KillerActor)
{
	UE_LOG(LogTemp, Warning,
		TEXT("[BossHealthBarV1] Boss died — removing HP bar"));

	if (LocalBossHealthBar)
	{
		LocalBossHealthBar->RemoveFromParent();
		LocalBossHealthBar = nullptr;
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

	// 보스 HP 바 스폰 — 로컬 머신에서만 발화
	if (APawn* BossPtr = LocalCinematicBoss.Get())
	{
		if (BossHealthBarWidgetClass)
		{
			APlayerController* LocalPC = nullptr;
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				if (APlayerController* PC = It->Get())
				{
					if (PC->IsLocalPlayerController())
					{
						LocalPC = PC;
						break;
					}
				}
			}

			if (LocalPC)
			{
				LocalBossHealthBar = CreateWidget<UUserWidget>(LocalPC, BossHealthBarWidgetClass);
				if (LocalBossHealthBar)
				{
					// 위젯의 BossActor 변수를 리플렉션으로 주입 (BP의 Actor Reference Exposed on Spawn 변수)
					if (FObjectProperty* BossProp = FindFProperty<FObjectProperty>(
						LocalBossHealthBar->GetClass(), TEXT("BossActor")))
					{
						BossProp->SetObjectPropertyValue_InContainer(LocalBossHealthBar, BossPtr);
					}

					LocalBossHealthBar->AddToViewport(BossHealthBarZOrder);

					// 보스 사망 시 HP 바 자동 제거
					if (UHellunaHealthComponent* HC = BossPtr->FindComponentByClass<UHellunaHealthComponent>())
					{
						HC->OnDeath.AddUniqueDynamic(this, &ABossSummonCinematicTrigger::OnBossDiedClient);
					}

					UE_LOG(LogTemp, Warning,
						TEXT("[BossHealthBarV1] Spawned HP bar for %s (class=%s)"),
						*BossPtr->GetName(), *BossHealthBarWidgetClass->GetName());
				}
			}
		}

		// BP 추가 연출 훅
		OnCinematicEndedClient(BossPtr);
	}
	LocalCinematicBoss.Reset();

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
