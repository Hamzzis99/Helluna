// Capstone Project Helluna

#include "BossEvent/PhantomBladesZone.h"

#include "BossEvent/PhantomBladeActor.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaHeroCharacter.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

#define PB_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[PhantomBlades] " Fmt), ##__VA_ARGS__)

APhantomBladesZone::APhantomBladesZone()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PostProcessComp = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcessComp->SetupAttachment(RootComponent);
	PostProcessComp->bUnbound = true;
	PostProcessComp->bEnabled = false;
}

// -----------------------------------------------------------------
// ActivateZone
// -----------------------------------------------------------------
void APhantomBladesZone::ActivateZone()
{
	Super::ActivateZone();

	if (bZoneActive) return;
	bZoneActive = true;
	ElapsedTime = 0.f;
	CurrentPhase = 0;
	TrailSpawnAccumulator = 0.f;
	DistortionElapsedTime = 0.f;
	TrailNodes.Empty();
	TrailDamageLastTime.Empty();

	// 초기 주파수 설정
	CurrentFreqX = Phase1FreqX;
	CurrentFreqY = Phase1FreqY;
	CurrentSpeed = BaseSpeed;

	PB_LOG("=== ActivateZone (Blades=%d, Freq=%d:%d, Speed=%.1f) ===",
		BladeCount, CurrentFreqX, CurrentFreqY, CurrentSpeed);

	// 검 초기화 — 각 검은 서로 다른 위상으로 시작
	Blades.Empty();
	Blades.SetNum(BladeCount);
	UWorld* SpawnWorld = GetWorld();
	for (int32 i = 0; i < BladeCount; ++i)
	{
		Blades[i].PhaseDelta = (2.f * PI * static_cast<float>(i)) / static_cast<float>(BladeCount);
		Blades[i].TimeParam = 0.f;
		Blades[i].CurrentPosition = CalculateLissajousPosition(0.f, Blades[i].PhaseDelta);
		Blades[i].LastHitTime.Empty();

		// BladeActor 스폰
		if (SpawnWorld && BladeActorClass)
		{
			FActorSpawnParameters Params;
			Params.Owner = this;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			APhantomBladeActor* BA = SpawnWorld->SpawnActor<APhantomBladeActor>(
				BladeActorClass,
				Blades[i].CurrentPosition, FRotator::ZeroRotator, Params);

			if (BA)
			{
				Blades[i].BladeActor = BA;
			}
		}
	}

	// 사운드
	if (BladeSound && OwnerEnemy)
	{
		OwnerEnemy->Multicast_PlaySoundAtLocation(BladeSound, GetActorLocation());
	}

	// 공간 왜곡 PP 활성화 (전 클라이언트)
	if (DistortionPPMaterial)
	{
		Multicast_ActivateDistortion();
	}

	// 패턴 종료 타이머
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			PatternEndTimerHandle, this,
			&APhantomBladesZone::OnPatternTimeUp,
			PatternDuration, false
		);
	}

	SetActorTickEnabled(true);
}

// -----------------------------------------------------------------
// DeactivateZone
// -----------------------------------------------------------------
void APhantomBladesZone::DeactivateZone()
{
	Super::DeactivateZone();

	bZoneActive = false;
	SetActorTickEnabled(false);

	for (FBladeState& Blade : Blades)
	{
		if (APhantomBladeActor* BA = Blade.BladeActor.Get())
		{
			BA->DeactivateVFX();
			BA->Destroy();
		}
	}
	Blades.Empty();
	TrailNodes.Empty();
	TrailDamageLastTime.Empty();

	Multicast_DeactivateDistortion();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PatternEndTimerHandle);
	}
}

// -----------------------------------------------------------------
// Tick
// -----------------------------------------------------------------
void APhantomBladesZone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 공간 왜곡 PP는 클라/서버 모두에서 시각적 업데이트
	UpdateDistortionMaterial(DeltaTime);

	// 게임플레이 로직(데미지/VFX 트리거)은 서버에서만. 클라는 멀티캐스트 수신만.
	if (!HasAuthority()) return;

	if (!bZoneActive) return;

	ElapsedTime += DeltaTime;

	UpdatePhase();
	UpdateBlades(DeltaTime);
	ProcessBladeHits();
	ApplyInputDistortion(DeltaTime);

	TrailSpawnAccumulator += DeltaTime;
	if (TrailSpawnAccumulator >= TrailSpawnInterval)
	{
		TrailSpawnAccumulator -= TrailSpawnInterval;
		SpawnTrailNodes();
	}

	ProcessTrails();
}

