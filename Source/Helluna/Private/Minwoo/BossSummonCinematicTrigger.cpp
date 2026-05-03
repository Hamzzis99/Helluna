// Source/Helluna/Private/Minwoo/BossSummonCinematicTrigger.cpp
// LiveCoding nudge: 2026-04-24

#include "Minwoo/BossSummonCinematicTrigger.h"

#include "Camera/CameraShakeBase.h"
#include "Kismet/GameplayStatics.h"
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
#include "DrawDebugHelpers.h"
#include "Camera/PlayerCameraManager.h"

ABossSummonCinematicTrigger::ABossSummonCinematicTrigger()
{
	// [CinematicWalkV1] Tick 가 필요. 평소엔 비활성, TryActivate 에서 켜고 종료 시 끔.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	SetReplicateMovement(false);

	// [PortalCutsV1] 디폴트 컷 시퀀스 — Manny 기본 스켈레톤 기준 발 → 무기 손 → 얼굴.
	//   사용자가 BP 인스턴스에서 자유롭게 튜닝/추가/삭제. 비우면 단일 카메라 폴백.
	{
		FBossCinematicCut FootCut;
		FootCut.Socket       = TEXT("foot_r");
		FootCut.Duration     = 3.f;
		FootCut.CameraOffset = FVector(80.f, -60.f, 30.f);
		FootCut.LookAtOffset = FVector::ZeroVector;
		CameraCuts.Add(FootCut);

		FBossCinematicCut WeaponCut;
		WeaponCut.Socket       = TEXT("hand_r");
		WeaponCut.Duration     = 3.f;
		WeaponCut.CameraOffset = FVector(60.f, 60.f, 0.f);
		WeaponCut.LookAtOffset = FVector::ZeroVector;
		CameraCuts.Add(WeaponCut);

		FBossCinematicCut FaceCut;
		FaceCut.Socket       = TEXT("head");
		FaceCut.Duration     = 3.f;
		FaceCut.CameraOffset = FVector(120.f, 0.f, 0.f);
		FaceCut.LookAtOffset = FVector::ZeroVector;
		CameraCuts.Add(FaceCut);
	}
}

