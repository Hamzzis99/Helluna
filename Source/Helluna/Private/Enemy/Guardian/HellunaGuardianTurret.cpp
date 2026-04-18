// File: Source/Helluna/Private/Enemy/Guardian/HellunaGuardianTurret.cpp

#include "Enemy/Guardian/HellunaGuardianTurret.h"
#include "Enemy/Guardian/HellunaGuardianProjectile.h"

#include "Helluna.h"
#include "Character/HellunaHeroCharacter.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "GameMode/HellunaDefenseGameState.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
#include "DrawDebugHelpers.h"
#endif


// =========================================================
// 생성자
// =========================================================

AHellunaGuardianTurret::AHellunaGuardianTurret()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;

	bReplicates = true;
	SetReplicateMovement(false);
	bAlwaysRelevant = true;

	// ── 고정 루트 ─────────────────────────────────────────────
	TurretRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TurretRoot"));
	SetRootComponent(TurretRoot);

	// ── 몸체 메쉬 (고정) ─────────────────────────────────────
	MeshBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshBody"));
	MeshBody->SetupAttachment(TurretRoot);

	// ── 감지 구체 (고정) ─────────────────────────────────────
	DetectionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("DetectionSphere"));
	DetectionSphere->SetupAttachment(TurretRoot);
	// 주의: SetSphereRadius 는 BP SCS 의 사용자 지정 반경/스케일을 덮어쓰므로 여기서 호출하지 않는다.
	// BP 인스턴스에서 Unscaled Radius / Scale3D 로 직접 조정.
	DetectionSphere->SetCollisionProfileName(TEXT("Trigger"));
	DetectionSphere->SetGenerateOverlapEvents(true);
	DetectionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	DetectionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	DetectionSphere->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::OnDetectionBeginOverlap);
	DetectionSphere->OnComponentEndOverlap.AddUniqueDynamic(this, &ThisClass::OnDetectionEndOverlap);

	// ── 헤드 회전 피벗 ──────────────────────────────────────
	TurretHead = CreateDefaultSubobject<USceneComponent>(TEXT("TurretHead"));
	TurretHead->SetupAttachment(TurretRoot);

	// ── 헤드 메쉬 (회전) ────────────────────────────────────
	MeshHead = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshHead"));
	MeshHead->SetupAttachment(TurretHead);

	// ── 총구 (회전) ─────────────────────────────────────────
	MuzzlePoint = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzlePoint"));
	MuzzlePoint->SetupAttachment(MeshHead);
	MuzzlePoint->SetRelativeLocation(FVector(100.f, 0.f, 0.f));

	// ── 체력 컴포넌트 ───────────────────────────────────────
	HealthComponent = CreateDefaultSubobject<UHellunaHealthComponent>(TEXT("HealthComponent"));
}

// =========================================================
// BeginPlay
// =========================================================

void AHellunaGuardianTurret::BeginPlay()
{
	Super::BeginPlay();

	// MaxHealth 초기 동기화 (HealthComponent 내부 값이 BP 기본값과 다를 경우 대비)
	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddUniqueDynamic(this, &ThisClass::OnGuardianDeath);
	}

	// 서버 전용 초기화
	if (HasAuthority())
	{
		// BeginPlay 이전에 이미 DetectionSphere 안에 들어와 있던 Hero 는
		// OnComponentBeginOverlap 이벤트를 못 받음 → 수동으로 초기 오버랩 수집
		if (DetectionSphere)
		{
			TArray<AActor*> InitialOverlaps;
			DetectionSphere->GetOverlappingActors(InitialOverlaps, AHellunaHeroCharacter::StaticClass());
			for (AActor* A : InitialOverlaps)
			{
				if (AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(A))
				{
					PlayersInRange.AddUnique(TWeakObjectPtr<AHellunaHeroCharacter>(Hero));
				}
			}
		}

		if (UWorld* World = GetWorld())
		{
			if (AGameStateBase* GS = World->GetGameState())
			{
				CachedGameState = Cast<AHellunaDefenseGameState>(GS);
			}

			// GameState 가 아직 생성되지 않았을 수 있음 → 폴링 타이머가 추후 재시도
			CachedPhase = CachedGameState.IsValid() ? CachedGameState->GetPhase() : EDefensePhase::Day;
			bIsDay = (CachedPhase == EDefensePhase::Day);

			World->GetTimerManager().SetTimer(
				PhasePollTimerHandle,
				this,
				&ThisClass::PollDefensePhase,
				0.5f,
				true);
		}
	}
}

