// File: Source/Helluna/Private/Object/ResourceUsingObject/ResourceUsingObject_AttackTurret.cpp

#include "Object/ResourceUsingObject/ResourceUsingObject_AttackTurret.h"

#include "Character/HellunaEnemyCharacter.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Components/SphereComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Components/DynamicMeshComponent.h"


// =========================================================
// 생성자
// =========================================================

AResourceUsingObject_AttackTurret::AResourceUsingObject_AttackTurret()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f; // 매 프레임 (회전 보간)

	bReplicates = true;
	bAlwaysRelevant = true;

	// 터렛 루트 — 회전 기준점 (DynamicMesh 위에 빈 SceneComponent)
	TurretRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TurretRoot"));
	SetRootComponent(TurretRoot);

	// DynamicMeshComponent를 TurretRoot 하위로 재배치
	// → BP에서 DynamicMeshComponent의 Relative Rotation으로 메쉬 방향 조절 가능
	if (DynamicMeshComponent)
	{
		DynamicMeshComponent->SetupAttachment(TurretRoot);
	}

	// 총구 위치 (DynamicMesh 하위)
	MuzzlePoint = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzlePoint"));
	if (DynamicMeshComponent)
	{
		MuzzlePoint->SetupAttachment(DynamicMeshComponent);
	}
	MuzzlePoint->SetRelativeLocation(FVector(100.f, 0.f, 0.f));

	// 탐지 구체 (TurretRoot 하위 — 메쉬 회전과 무관하게 위치 유지)
	DetectionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("DetectionSphere"));
	DetectionSphere->SetupAttachment(TurretRoot);
	DetectionSphere->SetSphereRadius(1500.f);
	DetectionSphere->SetCollisionProfileName(TEXT("Trigger"));
	DetectionSphere->SetGenerateOverlapEvents(true);
	DetectionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	DetectionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	DetectionSphere->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::OnDetectionBeginOverlap);
	DetectionSphere->OnComponentEndOverlap.AddUniqueDynamic(this, &ThisClass::OnDetectionEndOverlap);
}

// =========================================================
// BeginPlay
// =========================================================

void AResourceUsingObject_AttackTurret::BeginPlay()
{
	Super::BeginPlay();

	// 공격은 Tick에서 조건 판단 후 실행 — 별도 타이머 불필요
	TimeSinceLastAttack = AttackInterval; // 시작 직후 바로 발사 가능
}

// =========================================================
// EndPlay — 델리게이트 정리
// =========================================================

void AResourceUsingObject_AttackTurret::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(PostAttackPauseTimerHandle);

	// 모든 적의 HealthComponent에 바인딩된 사망 델리게이트 해제
	AHellunaEnemyCharacter* OldTarget = Cast<AHellunaEnemyCharacter>(CurrentTarget.Get());
	if (OldTarget)
	{
		UnbindTargetDeathDelegate(OldTarget);
	}
	CurrentTarget = nullptr;
	EnemiesInRange.Empty();

	Super::EndPlay(EndPlayReason);
}

// =========================================================
// Tick — 회전 + 조건부 공격
// =========================================================

void AResourceUsingObject_AttackTurret::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 쿨다운 누적 (정지 중에도 시간은 흐른다)
	TimeSinceLastAttack += DeltaTime;

	// 회전 보간 (정지 중이면 스킵)
	UpdateTurretRotation(DeltaTime);

	// 서버에서만 공격 판정
	if (!HasAuthority())
	{
		return;
	}

	// 쿨다운 미완료
	if (TimeSinceLastAttack < AttackInterval)
	{
		return;
	}

	// 정지 중이면 아직 발사 불가 (정지 해제 후 회전부터)
	if (bRotationPaused)
	{
		return;
	}

	// 타겟 유효성
	if (!IsTargetValid())
	{
		SelectClosestTarget();
		if (!IsTargetValid())
		{
			return;
		}
	}

	// 타겟을 바라보고 있을 때만 발사
	if (IsFacingTarget())
	{
		PerformAttack();
	}
}