// [CinematicWalkInputV2] 시네마틱 동안 보스를 AddMovementInput 으로 슬로우 전진.
//   설계 근거: AM_Boss_Walk root motion 직접 사용은 (a) 시네마틱 카메라 KeepRelative attach 로
//   화면 정지 → 종료 시 텔레포트 (b) 클라 sim proxy 가 root motion 안 따라가는 동기화 문제로
//   불안정. 그래서 root motion 은 IgnoreRootMotion 모드로 끄고 (Boss::Multicast_PlaySummonMontage),
//   대신 일반 CMC input 흐름으로 정해진 속도(CinematicBossWalkSpeed) 만큼만 전진. 애니메이션은
//   0.5x 슬로우라 다리 cadence 가 평소보다 느림 → 전진 속도도 평소 walk 보다 낮게 (BP 에서 튜닝).
void ABossSummonCinematicTrigger::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// [PortalCutsV1] 클라 사이드: 카메라 컷 진행. CameraSequence 사용 시 시퀀스가 카메라를
	//   몰기 때문에 컷 코드는 비활성. CameraCuts 비어있으면 단일 카메라 폴백 모드라 갱신 불필요.
	if (LocalCameraActor.IsValid() && !CameraSequence && CameraCuts.Num() > 0)
	{
		CutsElapsedTime += DeltaTime;

		// 누적 시간이 어느 컷 버킷에 들어가는지 결정.
		float Acc = 0.f;
		int32 NewIndex = CameraCuts.Num() - 1; // 마지막 컷에 머무름 (Min/Max Duration 클램핑 후 잔여)
		for (int32 i = 0; i < CameraCuts.Num(); ++i)
		{
			Acc += FMath::Max(CameraCuts[i].Duration, 0.1f);
			if (CutsElapsedTime < Acc)
			{
				NewIndex = i;
				break;
			}
		}

		if (NewIndex != CurrentCutIndex)
		{
			CurrentCutIndex = NewIndex;
			UE_LOG(LogTemp, Warning,
				TEXT("[BossSummonCinematic_LiveCodeCheck] Cut switch → %d (Socket=%s)"),
				NewIndex,
				CameraCuts.IsValidIndex(NewIndex) ? *CameraCuts[NewIndex].Socket.ToString() : TEXT("?"));
		}

		UpdateCameraForCurrentCut();

		// [ClientCinematicV2] ViewTarget 강제 재적용 — 외부에서 재설정해도 cinematic 카메라 유지.
		//   추가로 PCM 의 현재 ViewTarget 과 LocalCameraActor 가 다르면 디버그 로그.
		if (UWorld* W = GetWorld())
		{
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0))
			{
				if (APlayerCameraManager* PCM = PC->PlayerCameraManager)
				{
					AActor* CurrentVT = PCM->GetViewTarget();
					if (CurrentVT != LocalCameraActor.Get())
					{
						// 1초마다 한 번만 로그 (스팸 방지)
						static double LastLogTime = 0.0;
						const double Now = FPlatformTime::Seconds();
						if (Now - LastLogTime > 1.0)
						{
							LastLogTime = Now;
							UE_LOG(LogTemp, Warning,
								TEXT("[BossSummonCinematic_LiveCodeCheck] ViewTarget MISMATCH — VT=%s, want=%s — re-applying"),
								*GetNameSafe(CurrentVT),
								*GetNameSafe(LocalCameraActor.Get()));
						}
						PC->SetViewTarget(LocalCameraActor.Get());
					}
				}
			}
		}

	}

	if (!HasAuthority() || !bCinematicActive) return;
	// [PortalRevealV1] 포탈만 보여주는 phase 동안 보스 walk 입력 억제.
	if (!bCinematicWalkActive) return;
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
		World->GetTimerManager().ClearTimer(PortalRevealTimerServer);
		World->GetTimerManager().ClearTimer(PortalRevealTimerLocal);
		World->GetTimerManager().ClearTimer(BossEmergeTimerServer);
		World->GetTimerManager().ClearTimer(BossEmergeTimerLocal);
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

	// [CinematicFaceTargetV1] 시네마틱 시작 즉시 보스를 가장 가까운 플레이어 방향으로 회전.
	//   목적: 카메라 오프셋이 보스 로컬 정면(+X) 기준이라 보스가 어떤 방향이든 카메라가 항상
	//   보스의 정면(=플레이어를 등진 자세)을 비추도록 보장. AI 가 곧 정지되니 회전이 풀리지 않음.
	{
		AActor* ClosestPlayer = nullptr;
		float ClosestDistSq = TNumericLimits<float>::Max();
		const FVector BossLoc = Boss->GetActorLocation();
		if (UWorld* W = GetWorld())
		{
			for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
			{
				if (APlayerController* PC = It->Get())
				{
					if (APawn* PlayerPawn = PC->GetPawn())
					{
						const float D = FVector::DistSquared(BossLoc, PlayerPawn->GetActorLocation());
						if (D < ClosestDistSq)
						{
							ClosestDistSq = D;
							ClosestPlayer = PlayerPawn;
						}
					}
				}
			}
		}
		if (ClosestPlayer)
		{
			FVector ToPlayer = (ClosestPlayer->GetActorLocation() - BossLoc).GetSafeNormal2D();
			if (!ToPlayer.IsNearlyZero())
			{
				const FRotator FaceRot(0.f, ToPlayer.Rotation().Yaw, 0.f);
				Boss->SetActorRotation(FaceRot);
				if (ACharacter* BossCh = Cast<ACharacter>(Boss))
				{
					if (UCharacterMovementComponent* M = BossCh->GetCharacterMovement())
					{
						M->bOrientRotationToMovement = false;
					}
				}
				UE_LOG(LogTemp, Warning,
					TEXT("[BossSummonCinematic_LiveCodeCheck] FaceTarget yaw=%.1f → boss now faces %s"),
					FaceRot.Yaw, *ClosestPlayer->GetName());
			}
		}
	}

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

	// 3) 이동 잠금 — 보스가 portal phase 동안 그 자리에 멈춰 있도록 walk 입력 게이트.
	//    MaxWalkSpeed 는 reveal 후 OnPortalRevealElapsedServer 에서 override.
	if (ACharacter* BossChar = Cast<ACharacter>(Boss))
	{
		if (UCharacterMovementComponent* Move = BossChar->GetCharacterMovement())
		{
			Move->StopMovementImmediately();
			Move->SetMovementMode(MOVE_Walking);
		}
	}
	bCinematicWalkActive = false;

	// [CinematicNetSyncV1] 시네마틱 동안 보스 NetUpdateFrequency 일시 부스트 (클라 잔진동 완화).
	Boss->SetNetUpdateFrequency(60.f);
	Boss->SetMinNetUpdateFrequency(30.f);

	// 4) 카메라 전환 RPC — 클라이언트들은 portal 만 스폰하고 reveal delay 까지 카메라는 player 유지.
	Multicast_StartCinematic(Boss);

	// 5) MinHold + Failsafe 계산. 컷 합계 + reveal delay 까지 포함해야 컷이 다 보임.
	float MinHold = FMath::Max(MinCinematicHoldDuration, 0.5f);
	if (!CameraSequence && CameraCuts.Num() > 0)
	{
		float CutsTotal = 0.f;
		for (const FBossCinematicCut& Cut : CameraCuts)
		{
			CutsTotal += FMath::Max(Cut.Duration, 0.1f);
		}
		if (CutsTotal > MinHold)
		{
			MinHold = CutsTotal;
		}
	}
	const float RevealDelay = FMath::Max(PortalRevealDelay, 0.f);
	MinHold += RevealDelay;
	UE_LOG(LogTemp, Warning,
		TEXT("[BossSummonCinematic_LiveCodeCheck] MinHold=%.2f (reveal=%.2f + cuts/min)"),
		MinHold, RevealDelay);
	GetWorldTimerManager().SetTimer(MinHoldTimer, this,
		&ABossSummonCinematicTrigger::OnMinHoldElapsedServer, MinHold, false);

	const float Failsafe = FMath::Max(MaxDuration, MinHold + 0.5f);
	GetWorldTimerManager().SetTimer(FailsafeTimer, this,
		&ABossSummonCinematicTrigger::HandleCinematicCompletedServer, Failsafe, false);

	// 6) Reveal 타이머 — RevealDelay 후 보스 walk 시작 + 몽타주 재생.
	//    HellunaEnemyCharacter 아닌 경우엔 몽타주가 없으니 즉시 플래그만 세팅 (기존 동작 유지).
	if (Cast<AHellunaEnemyCharacter_Boss>(Boss))
	{
		if (RevealDelay > KINDA_SMALL_NUMBER)
		{
			GetWorldTimerManager().SetTimer(PortalRevealTimerServer, this,
				&ABossSummonCinematicTrigger::OnPortalRevealElapsedServer, RevealDelay, false);
		}
		else
		{
			OnPortalRevealElapsedServer();
		}
	}
	else
	{
		bMontageFinishedFlag = true;
	}

	return true;
}