// =========================================================
// EndPlay
// =========================================================

void AHellunaGuardianTurret::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PhasePollTimerHandle);
	}

	if (HealthComponent && HealthComponent->IsRegistered())
	{
		HealthComponent->OnDeath.RemoveDynamic(this, &ThisClass::OnGuardianDeath);
	}

	StopAimBeam();

	PlayersInRange.Empty();
	CurrentTarget = nullptr;

	Super::EndPlay(EndPlayReason);
}

// =========================================================
// 리플리케이션
// =========================================================

void AHellunaGuardianTurret::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHellunaGuardianTurret, CurrentState);
	DOREPLIFETIME(AHellunaGuardianTurret, CurrentTarget);
	DOREPLIFETIME(AHellunaGuardianTurret, LockedFireTarget);
}

// =========================================================
// Tick — 서버: 상태 전이 / 공통: 헤드 회전
// =========================================================

void AHellunaGuardianTurret::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 공통: 헤드 회전 보간 (타겟 있을 때만)
	// Lock/FireDelay 모두 live 추적 — 발사 순간 PerformFire 에서 LockedFireTarget 확정
	if (AActor* Target = CurrentTarget.Get())
	{
		if (IsValid(Target) && !Target->IsActorBeingDestroyed())
		{
			const FVector AimPoint = GetAimPointFor(Target);
			UpdateHeadRotation(DeltaTime, AimPoint);

			// 공통: 조준 빔 엔드포인트 갱신 (Lock/FireDelay 모두 live)
			if (ActiveAimBeam && (CurrentState == EGuardianState::Lock ||
				CurrentState == EGuardianState::FireDelay))
			{
				UpdateAimBeamEndpoint(AimPoint);
			}
		}
	}

#if !UE_BUILD_SHIPPING
	if (bShowDebug)
	{
		const UWorld* World = GetWorld();
		if (World && DetectionSphere)
		{
			const FColor RangeColor =
				(CurrentState == EGuardianState::Idle)     ? FColor::Green :
				(CurrentState == EGuardianState::Detect)   ? FColor::Yellow :
				(CurrentState == EGuardianState::Lock)     ? FColor::Orange :
				(CurrentState == EGuardianState::FireDelay)? FColor::Red :
				(CurrentState == EGuardianState::Fire)     ? FColor::Magenta :
				(CurrentState == EGuardianState::Cooldown) ? FColor::Cyan :
				                                             FColor::Black;
			DrawDebugSphere(World, DetectionSphere->GetComponentLocation(),
				DetectionSphere->GetScaledSphereRadius(), 24, RangeColor, false, -1.f, 0, 2.f);
		}

		if (MuzzlePoint && CurrentTarget.Get())
		{
			const FVector Start = MuzzlePoint->GetComponentLocation();
			const FVector End = (CurrentState == EGuardianState::Fire)
				? FVector(LockedFireTarget)
				: GetAimPointFor(CurrentTarget.Get());
			const FColor LineColor = (CurrentState == EGuardianState::Lock ||
				CurrentState == EGuardianState::FireDelay ||
				CurrentState == EGuardianState::Fire) ? FColor::Red : FColor::Yellow;
			DrawDebugLine(GetWorld(), Start, End, LineColor, false, -1.f, 0, 1.5f);
		}
	}