// =========================================================
// 리플리케이션
// =========================================================

void AResourceUsingObject_AttackTurret::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AResourceUsingObject_AttackTurret, CurrentTarget);
}

void AResourceUsingObject_AttackTurret::OnRep_CurrentTarget()
{
	// 클라이언트에서 타겟 변경 시 추가 처리가 필요하면 여기에
}

// =========================================================
// 탐지 오버랩 콜백 (서버)
// =========================================================

void AResourceUsingObject_AttackTurret::OnDetectionBeginOverlap(
	UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority())
	{
		return;
	}

	AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(OtherActor);
	if (!Enemy || Enemy->IsActorBeingDestroyed())
	{
		return;
	}

	// 이미 사망한 적은 무시
	UHellunaHealthComponent* HealthComp = Enemy->FindComponentByClass<UHellunaHealthComponent>();
	if (HealthComp && HealthComp->IsDead())
	{
		return;
	}

	EnemiesInRange.AddUnique(Enemy);

	// 타겟이 없으면 즉시 할당
	if (!IsTargetValid())
	{
		SelectClosestTarget();
	}
}

void AResourceUsingObject_AttackTurret::OnDetectionEndOverlap(
	UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!HasAuthority())
	{
		return;
	}

	AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(OtherActor);
	if (!Enemy)
	{
		return;
	}

	EnemiesInRange.Remove(Enemy);

	// 파괴 중이거나 무효한 액터의 컴포넌트에 접근하면 bRegistered assertion 발생 — 조기 반환
	// IsValid()만으로는 부족: Destroy() 과정에서 MarkAsGarbage 전에 EndOverlap이 발생하므로
	// IsActorBeingDestroyed()로 파괴 진행 중인 액터도 차단해야 함
	if (!IsValid(Enemy) || Enemy->IsActorBeingDestroyed())
	{
		if (CurrentTarget.Get() == OtherActor)
		{
			CurrentTarget = nullptr;
			SelectClosestTarget();
		}
		return;
	}

	// 현재 타겟이 범위를 벗어났으면 새 타겟 선택
	if (CurrentTarget.Get() == Enemy)
	{
		UnbindTargetDeathDelegate(Enemy);
		CurrentTarget = nullptr;
		SelectClosestTarget();
	}
}

// =========================================================
// 공격 로직 (서버)
// =========================================================

void AResourceUsingObject_AttackTurret::PerformAttack()
{
	AActor* Target = CurrentTarget.Get();
	if (!IsValid(Target) || Target->IsActorBeingDestroyed())
	{
		CurrentTarget = nullptr;
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 쿨다운 리셋
	TimeSinceLastAttack = 0.f;

	// 라인트레이스: 총구 → 타겟 위치
	const FVector TraceStart = MuzzlePoint ? MuzzlePoint->GetComponentLocation() : GetActorLocation();
	const FVector TraceEnd = Target->GetActorLocation();

	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	const bool bHit = World->LineTraceSingleByChannel(
		HitResult,
		TraceStart,
		TraceEnd,
		TraceChannel,
		QueryParams
	);

	// 발사 FX·사운드 — 히트 여부와 무관하게 총구에서 재생
	const FVector ImpactLocation = bHit ? HitResult.ImpactPoint : TraceEnd;
	Multicast_PlayFireFX(TraceStart, bHit, ImpactLocation);

	if (bHit && HitResult.GetActor())
	{
		AHellunaEnemyCharacter* HitEnemy = Cast<AHellunaEnemyCharacter>(HitResult.GetActor());
		if (HitEnemy)
		{
			UGameplayStatics::ApplyDamage(
				HitEnemy,
				AttackDamage,
				nullptr, // 포탑에는 Controller 없음
				this,    // DamageCauser
				UDamageType::StaticClass()
			);
		}
	}
}

// =========================================================
// VFX / 사운드 (멀티캐스트)
// =========================================================

void AResourceUsingObject_AttackTurret::Multicast_PlayFireFX_Implementation(
	FVector_NetQuantize MuzzleLocation, bool bHit, FVector_NetQuantize HitLocation)
{
	// 공격 직후 회전 정지 (서버/클라 공통)
	bRotationPaused = true;
	GetWorldTimerManager().SetTimer(
		PostAttackPauseTimerHandle,
		this,
		&ThisClass::OnPostAttackPauseEnd,
		PostAttackPause,
		false
	);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 총구 이펙트
	if (MuzzleFlashFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, MuzzleFlashFX, MuzzleLocation,
			FRotator::ZeroRotator, MuzzleFXScale,
			true, true, ENCPoolMethod::AutoRelease
		);
	}

	// 발사 사운드
	if (FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, MuzzleLocation);
	}

	if (!bHit)
	{
		return;
	}

	// 피격 이펙트
	if (ImpactFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, ImpactFX, HitLocation,
			FRotator::ZeroRotator, ImpactFXScale,
			true, true, ENCPoolMethod::AutoRelease
		);
	}

	// 피격 사운드
	if (ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, HitLocation);
	}
}