void ABossSummonCinematicTrigger::OnPortalRevealElapsedServer()
{
	// [BossEmergeV2] PortalRevealDelay 경과 — 시네마틱 카메라 시작.
	//   *Walk + 몽타주 즉시 시작* (boss 는 hidden 상태로 walk → BossEmergeDelay 후 visible).
	//   이래야 보스가 포탈을 walk-through 하다가 visible 되어 "포탈에서 나오는" 느낌이 남.
	if (!HasAuthority() || !bCinematicActive) return;
	APawn* Boss = ActiveBoss.Get();
	if (!Boss) return;

	if (ACharacter* BossChar = Cast<ACharacter>(Boss))
	{
		if (UCharacterMovementComponent* Move = BossChar->GetCharacterMovement())
		{
			if (CinematicBossWalkSpeed > 0.f)
			{
				SavedBossMaxWalkSpeed = Move->MaxWalkSpeed;
				Move->MaxWalkSpeed = CinematicBossWalkSpeed;
				SetActorTickEnabled(true);
				bCinematicWalkActive = true;
				UE_LOG(LogTemp, Warning,
					TEXT("[BossSummonCinematic_LiveCodeCheck] Camera start — walk enabled (boss still hidden)"));
			}
		}
	}

	if (AHellunaEnemyCharacter_Boss* BossEnemy = Cast<AHellunaEnemyCharacter_Boss>(Boss))
	{
		BossEnemy->OnSummonMontageFinished.Unbind();
		BossEnemy->OnSummonMontageFinished.BindUObject(this,
			&ABossSummonCinematicTrigger::OnSummonMontageFinishedServer);
		BossEnemy->bShouldLoopSummonMontage = true;
		BossEnemy->Multicast_PlaySummonMontage();
	}

	// 서버 사이드 emerge 는 visibility 와 무관 (visibility 는 클라 로컬). 노티만 남김.
	const float Emerge = FMath::Max(BossEmergeDelay, 0.f);
	if (Emerge > KINDA_SMALL_NUMBER)
	{
		GetWorldTimerManager().SetTimer(BossEmergeTimerServer, this,
			&ABossSummonCinematicTrigger::OnBossEmergeElapsedServer, Emerge, false);
	}
}