#endif

	// 서버 전용: 상태 전이
	if (!HasAuthority())
	{
		return;
	}

	StateTimer += DeltaTime;

	switch (CurrentState)
	{
	case EGuardianState::Idle:
	{
		if (!bIsDay)
		{
			break;
		}
		if (!IsTargetValid())
		{
			SelectClosestTarget();
		}
		AActor* Target = CurrentTarget.Get();
		if (Target && IsTargetInRange(Target) && HasLineOfSightTo(GetAimPointFor(Target)))
		{
			SetState(EGuardianState::Detect);
		}
		break;
	}
	case EGuardianState::Detect:
	{
		AActor* Target = CurrentTarget.Get();
		if (!Target || !IsTargetValid())
		{
			SelectClosestTarget();
			Target = CurrentTarget.Get();
		}
		if (!Target || !IsTargetInRange(Target))
		{
			SetState(EGuardianState::Idle);
			break;
		}
		const FVector AimPoint = GetAimPointFor(Target);
		if (!HasLineOfSightTo(AimPoint))
		{
			SetState(EGuardianState::Idle);
			break;
		}
		if (IsFacingTarget(AimPoint))
		{
			SetState(EGuardianState::Lock);
		}
		break;
	}
	case EGuardianState::Lock:
	{
		AActor* Target = CurrentTarget.Get();
		if (!Target || !IsTargetValid() || !IsTargetInRange(Target))
		{
			// 비대칭 시야: 반경 이탈 만으로 해제 (LoS 상실로는 해제 불가)
			SetState(EGuardianState::Idle);
			break;
		}
		if (StateTimer >= LockDuration)
		{
			SetState(EGuardianState::FireDelay);
		}
		break;
	}
	case EGuardianState::FireDelay:
	{
		AActor* Target = CurrentTarget.Get();
		if (!Target || !IsTargetValid() || !IsTargetInRange(Target))
		{
			SetState(EGuardianState::Idle);
			break;
		}
		if (StateTimer >= FireDelayDuration)
		{
			// 발사 순간 타겟 위치 캡처 → 레이저·투사체 방향 일치
			LockedFireTarget = GetAimPointFor(Target);
			SetState(EGuardianState::Fire);
			PerformFire();
		}
		break;
	}
	case EGuardianState::Fire:
	{
		if (StateTimer >= FireFlashDuration)
		{
			SetState(EGuardianState::Cooldown);
		}
		break;
	}
	case EGuardianState::Cooldown:
	{
		if (StateTimer < CooldownDuration)
		{
			break;
		}
		if (!IsTargetValid())
		{
			SelectClosestTarget();
		}
		AActor* Target = CurrentTarget.Get();
		if (Target && IsTargetInRange(Target) && HasLineOfSightTo(GetAimPointFor(Target)))
		{
			SetState(EGuardianState::Detect);
		}
		else
		{
			SetState(EGuardianState::Idle);
		}
		break;
	}
	case EGuardianState::Dead:
	default:
		break;
	}
}

// =========================================================
// 상태 전이 헬퍼
// =========================================================

void AHellunaGuardianTurret::SetState(EGuardianState NewState)
{
	if (CurrentState == NewState)
	{
		return;
	}

	UE_LOG(LogHellunaGuardian, Verbose, TEXT("[%s] State %d -> %d"),
		*GetName(), static_cast<int32>(CurrentState), static_cast<int32>(NewState));

	CurrentState = NewState;
	StateTimer = 0.f;

	// Idle 진입 시 LockedFireTarget 초기화
	if (NewState == EGuardianState::Idle || NewState == EGuardianState::Dead)
	{
		LockedFireTarget = FVector::ZeroVector;
	}

	// 서버 로컬 조준 빔 적용 (클라는 OnRep_CurrentState 에서 동일 처리)
	ApplyAimBeamForState(NewState);

	Multicast_OnStateChanged(NewState);
}

// =========================================================
// 감지 오버랩 콜백
// =========================================================