// -----------------------------------------------------------------
// CalculateLissajousPosition — 리사주 곡선
// -----------------------------------------------------------------
FVector APhantomBladesZone::CalculateLissajousPosition(float T, float PhaseDelta) const
{
	const FVector Center = GetActorLocation();

	// Lissajous: x = A*sin(a*t + delta), y = B*sin(b*t)
	const float X = TrajectoryRadiusX * FMath::Sin(
		static_cast<float>(CurrentFreqX) * T + PhaseDelta);
	const float Y = TrajectoryRadiusY * FMath::Sin(
		static_cast<float>(CurrentFreqY) * T);

	return Center + FVector(X, Y, BladeHeight);
}

// -----------------------------------------------------------------
// UpdatePhase — 주파수비 전환
// -----------------------------------------------------------------
void APhantomBladesZone::UpdatePhase()
{
	int32 NewPhase;
	if (ElapsedTime < Phase1Duration)
	{
		NewPhase = 1;
	}
	else if (ElapsedTime < Phase1Duration + Phase2Duration)
	{
		NewPhase = 2;
	}
	else
	{
		NewPhase = 3;
	}

	if (NewPhase == CurrentPhase) return;
	CurrentPhase = NewPhase;

	// 주파수 및 속도 변경
	switch (CurrentPhase)
	{
	case 1:
		CurrentFreqX = Phase1FreqX;
		CurrentFreqY = Phase1FreqY;
		CurrentSpeed = BaseSpeed;
		break;
	case 2:
		CurrentFreqX = Phase2FreqX;
		CurrentFreqY = Phase2FreqY;
		CurrentSpeed = BaseSpeed * SpeedEscalation;
		break;
	case 3:
		CurrentFreqX = Phase3FreqX;
		CurrentFreqY = Phase3FreqY;
		CurrentSpeed = BaseSpeed * SpeedEscalation * SpeedEscalation;
		break;
	default: break;
	}

	PB_LOG("=== Phase %d: Freq=%d:%d, Speed=%.2f ===",
		CurrentPhase, CurrentFreqX, CurrentFreqY, CurrentSpeed);

	// 전환 VFX
	if (PhaseChangeVFX && OwnerEnemy)
	{
		OwnerEnemy->MulticastPlayEffect(GetActorLocation(), PhaseChangeVFX, PhaseChangeVFXScale, false);
	}

	// 전환 사운드
	if (PhaseChangeSound && OwnerEnemy)
	{
		OwnerEnemy->Multicast_PlaySoundAtLocation(PhaseChangeSound, GetActorLocation());
	}
}

// -----------------------------------------------------------------
// UpdateBlades — 검 위치 이동
// -----------------------------------------------------------------
void APhantomBladesZone::UpdateBlades(float DeltaTime)
{
	for (FBladeState& Blade : Blades)
	{
		const FVector PrevPosition = Blade.CurrentPosition;
		Blade.TimeParam += CurrentSpeed * DeltaTime;
		Blade.CurrentPosition = CalculateLissajousPosition(Blade.TimeParam, Blade.PhaseDelta);

		if (APhantomBladeActor* BA = Blade.BladeActor.Get())
		{
			BA->SetActorLocation(Blade.CurrentPosition);

			const FVector Direction = Blade.CurrentPosition - PrevPosition;
			if (!Direction.IsNearlyZero())
			{
				BA->SetActorRotation(Direction.Rotation());
			}
		}
	}
}