void ABossSummonCinematicTrigger::OnBossEmergeElapsedServer()
{
	UE_LOG(LogTemp, Warning,
		TEXT("[BossSummonCinematic_LiveCodeCheck] Server emerge tick (visibility was client-side)"));
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
	bCinematicWalkActive = false;
	GetWorldTimerManager().ClearTimer(FailsafeTimer);
	GetWorldTimerManager().ClearTimer(MinHoldTimer);
	GetWorldTimerManager().ClearTimer(PortalRevealTimerServer);
	GetWorldTimerManager().ClearTimer(BossEmergeTimerServer);

	UE_LOG(LogTemp, Warning,
		TEXT("[BossSummonCinematic_LiveCodeCheck] Server cinematic completed — restoring boss"));

	if (APawn* Boss = ActiveBoss.Get())
	{
		// 1) 델리게이트 해제 + 자동 재시작 억제 해제 + 소환 몽타주 즉시 종료
		if (AHellunaEnemyCharacter_Boss* BossEnemy = Cast<AHellunaEnemyCharacter_Boss>(Boss))
		{
			BossEnemy->OnSummonMontageFinished.Unbind();
			// [SummonMontageLoopV1] 루프 해제 (이걸 풀어야 Montage_Stop 의 OnEnded 가 cleanup 진행).
			BossEnemy->bShouldLoopSummonMontage = false;

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

					// [RootMotionRestoreV1] 서버 사이드 안전장치.
					AnimInst->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
				}
			}
		}

		// [CinematicNetSyncV1] NetUpdateFrequency 복원 (즉시).
		Boss->SetNetUpdateFrequency(10.f);
		Boss->SetMinNetUpdateFrequency(2.f);

		// [CinematicWalkV1] 이동 시스템 복원 (즉시 — 보스가 그 자리에서 idle).
		if (ACharacter* BossChar = Cast<ACharacter>(Boss))
		{
			if (UCharacterMovementComponent* Move = BossChar->GetCharacterMovement())
			{
				Move->SetMovementMode(MOVE_Walking);
				if (SavedBossMaxWalkSpeed >= 0.f)
				{
					Move->MaxWalkSpeed = SavedBossMaxWalkSpeed;
					SavedBossMaxWalkSpeed = -1.f;
				}
				SetActorTickEnabled(false);
			}
		}

		// [GracePeriodV1] 무적 해제 + AI 재개는 PostCinematicGracePeriod 후로 지연.
		//   카메라가 player 로 블렌드 되는 동안 보스가 즉시 공격 못 하게 함.
		const float Grace = FMath::Max(PostCinematicGracePeriod, 0.f);
		if (Grace > KINDA_SMALL_NUMBER)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[BossSummonCinematic_LiveCodeCheck] Grace period %.2fs scheduled before AI resume"),
				Grace);
			GetWorldTimerManager().SetTimer(GraceTimer, this,
				&ABossSummonCinematicTrigger::OnGracePeriodElapsed, Grace, false);
		}
		else
		{
			OnGracePeriodElapsed();
		}
	}

	// 5) 카메라 복귀 + 입력 복원 (모든 클라) — grace 와 무관하게 즉시 카메라 반환.
	Multicast_EndCinematic();
}

void ABossSummonCinematicTrigger::OnGracePeriodElapsed()
{
	APawn* Boss = ActiveBoss.Get();
	if (Boss)
	{
		Boss->SetCanBeDamaged(true);

		if (AHellunaEnemyCharacter_Boss* BossEnemy = Cast<AHellunaEnemyCharacter_Boss>(Boss))
		{
			BossEnemy->bSuppressAutoBrainRestart = false;
		}

		if (bAIStopped)
		{
			if (AAIController* AIC = Cast<AAIController>(Boss->GetController()))
			{
				if (UBrainComponent* Brain = AIC->GetBrainComponent())
				{
					Brain->StartLogic();
					UE_LOG(LogTemp, Warning,
						TEXT("[BossSummonCinematic_LiveCodeCheck] Grace elapsed — AI brain resumed, boss vulnerable"));
				}
			}
			bAIStopped = false;
		}
	}

	ActiveBoss.Reset();
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

	// [ClientBossResolveV1] PIE 멀티플레이 / 라이브 서버에서 클라 Boss=NULL 로 도착하는 케이스 핸들링.
	// 폴링 최대 30회 × 0.1s = 3s. 그동안 보스 액터가 클라 월드에 도착하면 즉시 시네마틱 시작.
	StartLocalCinematicWithRetry(Boss, 30);
}

void ABossSummonCinematicTrigger::StartLocalCinematicWithRetry(APawn* Boss, int32 RemainingRetries)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!IsValid(Boss))
	{
		Boss = ResolveTargetBoss();
	}

	if (!IsValid(Boss))
	{
		if (RemainingRetries <= 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[BossSummonCinematic_LiveCodeCheck] Client: Boss never resolved after retries — aborting local cinematic"));
			return;
		}

		TWeakObjectPtr<ABossSummonCinematicTrigger> Weak(this);
		const int32 NextRetries = RemainingRetries - 1;
		World->GetTimerManager().SetTimer(
			ClientCinematicRetryTimer,
			FTimerDelegate::CreateLambda([Weak, NextRetries]()
			{
				if (Weak.IsValid())
				{
					Weak->StartLocalCinematicWithRetry(nullptr, NextRetries);
				}
			}),
			0.1f, false);
		return;
	}

	StartLocalCinematicBody(Boss);
}