void AHellunaGuardianTurret::OnDetectionBeginOverlap(
	UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (!HasAuthority() || CurrentState == EGuardianState::Dead)
	{
		return;
	}

	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(OtherActor);
	if (!Hero || Hero->IsActorBeingDestroyed())
	{
		return;
	}

	const UHellunaHealthComponent* HeroHealth = Hero->FindComponentByClass<UHellunaHealthComponent>();
	if (HeroHealth && HeroHealth->IsDead())
	{
		return;
	}

	PlayersInRange.AddUnique(Hero);
}

void AHellunaGuardianTurret::OnDetectionEndOverlap(
	UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/)
{
	if (!HasAuthority())
	{
		return;
	}

	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(OtherActor);
	if (!Hero)
	{
		return;
	}

	PlayersInRange.Remove(Hero);

	if (CurrentTarget.Get() == Hero)
	{
		CurrentTarget = nullptr;
	}
}

// =========================================================
// 타겟 선정·검증
// =========================================================

void AHellunaGuardianTurret::SelectClosestTarget()
{
	PlayersInRange.RemoveAll([](const TWeakObjectPtr<AHellunaHeroCharacter>& Weak)
	{
		if (!Weak.IsValid())
		{
			return true;
		}
		AHellunaHeroCharacter* Hero = Weak.Get();
		if (!IsValid(Hero) || Hero->IsActorBeingDestroyed())
		{
			return true;
		}
		const UHellunaHealthComponent* HealthComp = Hero->FindComponentByClass<UHellunaHealthComponent>();
		if (HealthComp && HealthComp->IsDead())
		{
			return true;
		}
		return false;
	});

	if (PlayersInRange.IsEmpty())
	{
		CurrentTarget = nullptr;
		return;
	}

	const FVector GuardianLocation = GetActorLocation();
	float ClosestDistSq = MAX_FLT;
	AHellunaHeroCharacter* ClosestHero = nullptr;

	for (const TWeakObjectPtr<AHellunaHeroCharacter>& Weak : PlayersInRange)
	{
		if (!Weak.IsValid())
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(GuardianLocation, Weak->GetActorLocation());
		if (DistSq < ClosestDistSq)
		{
			ClosestDistSq = DistSq;
			ClosestHero = Weak.Get();
		}
	}

	CurrentTarget = ClosestHero;
}

bool AHellunaGuardianTurret::IsTargetValid() const
{
	AActor* Target = CurrentTarget.Get();
	if (!IsValid(Target) || Target->IsActorBeingDestroyed())
	{
		return false;
	}

	const AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(Target);
	if (!Hero)
	{
		return false;
	}

	const UHellunaHealthComponent* HealthComp = Hero->FindComponentByClass<UHellunaHealthComponent>();
	if (HealthComp && HealthComp->IsDead())
	{
		return false;
	}

	return true;
}

bool AHellunaGuardianTurret::IsTargetInRange(const AActor* Target) const
{
	if (!Target)
	{
		return false;
	}
	const float DistSq = FVector::DistSquared(GetActorLocation(), Target->GetActorLocation());
	return DistSq <= (DetectionRadius * DetectionRadius);
}

bool AHellunaGuardianTurret::HasLineOfSightTo(const FVector& AimPoint) const
{
	const UWorld* World = GetWorld();
	if (!World || !TurretHead)
	{
		return false;
	}

	const FVector Start = TurretHead->GetComponentLocation();

	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	if (const AActor* Target = CurrentTarget.Get())
	{
		QueryParams.AddIgnoredActor(Target);
	}

	const bool bHit = World->LineTraceSingleByChannel(
		HitResult, Start, AimPoint, TraceChannel, QueryParams);

	// 아무것도 안 맞았으면 LoS 확보, 뭔가 맞았으면 가려진 것
	return !bHit;
}

FVector AHellunaGuardianTurret::GetAimPointFor(const AActor* Target) const
{
	if (!Target)
	{
		return FVector::ZeroVector;
	}
	return Target->GetActorLocation() + FVector(0.f, 0.f, TargetAimOffsetZ);
}