// -----------------------------------------------------------------
// ProcessBladeHits — 검 직격 판정
// -----------------------------------------------------------------
void APhantomBladesZone::ProcessBladeHits()
{
	UWorld* World = GetWorld();
	if (!World || !OwnerEnemy)
	{
		PB_LOG("ProcessBladeHits: SKIP (World=%d, OwnerEnemy=%d)", World != nullptr, OwnerEnemy != nullptr);
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();
	const float HitRadiusSq = BladeHitRadius * BladeHitRadius;

	int32 PlayerCount = 0;
	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		AHellunaHeroCharacter* Player = *It;
		if (!IsValid(Player)) continue;
		++PlayerCount;

		const FVector PlayerLoc = Player->GetActorLocation();
		TWeakObjectPtr<AActor> WeakPlayer(Player);

		for (int32 i = 0; i < Blades.Num(); ++i)
		{
			FBladeState& Blade = Blades[i];
			const float Dist = FVector::Dist(PlayerLoc, Blade.CurrentPosition);

			// 가까울 때만 로그 (반지름의 3배 이내)
			if (Dist < BladeHitRadius * 3.f)
			{
				PB_LOG("Blade[%d] <-> [%s] Dist=%.0f / Radius=%.0f %s",
					i, *Player->GetName(), Dist, BladeHitRadius,
					Dist <= BladeHitRadius ? TEXT("** HIT **") : TEXT("(near)"));
			}

			if (Dist > BladeHitRadius) continue;

			// 쿨타임 체크
			const double* LastHit = Blade.LastHitTime.Find(WeakPlayer);
			if (LastHit && (CurrentTime - *LastHit) < BladeHitCooldown)
			{
				PB_LOG("Blade[%d] hit [%s] blocked by cooldown (%.1fs left)",
					i, *Player->GetName(), BladeHitCooldown - (CurrentTime - *LastHit));
				continue;
			}

			Blade.LastHitTime.FindOrAdd(WeakPlayer) = CurrentTime;

			const float DmgApplied = UGameplayStatics::ApplyDamage(
				Player, BladeDirectDamage,
				OwnerEnemy->GetController(), OwnerEnemy,
				UDamageType::StaticClass()
			);

			PB_LOG("Blade[%d] DAMAGE [%s] -> %.0f (returned %.0f)",
				i, *Player->GetName(), BladeDirectDamage, DmgApplied);
		}
	}

	if (PlayerCount == 0)
	{
		PB_LOG("ProcessBladeHits: No HeroCharacter found in world!");
	}
}

// -----------------------------------------------------------------
// SpawnTrailNodes — 검 위치에 트레일 생성
// -----------------------------------------------------------------
void APhantomBladesZone::SpawnTrailNodes()
{
	UWorld* World = GetWorld();
	if (!World) return;

	const double CurrentTime = World->GetTimeSeconds();

	for (const FBladeState& Blade : Blades)
	{
		// 최대 트레일 수 초과 시 가장 오래된 것 제거
		while (TrailNodes.Num() >= MaxTrailNodes)
		{
			TrailNodes.RemoveAt(0);
		}

		FTrailNode Node;
		Node.Position = Blade.CurrentPosition;
		Node.SpawnWorldTime = CurrentTime;
		TrailNodes.Add(MoveTemp(Node));

		// 트레일 VFX
		if (TrailVFX && OwnerEnemy)
		{
			OwnerEnemy->MulticastPlayEffect(Blade.CurrentPosition, TrailVFX, TrailVFXScale, false);
		}
	}
}