void ABossSummonCinematicTrigger::StartLocalCinematicBody(APawn* Boss)
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(Boss))
	{
		return;
	}

	// BP OnCinematicEndedClient에서 쓸 수 있게 보스 참조 캐시
	LocalCinematicBoss = Boss;

	// [PortalCutsV1] 보스 등장 포탈 — 클라 로컬 스폰. 데디 서버는 위에서 이미 return.
	if (PortalClass)
	{
		const FVector BossLoc  = Boss->GetActorLocation();
		const FVector Forward  = Boss->GetActorForwardVector();
		const FVector Right    = Boss->GetActorRightVector();
		const FVector Up       = Boss->GetActorUpVector();
		const FVector PortalLoc = BossLoc
			+ Forward * PortalSpawnOffset.X
			+ Right   * PortalSpawnOffset.Y
			+ Up      * PortalSpawnOffset.Z;
		const FRotator PortalRot = Boss->GetActorRotation() + PortalSpawnRotation;

		FActorSpawnParameters PortalParams;
		PortalParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* SpawnedPortal = World->SpawnActor<AActor>(PortalClass, PortalLoc, PortalRot, PortalParams);
		if (SpawnedPortal)
		{
			// [PortalRevealV1] BP_Portal 기본 크기 ×N 배율 — 거대 포탈 연출용.
			SpawnedPortal->SetActorScale3D(PortalSpawnScale);
			LocalPortalActor = SpawnedPortal;
			UE_LOG(LogTemp, Warning,
				TEXT("[BossSummonCinematic_LiveCodeCheck] Portal spawned: %s @ %s scale=%s"),
				*SpawnedPortal->GetName(), *PortalLoc.ToString(), *PortalSpawnScale.ToString());
		}
	}

	// [CinematicShakeV1] 시네마틱 동안 World Camera Shake 반복.
	// 데디 서버는 렌더 카메라가 없으므로 호출 자체는 무해 — TriggerLocalCinematicShake 내부에서 스킵.
	if (CinematicShakeClass)
	{
		TriggerLocalCinematicShake();
		if (CinematicShakeInterval > 0.f)
		{
			TWeakObjectPtr<ABossSummonCinematicTrigger> Weak(this);
			World->GetTimerManager().SetTimer(CinematicShakeTimer,
				FTimerDelegate::CreateLambda([Weak]()
				{
					if (Weak.IsValid())
					{
						Weak->TriggerLocalCinematicShake();
					}
				}),
				CinematicShakeInterval, /*bLoop=*/true);
		}
	}

	// [PortalClipV1] dissolve 대신 portal-plane clip — 보스 픽셀 중 평면 뒤쪽만 invisible.
	//   평면은 portal 위치 + portal forward 방향. 보스가 walk 로 평면 통과하면 통과한 부위만 visible.
	//   완료 (BossEmergeDelay 후) 에 ClipPlane 비활성화.
	if (AHellunaEnemyCharacter* EnemyBoss = Cast<AHellunaEnemyCharacter>(Boss))
	{
		AActor* PortalActor = LocalPortalActor.Get();
		if (PortalActor)
		{
			const FVector PortalLoc = PortalActor->GetActorLocation();
			// [PortalClipV2] 평면 노멀 = 보스 forward (= player 방향).
			//   visible side = +forward (player 쪽). 평면 반대(보스 뒤쪽 = 포탈의 안쪽) 에 있는 픽셀은 invisible.
			//   Portal forward 가 모델에 따라 어느 면을 향하는지 불확실해서 보스 forward 사용.
			const FVector VisibleNormal = Boss->GetActorForwardVector();
			EnemyBoss->StartPortalClipPlaneVisuals(PortalLoc, VisibleNormal);
		}
	}
	else if (ACharacter* BossChar = Cast<ACharacter>(Boss))
	{
		// HellunaEnemyCharacter 가 아니면 폴백 (단순 hide).
		if (USkeletalMeshComponent* BossMesh = BossChar->GetMesh())
		{
			BossMesh->SetVisibility(false, true);
		}
	}

	const float RevealDelay = FMath::Max(PortalRevealDelay, 0.f);
	if (RevealDelay > KINDA_SMALL_NUMBER)
	{
		TWeakObjectPtr<ABossSummonCinematicTrigger> Weak(this);
		World->GetTimerManager().SetTimer(PortalRevealTimerLocal,
			FTimerDelegate::CreateLambda([Weak]()
			{
				if (Weak.IsValid())
				{
					Weak->StartCinematicCameraAfterReveal();
				}
			}),
			RevealDelay, false);
		UE_LOG(LogTemp, Warning,
			TEXT("[BossSummonCinematic_LiveCodeCheck] Portal-only phase %.2fs scheduled, camera/boss reveal pending"),
			RevealDelay);
		return;
	}

	StartCinematicCameraAfterReveal();
}