// =========================================================
// 헤드 회전 보간 (Yaw + Pitch, Roll = 0)
// =========================================================

void AHellunaGuardianTurret::UpdateHeadRotation(float DeltaTime, const FVector& AimPoint)
{
	if (!TurretHead)
	{
		return;
	}

	const FVector HeadLocation = TurretHead->GetComponentLocation();
	const FVector Direction = (AimPoint - HeadLocation).GetSafeNormal();

	if (Direction.IsNearlyZero())
	{
		return;
	}

	const FRotator DirRot = Direction.Rotation();
	const float ClampedPitch = FMath::Clamp(DirRot.Pitch, MinPitchAngle, MaxPitchAngle);
	const FRotator DesiredRotation(ClampedPitch, DirRot.Yaw, 0.f);

	const FRotator CurrentRotation = TurretHead->GetComponentRotation();
	const FRotator NewRotation = FMath::RInterpConstantTo(
		CurrentRotation, DesiredRotation, DeltaTime, HeadRotationSpeed);

	TurretHead->SetWorldRotation(NewRotation);
}

bool AHellunaGuardianTurret::IsFacingTarget(const FVector& AimPoint) const
{
	if (!TurretHead)
	{
		return false;
	}

	const FVector Direction = (AimPoint - TurretHead->GetComponentLocation()).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return true;
	}

	const FVector Forward = TurretHead->GetForwardVector();
	const float DotProduct = FVector::DotProduct(Forward, Direction);
	const float AngleDeg = FMath::RadiansToDegrees(
		FMath::Acos(FMath::Clamp(DotProduct, -1.f, 1.f)));

	return AngleDeg <= FireAngleThreshold;
}

// =========================================================
// 발사
// =========================================================

void AHellunaGuardianTurret::PerformFire()
{
	UWorld* World = GetWorld();
	if (!World || !MuzzlePoint)
	{
		return;
	}

	const FVector TraceStart = MuzzlePoint->GetComponentLocation();
	const FVector TraceEnd = LockedFireTarget;

	// ── 투사체 모드 (ProjectileClass 지정 시) ───────────────
	// 물리 투사체를 머즐에서 타겟 방향으로 발사.
	// 충돌·폭발·VFX 는 투사체 액터가 담당. 레거시 즉시 트레이스 경로 스킵.
	if (ProjectileClass)
	{
		const FVector AimDelta = TraceEnd - TraceStart;
		const FVector AimDir = AimDelta.GetSafeNormal();
		const FRotator SpawnRot = AimDir.Rotation();

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.Owner = this;

		AHellunaGuardianProjectile* Proj = World->SpawnActor<AHellunaGuardianProjectile>(
			ProjectileClass, TraceStart, SpawnRot, SpawnParams);
		if (Proj)
		{
			Proj->InitProjectile(
				Damage,
				ExplosionRadius,
				bExplosionFalloff,
				AimDir * ProjectileSpeed,
				ProjectileLifeSeconds);
		}

		// 발사 사운드는 머즐에서 멀티캐스트 (폭발 VFX 는 투사체 측에서 처리)
		Multicast_PlayFireFX(TraceStart, false, FVector::ZeroVector, FVector::ZeroVector);
		return;
	}

	// ── 레거시 모드 (ProjectileClass 미지정) — 즉시 라인트레이스 ─
	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	const bool bHit = World->LineTraceSingleByChannel(
		HitResult, TraceStart, TraceEnd, TraceChannel, QueryParams);

	const FVector ImpactLocation = bHit ? HitResult.ImpactPoint : TraceEnd;
	const FVector ImpactNormal = bHit
		? HitResult.ImpactNormal
		: (TraceStart - ImpactLocation).GetSafeNormal();
	Multicast_PlayFireFX(TraceStart, bHit, ImpactLocation, ImpactNormal);

	if (ExplosionRadius > 0.f)
	{
		TArray<AActor*> IgnoreActors;
		IgnoreActors.Add(this);
		UGameplayStatics::ApplyRadialDamage(
			this,
			Damage,
			ImpactLocation,
			ExplosionRadius,
			UDamageType::StaticClass(),
			IgnoreActors,
			this,
			nullptr,
			/*bDoFullDamage=*/!bExplosionFalloff,
			TraceChannel);
		return;
	}

	// 직격 모드 (기존 로직): 트레이스 막힘 시 직접 맞은 Hero 만, 아니면 고정 지점 근방 Hero
	if (bHit)
	{
		// 벽 등 월드 지오메트리에 막힘 → 플레이어가 피한 것으로 간주
		AHellunaHeroCharacter* HitHero = Cast<AHellunaHeroCharacter>(HitResult.GetActor());
		if (HitHero)
		{
			UGameplayStatics::ApplyDamage(
				HitHero, Damage, nullptr, this, UDamageType::StaticClass());
		}
		return;
	}

	// 막히지 않고 LockedFireTarget 까지 도달 → 고정 지점 근방 플레이어 체크
	for (const TWeakObjectPtr<AHellunaHeroCharacter>& Weak : PlayersInRange)
	{
		if (!Weak.IsValid())
		{
			continue;
		}
		AHellunaHeroCharacter* Hero = Weak.Get();
		if (!IsValid(Hero) || Hero->IsActorBeingDestroyed())
		{
			continue;
		}
		const FVector HeroAim = GetAimPointFor(Hero);
		if (FVector::Dist(HeroAim, LockedFireTarget) <= HitTolerance)
		{
			UGameplayStatics::ApplyDamage(
				Hero, Damage, nullptr, this, UDamageType::StaticClass());
		}
	}
}

