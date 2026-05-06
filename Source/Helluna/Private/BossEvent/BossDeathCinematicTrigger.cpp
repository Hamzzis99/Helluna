// Capstone Project Helluna

#include "BossEvent/BossDeathCinematicTrigger.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Controller/HellunaHeroController.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
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
}

bool ABossDeathCinematicTrigger::TryActivate(APawn* InTargetBoss)
{
	if (!HasAuthority()) return false;
	if (!InTargetBoss) return false;
	ActiveBoss = InTargetBoss;
	Multicast_StartCinematic(InTargetBoss);
	return true;
}

void ABossDeathCinematicTrigger::Multicast_StartCinematic_Implementation(APawn* Boss)
{
	if (!Boss) return;
	ActiveBoss = Boss;

	UWorld* World = GetWorld();
	if (!World) return;

	// =========================================================
	// 카메라 spawn — 첫 Tick 위치 = head 본 기준 face close-up
	// =========================================================
	const FVector BossLoc = Boss->GetActorLocation();
	const FVector Forward = Boss->GetActorForwardVector();
	const FVector Right = Boss->GetActorRightVector();
	const FVector Up = Boss->GetActorUpVector();

	// 초기 anchor — head 본이 있으면 head, 없으면 보스 위치 + 기본 90cm.
	FVector Anchor = BossLoc + FVector(0.f, 0.f, 90.f);
	if (ACharacter* BossChar = Cast<ACharacter>(Boss))
	{
		if (USkeletalMeshComponent* SkelMesh = BossChar->GetMesh())
		{
			if (HeadBoneName != NAME_None && SkelMesh->DoesSocketExist(HeadBoneName))
			{
				const FVector HeadLoc = SkelMesh->GetSocketLocation(HeadBoneName);
				Anchor = FMath::Lerp(BossLoc + FVector(0.f, 0.f, 90.f), HeadLoc, FMath::Clamp(HeadFollowAlpha, 0.f, 1.f));
			}
		}
	}

	const FVector StartCamLoc = Anchor + Forward * FaceOffset.X + Right * FaceOffset.Y + Up * FaceOffset.Z;
	const FVector StartLookTarget = Anchor + FVector(0.f, 0.f, LookAtZOffset);
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

void ABossDeathCinematicTrigger::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!LocalCameraActor.IsValid()) return;
	APawn* Boss = ActiveBoss.Get();
	if (!Boss) return;

	// 매 Tick 카메라를 boss orientation + head bone 위치에 맞춰 갱신.
	const FVector BossLoc = Boss->GetActorLocation();
	const FVector Forward = Boss->GetActorForwardVector();
	const FVector Right = Boss->GetActorRightVector();
	const FVector Up = Boss->GetActorUpVector();

	FVector Anchor = BossLoc + FVector(0.f, 0.f, 90.f);
	if (ACharacter* BossChar = Cast<ACharacter>(Boss))
	{
		if (USkeletalMeshComponent* SkelMesh = BossChar->GetMesh())
		{
			if (HeadBoneName != NAME_None && SkelMesh->DoesSocketExist(HeadBoneName))
			{
				const FVector HeadLoc = SkelMesh->GetSocketLocation(HeadBoneName);
				Anchor = FMath::Lerp(BossLoc + FVector(0.f, 0.f, 90.f), HeadLoc, FMath::Clamp(HeadFollowAlpha, 0.f, 1.f));
			}
		}
	}

	const FVector CamLoc = Anchor + Forward * FaceOffset.X + Right * FaceOffset.Y + Up * FaceOffset.Z;
	const FVector LookTarget = Anchor + FVector(0.f, 0.f, LookAtZOffset);
	const FRotator NewRot = (LookTarget - CamLoc).Rotation();
	LocalCameraActor->SetActorLocationAndRotation(CamLoc, NewRot);
}