bool AResourceUsingObject_AttackTurret::IsTargetValid() const
{
	AActor* Target = CurrentTarget.Get();
	if (!IsValid(Target) || Target->IsActorBeingDestroyed())
	{
		return false;
	}

	// 사망 체크
	const AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Target);
	if (!Enemy)
	{
		return false;
	}

	const UHellunaHealthComponent* HealthComp = Enemy->FindComponentByClass<UHellunaHealthComponent>();
	if (HealthComp && HealthComp->IsDead())
	{
		return false;
	}

	return true;
}

bool AResourceUsingObject_AttackTurret::IsFacingTarget() const
{
	AActor* Target = CurrentTarget.Get();
	if (!IsValid(Target))
	{
		return false;
	}

	const FVector Direction = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return true;
	}

	const FVector Forward = GetActorForwardVector();
	// 2D (Yaw만) 비교
	const FVector Forward2D = FVector(Forward.X, Forward.Y, 0.f).GetSafeNormal();
	const FVector Dir2D = FVector(Direction.X, Direction.Y, 0.f).GetSafeNormal();

	const float DotProduct = FVector::DotProduct(Forward2D, Dir2D);
	const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(DotProduct, -1.f, 1.f)));

	return AngleDeg <= FireAngleThreshold;
}

void AResourceUsingObject_AttackTurret::SelectClosestTarget()
{
	// 유효하지 않은 적 제거 — 파괴 중인 액터의 컴포넌트 접근 방지
	EnemiesInRange.RemoveAll([](const TWeakObjectPtr<AHellunaEnemyCharacter>& WeakEnemy)
	{
		if (!WeakEnemy.IsValid())
		{
			return true;
		}

		AHellunaEnemyCharacter* Enemy = WeakEnemy.Get();
		if (!IsValid(Enemy) || Enemy->IsActorBeingDestroyed())
		{
			return true;
		}

		const UHellunaHealthComponent* HealthComp = Enemy->FindComponentByClass<UHellunaHealthComponent>();
		if (HealthComp && HealthComp->IsDead())
		{
			return true;
		}

		return false;
	});

	if (EnemiesInRange.IsEmpty())
	{
		CurrentTarget = nullptr;
		return;
	}

	// 가장 가까운 적 탐색
	const FVector TurretLocation = GetActorLocation();
	float ClosestDistSq = MAX_FLT;
	AHellunaEnemyCharacter* ClosestEnemy = nullptr;

	for (const TWeakObjectPtr<AHellunaEnemyCharacter>& WeakEnemy : EnemiesInRange)
	{
		if (!WeakEnemy.IsValid())
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(TurretLocation, WeakEnemy->GetActorLocation());
		if (DistSq < ClosestDistSq)
		{
			ClosestDistSq = DistSq;
			ClosestEnemy = WeakEnemy.Get();
		}
	}

	// 이전 타겟 델리게이트 해제
	AHellunaEnemyCharacter* OldTarget = Cast<AHellunaEnemyCharacter>(CurrentTarget.Get());
	if (OldTarget)
	{
		UnbindTargetDeathDelegate(OldTarget);
	}

	CurrentTarget = ClosestEnemy;

	// 새 타겟 사망 델리게이트 바인딩
	if (ClosestEnemy)
	{
		BindTargetDeathDelegate(ClosestEnemy);
	}
}