// =========================================================
// 복제 콜백
// =========================================================

void AHellunaGuardianTurret::OnRep_CurrentTarget()
{
	// 클라이언트에서 타겟 바뀌면 헤드 회전은 Tick 이 자연스럽게 처리
}

void AHellunaGuardianTurret::OnRep_CurrentState()
{
	// 클라에서 조준 빔 동기화
	ApplyAimBeamForState(CurrentState);
}

// =========================================================
// 조준 빔 제어
// =========================================================

void AHellunaGuardianTurret::StartAimBeam()
{
	if (ActiveAimBeam || !AimBeamFX || !MuzzlePoint)
	{
		return;
	}

	ActiveAimBeam = UNiagaraFunctionLibrary::SpawnSystemAttached(
		AimBeamFX,
		MuzzlePoint,
		NAME_None,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset,
		/*bAutoDestroy=*/false);

	if (ActiveAimBeam)
	{
		ActiveAimBeam->SetRelativeScale3D(AimBeamScale);
		// 일부 Niagara 시스템은 bAutoActivate 기본값에도 자동 활성화 되지 않음 → 명시적 활성화 필수
		ActiveAimBeam->Activate(true);
	}
}

void AHellunaGuardianTurret::StopAimBeam()
{
	if (!ActiveAimBeam)
	{
		return;
	}

	ActiveAimBeam->Deactivate();
	ActiveAimBeam->DestroyComponent();
	ActiveAimBeam = nullptr;
}

void AHellunaGuardianTurret::UpdateAimBeamEndpoint(const FVector& Endpoint)
{
	if (!ActiveAimBeam || AimBeamEndParamName.IsNone())
	{
		return;
	}
	ActiveAimBeam->SetVectorParameter(AimBeamEndParamName, Endpoint);
}

void AHellunaGuardianTurret::ApplyAimBeamForState(EGuardianState State)
{
	const bool bShouldShow = (State == EGuardianState::Lock || State == EGuardianState::FireDelay);
	if (bShouldShow)
	{
		StartAimBeam();
	}
	else
	{
		StopAimBeam();
	}
}

// =========================================================
// 멀티캐스트 RPC
// =========================================================