void ABossSummonCinematicTrigger::StartCinematicCameraAfterReveal()
{
	UWorld* World = GetWorld();
	APawn* Boss = LocalCinematicBoss.Get();
	if (!World || !IsValid(Boss))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BossSummonCinematic_LiveCodeCheck] StartCinematicCameraAfterReveal — invalid world or boss, abort"));
		return;
	}

	// [BossEmergeV1] 보스 메시는 아직 숨김 유지 — BossEmergeDelay 후 OnBossEmergeElapsedLocal 에서 복원.
	//   카메라는 즉시 시네마틱으로 전환 → 카메라가 빈 포탈을 잠시 잡고 → 보스가 등장.
	const float Emerge = FMath::Max(BossEmergeDelay, 0.f);
	if (Emerge > KINDA_SMALL_NUMBER)
	{
		TWeakObjectPtr<ABossSummonCinematicTrigger> Weak(this);
		World->GetTimerManager().SetTimer(BossEmergeTimerLocal,
			FTimerDelegate::CreateLambda([Weak]()
			{
				if (Weak.IsValid())
				{
					Weak->OnBossEmergeElapsedLocal();
				}
			}),
			Emerge, false);
		UE_LOG(LogTemp, Warning,
			TEXT("[BossSummonCinematic_LiveCodeCheck] Camera switching — boss emerges in %.2fs"), Emerge);
	}
	else
	{
		// 즉시 등장 (이전 동작)
		OnBossEmergeElapsedLocal();
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

	// ── 모드 2 (폴백): 보스 로컬 좌표 기준으로 카메라 초기 배치 ──
	//   [PortalCutsV1] CameraCuts 가 있으면 컷 모드 — 카메라를 보스에 attach 하지 않고
	//   매 Tick UpdateCameraForCurrentCut() 가 world transform 을 직접 갱신.
	//   비어있으면 기존 단일 카메라 폴백 유지.
	const bool bUseCutsMode = (CameraCuts.Num() > 0);

	FActorSpawnParameters SpawnParam;
	SpawnParam.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACameraActor* CamActor = nullptr;
	AActor* ViewTarget = Boss;

	if (bUseCutsMode)
	{
		// 컷 모드 — 첫 컷 위치에 카메라 스폰 (Tick 이 즉시 갱신).
		CurrentCutIndex  = -1;
		CutsElapsedTime  = 0.f;

		CamActor = World->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), Boss->GetActorLocation(), FRotator::ZeroRotator, SpawnParam);
		if (CamActor)
		{
			LocalCameraActor = CamActor;
			ViewTarget = CamActor;
			// 첫 프레임이 화면에 나가기 전에 컷 0 으로 위치 잡아둠 (블렌드 출발점이 보스 발 부근이 됨).
			CurrentCutIndex = 0;
			UpdateCameraForCurrentCut();
			SetActorTickEnabled(true);
		}
	}
	else
	{
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

		CamActor = World->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), CameraLoc, LookAt, SpawnParam);
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
	}

	// [ClientCinematicV2] 로컬 PlayerController 에 ViewTarget 블렌드 + 입력 잠금.
	//   기존: 첫 매칭 PC 만 처리 (break) → 멀티플레이 클라 인스턴스에서 PC 못 찾으면 시네마틱 안 보임.
	//   수정: ALL local PCs 순회 + 매칭 0건이면 GetFirstPlayerController 폴백 + Cast 실패해도
	//         최소한 SetViewTargetWithBlend 로 카메라는 잡아줌.
	int32 LocalPCCount = 0;
	int32 HeroPCMatched = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalPlayerController())
		{
			continue;
		}
		++LocalPCCount;
		AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(PC);
		UE_LOG(LogTemp, Warning,
			TEXT("[BossSummonCinematic_LiveCodeCheck] LocalPC found: %s (HeroPC=%s)"),
			*PC->GetName(), HeroPC ? TEXT("yes") : TEXT("no"));

		if (HeroPC)
		{
			++HeroPCMatched;
			HeroPC->EnterBossCinematic(ViewTarget, CameraBlendIn);

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
		}
		else
		{
			// HellunaHeroController 가 아니어도 최소 카메라는 전환.
			PC->SetViewTargetWithBlend(ViewTarget, FMath::Max(CameraBlendIn, 0.1f),
				EViewTargetBlendFunction::VTBlend_EaseInOut, 2.f, false);
			UE_LOG(LogTemp, Warning,
				TEXT("[BossSummonCinematic_LiveCodeCheck] Non-Hero PC — direct SetViewTarget on %s"),
				*PC->GetName());
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[BossSummonCinematic_LiveCodeCheck] PC iteration done — LocalPCCount=%d HeroPCMatched=%d"),
		LocalPCCount, HeroPCMatched);

	// 폴백: 위에서 못 찾았으면 GetFirstPlayerController 로 한 번 더 시도.
	if (LocalPCCount == 0)
	{
		if (APlayerController* FirstPC = UGameplayStatics::GetPlayerController(World, 0))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[BossSummonCinematic_LiveCodeCheck] Fallback GetFirstPlayerController = %s — applying ViewTarget"),
				*FirstPC->GetName());
			if (AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(FirstPC))
			{
				HeroPC->EnterBossCinematic(ViewTarget, CameraBlendIn);
			}
			else
			{
				FirstPC->SetViewTargetWithBlend(ViewTarget, FMath::Max(CameraBlendIn, 0.1f),
					EViewTargetBlendFunction::VTBlend_EaseInOut, 2.f, false);
			}
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[BossSummonCinematic_LiveCodeCheck] No local PC found at all — cinematic camera will not switch"));
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

	// [ClientBossResolveV1] Start 폴링이 아직 돌고 있으면 취소 (End 도착했으니 더 기다릴 의미 없음).
	World->GetTimerManager().ClearTimer(ClientCinematicRetryTimer);

	// [CinematicShakeV1] 반복 카메라 쉐이크 타이머 정리.
	World->GetTimerManager().ClearTimer(CinematicShakeTimer);

	// [PortalRevealV1] reveal/emerge 대기 타이머 정리 + 보스 visibility/clip 정리 (Failsafe 가 emerge 전 발화한 경우 대비).
	World->GetTimerManager().ClearTimer(PortalRevealTimerLocal);
	World->GetTimerManager().ClearTimer(BossEmergeTimerLocal);
	if (APawn* BossLocal = LocalCinematicBoss.Get())
	{
		if (AHellunaEnemyCharacter* EnemyBoss = Cast<AHellunaEnemyCharacter>(BossLocal))
		{
			EnemyBoss->StopPortalClipPlaneVisuals();
		}
		if (ACharacter* BossCh = Cast<ACharacter>(BossLocal))
		{
			if (USkeletalMeshComponent* Mesh = BossCh->GetMesh())
			{
				Mesh->SetVisibility(true, true);
			}
		}
	}

	// [ClientBossResolveV1] Start 가 보스 못 받아 LocalCinematicBoss 가 NULL 인 경우 — HP 바 스폰 위해
	// 클라 월드에서 직접 보스 검색.
	if (!LocalCinematicBoss.IsValid())
	{
		if (APawn* Resolved = ResolveTargetBoss())
		{
			LocalCinematicBoss = Resolved;
			UE_LOG(LogTemp, Warning,
				TEXT("[BossSummonCinematic_LiveCodeCheck] Client: LocalCinematicBoss was null at End — resolved by tag (%s)"),
				*Resolved->GetName());
		}
	}

	// [ClientCinematicV2] 로컬 PlayerController 카메라 복귀 + 입력 복원.
	//   Start 와 동일하게 ALL local PCs 순회 + 폴백.
	int32 EndLocalPCCount = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalPlayerController())
		{
			continue;
		}
		++EndLocalPCCount;
		if (AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(PC))
		{
			HeroPC->ExitBossCinematic(CameraBlendOut);
		}
		else if (APawn* PCPawn = PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(PCPawn, FMath::Max(CameraBlendOut, 0.1f),
				EViewTargetBlendFunction::VTBlend_EaseOut, 2.f, false);
		}
	}
	if (EndLocalPCCount == 0)
	{
		if (APlayerController* FirstPC = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(FirstPC))
			{
				HeroPC->ExitBossCinematic(CameraBlendOut);
			}
			else if (APawn* PCPawn = FirstPC->GetPawn())
			{
				FirstPC->SetViewTargetWithBlend(PCPawn, FMath::Max(CameraBlendOut, 0.1f),
					EViewTargetBlendFunction::VTBlend_EaseOut, 2.f, false);
			}
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

	// [PortalCutsV1] 클라 컷 진행 Tick 종료 + 상태 리셋.
	//   서버 walk 분기는 HandleCinematicCompletedServer 에서 자체 SetActorTickEnabled(false) 호출.
	//   리슨 서버에서도 두 호출이 모두 실행되지만 idempotent 라 안전.
	SetActorTickEnabled(false);
	CurrentCutIndex = -1;
	CutsElapsedTime = 0.f;

	// [PortalCutsV1] 포탈 파괴 — 보스가 빠져나오는 잔상용 지연 후 destroy.
	if (LocalPortalActor.IsValid())
	{
		AActor* PortalPtr = LocalPortalActor.Get();
		const float Delay = FMath::Max(PortalDestroyDelay, 0.05f);
		FTimerHandle PortalDestroyTimer;
		World->GetTimerManager().SetTimer(PortalDestroyTimer,
			[PortalPtr]()
			{
				if (IsValid(PortalPtr))
				{
					PortalPtr->Destroy();
				}
			},
			Delay, false);
		LocalPortalActor.Reset();
	}
}