// -----------------------------------------------------------------
// ProcessTrails — 트레일 데미지 + 소멸
// -----------------------------------------------------------------
void APhantomBladesZone::ProcessTrails()
{
	UWorld* World = GetWorld();
	if (!World || !OwnerEnemy) return;

	const double CurrentTime = World->GetTimeSeconds();
	const float TrailRadiusSq = TrailHitRadius * TrailHitRadius;

	// 만료된 트레일 제거 (앞에서부터 — 시간순)
	while (TrailNodes.Num() > 0 && (CurrentTime - TrailNodes[0].SpawnWorldTime) > TrailLifetime)
	{
		TrailNodes.RemoveAt(0);
	}

	if (TrailNodes.Num() == 0) return;

	// 플레이어 데미지 판정
	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		AHellunaHeroCharacter* Player = *It;
		if (!IsValid(Player)) continue;

		TWeakObjectPtr<AActor> WeakPlayer(Player);

		// 쿨타임 체크
		const double* LastTime = TrailDamageLastTime.Find(WeakPlayer);
		if (LastTime && (CurrentTime - *LastTime) < TrailHitCooldown) continue;

		const FVector PlayerLoc = Player->GetActorLocation();
		bool bHitTrail = false;

		// 모든 트레일 노드와 거리 체크 (가장 가까운 것만 찾으면 됨)
		// 성능 최적화: 일정 간격으로 샘플링 (4개마다 1개)
		const int32 Step = FMath::Max(1, TrailNodes.Num() / 50);
		for (int32 i = 0; i < TrailNodes.Num(); i += Step)
		{
			if (FVector::DistSquared(PlayerLoc, TrailNodes[i].Position) <= TrailRadiusSq)
			{
				bHitTrail = true;
				break;
			}
		}

		if (bHitTrail)
		{
			TrailDamageLastTime.FindOrAdd(WeakPlayer) = CurrentTime;

			const float DmgApplied = UGameplayStatics::ApplyDamage(
				Player, TrailDamage,
				OwnerEnemy->GetController(), OwnerEnemy,
				UDamageType::StaticClass()
			);

			PB_LOG("Trail DAMAGE [%s] -> %.0f (returned %.0f, nodes=%d)",
				*Player->GetName(), TrailDamage, DmgApplied, TrailNodes.Num());
		}
	}
}

// -----------------------------------------------------------------
// OnPatternTimeUp
// -----------------------------------------------------------------
void APhantomBladesZone::OnPatternTimeUp()
{
	PB_LOG("=== Pattern time up ===");

	bZoneActive = false;
	SetActorTickEnabled(false);

	for (FBladeState& Blade : Blades)
	{
		if (APhantomBladeActor* BA = Blade.BladeActor.Get())
		{
			BA->DeactivateVFX();
			BA->Destroy();
		}
	}
	Blades.Empty();
	TrailNodes.Empty();

	Multicast_DeactivateDistortion();

	NotifyPatternFinished(false);
}

// -----------------------------------------------------------------
// 공간 왜곡 — 포스트프로세스 활성화/해제
// -----------------------------------------------------------------
void APhantomBladesZone::Multicast_ActivateDistortion_Implementation()
{
	if (!DistortionPPMaterial || !PostProcessComp) return;

	DistortionMID = UMaterialInstanceDynamic::Create(DistortionPPMaterial, this);
	if (!DistortionMID) return;

	DistortionMID->SetScalarParameterValue(FName("DistortionRadius"), DistortionRadius);
	DistortionMID->SetScalarParameterValue(FName("DistortionStrength"), DistortionStrength);
	DistortionMID->SetScalarParameterValue(FName("ElapsedTime"), 0.f);
	DistortionMID->SetVectorParameterValue(FName("PhantomCenter"),
		FLinearColor(GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z, 0.f));

	PostProcessComp->Settings = FPostProcessSettings();
	PostProcessComp->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, DistortionMID));
	PostProcessComp->bEnabled = true;

	// 클라이언트에서도 Tick으로 PP 파라미터를 갱신해야 함
	SetActorTickEnabled(true);

	PB_LOG("[PP] Spatial Distortion activated (Radius=%.0f, Strength=%.2f)",
		DistortionRadius, DistortionStrength);
}

void APhantomBladesZone::Multicast_DeactivateDistortion_Implementation()
{
	if (PostProcessComp)
	{
		PostProcessComp->bEnabled = false;
		PostProcessComp->Settings.WeightedBlendables.Array.Empty();
	}
	DistortionMID = nullptr;

	PB_LOG("[PP] Spatial Distortion deactivated");
}