void AHellunaGuardianTurret::Multicast_PlayFireFX_Implementation(
	FVector_NetQuantize Muzzle, bool bHit, FVector_NetQuantize HitLocation, FVector_NetQuantizeNormal HitNormal)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector MuzzleVec = FVector(Muzzle);
	const FVector ImpactVec = FVector(HitLocation);
	const FVector NormalVec = FVector(HitNormal);

	// 투사체 모드: 머즐 빔/폭발 VFX 는 투사체가 담당 → 발사 사운드만 재생
	const bool bProjectileMode = (ProjectileClass != nullptr);
	if (bProjectileMode)
	{
		if (FireSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, FireSound, MuzzleVec);
		}
		return;
	}

	if (FireBeamFX)
	{
		const FVector Delta = ImpactVec - MuzzleVec;
		const FRotator BeamRot = Delta.Rotation();
		UNiagaraComponent* SpawnedFire = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, FireBeamFX, MuzzleVec, BeamRot, FireBeamScale,
			true, true, ENCPoolMethod::AutoRelease);
		if (SpawnedFire)
		{
			SpawnedFire->Activate(true);
		}
	}

	if (FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, MuzzleVec);
	}

	if (!bHit)
	{
		return;
	}

	if (ImpactFX)
	{
		// Normal 벡터를 X축(Niagara 기본 정면)으로 삼는 회전 — 벽면에 수직으로 튀어나오는 폭발
		const FRotator ImpactRot = NormalVec.IsNearlyZero()
			? FRotator::ZeroRotator
			: NormalVec.Rotation();
		UNiagaraComponent* SpawnedImpact = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, ImpactFX, ImpactVec, ImpactRot, ImpactFXScale,
			true, true, ENCPoolMethod::AutoRelease);
		if (SpawnedImpact)
		{
			SpawnedImpact->Activate(true);
			if (!ImpactFXRadiusParamName.IsNone() && ExplosionRadius > 0.f)
			{
				SpawnedImpact->SetFloatParameter(ImpactFXRadiusParamName, ExplosionRadius);
			}
		}
	}
	if (ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, ImpactVec);
	}
}

void AHellunaGuardianTurret::Multicast_OnStateChanged_Implementation(EGuardianState NewState)
{
	// BP 오버라이드/BGM 큐 바인딩 지점. C++ 기본 구현은 로그만.
	UE_LOG(LogHellunaGuardian, Verbose, TEXT("[%s] Multicast state changed to %d"),
		*GetName(), static_cast<int32>(NewState));
}

// =========================================================
// 체력·파괴
// =========================================================

void AHellunaGuardianTurret::OnGuardianDeath(AActor* /*DeadActor*/, AActor* /*KillerActor*/)
{
	if (!HasAuthority())
	{
		return;
	}

	SetState(EGuardianState::Dead);
	CurrentTarget = nullptr;
	PlayersInRange.Empty();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PhasePollTimerHandle);
	}

	// TODO(dropTable): 드랍 테이블 결정 시 이곳에서 스폰
}

// =========================================================
// Day/Night 폴링
// =========================================================

void AHellunaGuardianTurret::PollDefensePhase()
{
	if (!HasAuthority() || CurrentState == EGuardianState::Dead)
	{
		return;
	}

	if (!CachedGameState.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			if (AGameStateBase* GS = World->GetGameState())
			{
				CachedGameState = Cast<AHellunaDefenseGameState>(GS);
			}
		}
	}

	if (!CachedGameState.IsValid())
	{
		return;
	}

	const EDefensePhase NewPhase = CachedGameState->GetPhase();
	if (NewPhase != CachedPhase)
	{
		CachedPhase = NewPhase;
		HandlePhaseChanged(NewPhase);
	}
}

void AHellunaGuardianTurret::HandlePhaseChanged(EDefensePhase NewPhase)
{
	bIsDay = (NewPhase == EDefensePhase::Day);

	if (!bIsDay && CurrentState != EGuardianState::Dead)
	{
		SetState(EGuardianState::Idle);
		CurrentTarget = nullptr;
		LockedFireTarget = FVector::ZeroVector;
	}
}