void ABossSummonCinematicTrigger::OnBossEmergeElapsedLocal()
{
	APawn* Boss = LocalCinematicBoss.Get();
	if (!Boss) return;

	// [PortalClipV1] BossEmergeDelay 경과 — clip plane 끔. 이제 보스 전체 픽셀 visible.
	if (AHellunaEnemyCharacter* EnemyBoss = Cast<AHellunaEnemyCharacter>(Boss))
	{
		EnemyBoss->StopPortalClipPlaneVisuals();
		UE_LOG(LogTemp, Warning,
			TEXT("[BossSummonCinematic_LiveCodeCheck] Boss emerged — clip plane stopped"));
	}
	else if (ACharacter* BossChar = Cast<ACharacter>(Boss))
	{
		// 폴백: 메시 visibility 복원
		if (USkeletalMeshComponent* BossMesh = BossChar->GetMesh())
		{
			BossMesh->SetVisibility(true, true);
		}
	}
}

// [PortalCutsV1] 클라 매 Tick 호출 — 현재 컷의 소켓을 앵커로 보스 로컬축 기준 카메라 갱신.
//   카메라는 attach 되지 않고 world transform 을 직접 set 하므로 보스가 walk 하는 동안에도
//   카메라가 따라오면서 흔들림 없이 소켓을 추적.
void ABossSummonCinematicTrigger::UpdateCameraForCurrentCut()
{
	if (!LocalCameraActor.IsValid() || !LocalCinematicBoss.IsValid()) return;
	if (!CameraCuts.IsValidIndex(CurrentCutIndex)) return;

	APawn* Boss = LocalCinematicBoss.Get();
	ACameraActor* Cam = LocalCameraActor.Get();
	const FBossCinematicCut& Cut = CameraCuts[CurrentCutIndex];

	// [PortalAnchorV1] anchor 결정 — 포탈 anchor 면 정적, 보스 anchor 면 보스 추적.
	FVector AnchorWorld;
	FVector Forward, Right, Up;

	if (Cut.bAnchorToPortal && LocalPortalActor.IsValid())
	{
		AActor* Portal = LocalPortalActor.Get();
		AnchorWorld = Portal->GetActorLocation();
		Forward = Portal->GetActorForwardVector();
		Right   = Portal->GetActorRightVector();
		Up      = Portal->GetActorUpVector();
	}
	else
	{
		// 보스 anchor (기본): socket 있으면 socket world, 없으면 actor root.
		AnchorWorld = Boss->GetActorLocation();
		if (ACharacter* BossChar = Cast<ACharacter>(Boss))
		{
			if (USkeletalMeshComponent* Mesh = BossChar->GetMesh())
			{
				if (!Cut.Socket.IsNone() && Mesh->DoesSocketExist(Cut.Socket))
				{
					AnchorWorld = Mesh->GetSocketLocation(Cut.Socket);
				}
			}
		}
		Forward = Boss->GetActorForwardVector();
		Right   = Boss->GetActorRightVector();
		Up      = Boss->GetActorUpVector();
	}

	const FVector CameraWorld = AnchorWorld
		+ Forward * Cut.CameraOffset.X
		+ Right   * Cut.CameraOffset.Y
		+ Up      * Cut.CameraOffset.Z;

	const FVector LookAtWorld = AnchorWorld
		+ Forward * Cut.LookAtOffset.X
		+ Right   * Cut.LookAtOffset.Y
		+ Up      * Cut.LookAtOffset.Z;

	const FVector LookDir = LookAtWorld - CameraWorld;
	const FRotator NewRot = LookDir.IsNearlyZero()
		? Boss->GetActorRotation()
		: LookDir.Rotation();

	Cam->SetActorLocationAndRotation(CameraWorld, NewRot);
}

// [CinematicShakeV1] 클라 로컬 1회 쉐이크 발화. 데디 서버는 즉시 스킵.
void ABossSummonCinematicTrigger::TriggerLocalCinematicShake()
{
	if (!CinematicShakeClass) return;

	UWorld* World = GetWorld();
	if (!World) return;
	if (World->GetNetMode() == NM_DedicatedServer) return;

	// Origin: 보스 위치(있으면) 또는 트리거 본체. 거리 기반 falloff 의 기준점.
	FVector Origin = GetActorLocation();
	if (APawn* Boss = LocalCinematicBoss.Get())
	{
		Origin = Boss->GetActorLocation();
	}

	UGameplayStatics::PlayWorldCameraShake(
		World,
		CinematicShakeClass,
		Origin,
		CinematicShakeInnerRadius,
		CinematicShakeOuterRadius,
		CinematicShakeFalloff);
}