// =========================================================
// 타겟 사망 델리게이트
// =========================================================

void AResourceUsingObject_AttackTurret::BindTargetDeathDelegate(AHellunaEnemyCharacter* Enemy)
{
	if (!Enemy)
	{
		return;
	}

	UHellunaHealthComponent* HealthComp = Enemy->FindComponentByClass<UHellunaHealthComponent>();
	if (HealthComp)
	{
		HealthComp->OnDeath.AddUniqueDynamic(this, &ThisClass::OnTargetDeath);
	}
}

void AResourceUsingObject_AttackTurret::UnbindTargetDeathDelegate(AHellunaEnemyCharacter* Enemy)
{
	// 파괴 중인 액터의 컴포넌트는 이미 등록해제되었을 수 있음 — 접근 금지
	if (!IsValid(Enemy) || Enemy->IsActorBeingDestroyed())
	{
		return;
	}

	UHellunaHealthComponent* HealthComp = Enemy->FindComponentByClass<UHellunaHealthComponent>();
	if (HealthComp && HealthComp->IsRegistered())
	{
		HealthComp->OnDeath.RemoveDynamic(this, &ThisClass::OnTargetDeath);
	}
}

void AResourceUsingObject_AttackTurret::OnTargetDeath(AActor* DeadActor, AActor* KillerActor)
{
	if (!HasAuthority())
	{
		return;
	}

	// 사망한 적을 목록에서 제거
	AHellunaEnemyCharacter* DeadEnemy = Cast<AHellunaEnemyCharacter>(DeadActor);
	if (DeadEnemy)
	{
		UnbindTargetDeathDelegate(DeadEnemy);
		EnemiesInRange.Remove(DeadEnemy);
	}

	// 현재 타겟이 사망한 경우 새 타겟 선택
	if (CurrentTarget.Get() == DeadActor || !IsTargetValid())
	{
		CurrentTarget = nullptr;
		SelectClosestTarget();
	}
}

// =========================================================
// 포탑 회전 (서버/클라 공통)
// =========================================================

void AResourceUsingObject_AttackTurret::UpdateTurretRotation(float DeltaTime)
{
	if (bRotationPaused)
	{
		return;
	}

	AActor* Target = CurrentTarget.Get();
	if (!IsValid(Target) || Target->IsActorBeingDestroyed())
	{
		return;
	}

	const FVector TurretLocation = GetActorLocation();
	const FVector TargetLocation = Target->GetActorLocation();
	const FVector Direction = (TargetLocation - TurretLocation).GetSafeNormal();

	if (Direction.IsNearlyZero())
	{
		return;
	}

	const FRotator DesiredRotation = FRotator(0.f, Direction.Rotation().Yaw, 0.f);
	const FRotator CurrentRotation = GetActorRotation();

	const FRotator NewRotation = FMath::RInterpConstantTo(CurrentRotation, DesiredRotation, DeltaTime, TurretRotationSpeed);
	SetActorRotation(NewRotation);
}

void AResourceUsingObject_AttackTurret::OnPostAttackPauseEnd()
{
	bRotationPaused = false;
}