// -----------------------------------------------------------------
// UpdateDistortionMaterial — 검 위치 + 시간 파라미터 갱신
// -----------------------------------------------------------------
void APhantomBladesZone::UpdateDistortionMaterial(float DeltaTime)
{
	if (!DistortionMID || !PostProcessComp || !PostProcessComp->bEnabled) return;

	UWorld* World = GetWorld();
	if (!World) return;

	// 로컬 플레이어 카메라 위치
	FVector CameraLoc = GetActorLocation();
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		FRotator CamRot;
		PC->GetPlayerViewPoint(CameraLoc, CamRot);
	}

	// 서버: Blades 배열 우선 사용. 클라: TActorIterator로 BladeActor 수집.
	FVector ClosestBladePos = GetActorLocation();
	float ClosestDistSq = FLT_MAX;
	bool bFound = false;

	if (HasAuthority() && Blades.Num() > 0)
	{
		for (const FBladeState& Blade : Blades)
		{
			const float DistSq = FVector::DistSquared(CameraLoc, Blade.CurrentPosition);
			if (DistSq < ClosestDistSq)
			{
				ClosestDistSq = DistSq;
				ClosestBladePos = Blade.CurrentPosition;
				bFound = true;
			}
		}
	}
	else
	{
		for (TActorIterator<APhantomBladeActor> It(World); It; ++It)
		{
			APhantomBladeActor* BA = *It;
			if (!IsValid(BA)) continue;
			if (BA->GetOwner() != this) continue;

			const FVector Pos = BA->GetActorLocation();
			const float DistSq = FVector::DistSquared(CameraLoc, Pos);
			if (DistSq < ClosestDistSq)
			{
				ClosestDistSq = DistSq;
				ClosestBladePos = Pos;
				bFound = true;
			}
		}
	}

	if (!bFound) return;

	DistortionMID->SetVectorParameterValue(FName("PhantomCenter"),
		FLinearColor(ClosestBladePos.X, ClosestBladePos.Y, ClosestBladePos.Z, 1.f));
	DistortionMID->SetScalarParameterValue(FName("DistortionRadius"), DistortionRadius);
	DistortionMID->SetScalarParameterValue(FName("DistortionStrength"), DistortionStrength);

	// 셰이더 애니메이션용 누적 시간 (클라/서버 각자 독립 카운터 — 단순 노이즈 시드)
	DistortionElapsedTime += DeltaTime;
	DistortionMID->SetScalarParameterValue(FName("ElapsedTime"), DistortionElapsedTime);
}

// -----------------------------------------------------------------
// ApplyInputDistortion — 검 근처 플레이어에게 시점 회전 노이즈 적용
// -----------------------------------------------------------------
void APhantomBladesZone::ApplyInputDistortion(float DeltaTime)
{
	if (InputDistortionRadius <= 0.f || InputDistortionYawRate <= 0.f) return;

	UWorld* World = GetWorld();
	if (!World) return;

	const float RadiusSq = InputDistortionRadius * InputDistortionRadius;

	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		AHellunaHeroCharacter* Player = *It;
		if (!IsValid(Player)) continue;

		AController* Controller = Player->GetController();
		if (!Controller) continue;

		// 가장 가까운 검까지의 거리
		float ClosestDistSq = FLT_MAX;
		const FVector PlayerLoc = Player->GetActorLocation();
		for (const FBladeState& Blade : Blades)
		{
			const float DSq = FVector::DistSquared(PlayerLoc, Blade.CurrentPosition);
			if (DSq < ClosestDistSq) ClosestDistSq = DSq;
		}

		if (ClosestDistSq > RadiusSq) continue;

		// 거리에 반비례해서 강해지는 yaw 노이즈
		const float Dist = FMath::Sqrt(ClosestDistSq);
		const float Alpha = 1.f - (Dist / InputDistortionRadius);         // 0~1
		const float Magnitude = InputDistortionYawRate * Alpha * DeltaTime;

		// sin(t) 기반 좌우 진동
		const float T = World->GetTimeSeconds() * 6.f;
		const float YawDelta = FMath::Sin(T) * Magnitude;
		const float PitchDelta = FMath::Cos(T * 1.3f) * Magnitude * 0.4f;

		FRotator ControlRot = Controller->GetControlRotation();
		ControlRot.Yaw += YawDelta;
		ControlRot.Pitch = FMath::Clamp(ControlRot.Pitch + PitchDelta, -89.f, 89.f);
		Controller->SetControlRotation(ControlRot);
	}
}

#undef PB_LOG
