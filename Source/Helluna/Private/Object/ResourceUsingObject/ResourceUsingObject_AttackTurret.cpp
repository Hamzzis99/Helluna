// File: Source/Helluna/Private/Object/ResourceUsingObject/ResourceUsingObject_AttackTurret.cpp

#include "Object/ResourceUsingObject/ResourceUsingObject_AttackTurret.h"

#include "Character/HellunaEnemyCharacter.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "GameMode/HellunaDefenseGameMode.h" // [BossCinematicFreezeV1] 시네마틱 중 포탑 정지 쿼리
#include "Kismet/KismetSystemLibrary.h"       // [InitialOverlapSeedV1] SphereOverlapActors
#include "Components/SphereComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Sound/SoundAttenuation.h"
#include "UObject/ConstructorHelpers.h"


// =========================================================
// 생성자
// =========================================================

AResourceUsingObject_AttackTurret::AResourceUsingObject_AttackTurret()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f; // 매 프레임 (회전 보간)

	bReplicates = true;
	bAlwaysRelevant = true;

	// [TurretSoundV1] 발사 사운드 기본 거리 감쇠 — 없으면 거리 무관 풀볼륨으로 들림. BP 에서 교체 가능.
	static ConstructorHelpers::FObjectFinder<USoundAttenuation> AttenFinder(
		TEXT("/Game/Migration/VFX/ProjectileHitVFX/_GenericSource/SFX/GenericSoundAttenuation.GenericSoundAttenuation"));
	if (AttenFinder.Succeeded())
	{
		FireSoundAttenuation = AttenFinder.Object;
	}

	// ── 고정 루트 (회전하지 않음) ────────────────────────────
	TurretRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TurretRoot"));
	SetRootComponent(TurretRoot);

	// ── [파트1] 베이스 메쉬 (고정) — Base 클래스의 DynamicMeshComponent 재활용 ──
	if (DynamicMeshComponent)
	{
		DynamicMeshComponent->SetupAttachment(TurretRoot);
	}

	// ── [파트2] 몸체 메쉬 (고정) ─────────────────────────────
	MeshBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshBody"));
	MeshBody->SetupAttachment(TurretRoot);

	// ── [파트3] 헤드 회전 피벗 + 헤드 메쉬 (적 추적 회전) ────
	TurretHead = CreateDefaultSubobject<USceneComponent>(TEXT("TurretHead"));
	TurretHead->SetupAttachment(TurretRoot);

	MeshHead = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshHead"));
	MeshHead->SetupAttachment(TurretHead);

	// ── 총구 위치 (헤드 메쉬 하위 — 함께 회전) ──────────────
	MuzzlePoint = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzlePoint"));
	MuzzlePoint->SetupAttachment(MeshHead);
	MuzzlePoint->SetRelativeLocation(FVector(100.f, 0.f, 0.f));

	// ── 탐지 구체 (고정 루트 하위 — 회전과 무관) ─────────────
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

	// [DetectionRangeV1] 탐지 반경을 DetectionRadius 단일 소스로 적용 (BP 컴포넌트 템플릿 값 덮어씀).
	//   BP DetectionSphere 반경이 10m 로 너무 작아 보스가 아레나에서 범위 안에 들어오지 못했던 문제 해결.
	if (DetectionSphere)
	{
		DetectionSphere->SetSphereRadius(DetectionRadius);
	}

	// [InitialOverlapSeedV1 + DetectionRangeV1] 범위 내 적을 주기적으로 직접 스캔해 등록.
	//   Why: 적 등록이 OnDetectionBeginOverlap(범위 "진입" 이벤트)에만 의존하면 두 경우를 놓친다.
	//        ① 포탑 설치 시점에 이미 범위 안에 서 있던 적(보스 등) — 경계를 새로 넘지 않음.
	//        ② 범위 안에서 스폰되거나 콜리전이 잠시 꺼졌다 켜진 적(보스 등장 연출) — 경계 통과 없음.
	//   How: 0.15s 후부터 1초 간격으로 SphereOverlap 질의해 EnemiesInRange 시드 + 무타겟 시 재선택.
	//        포탑은 소수라 1초당 구체 질의 1회는 비용 무시 가능.
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(
			RescanTimerHandle, this, &AResourceUsingObject_AttackTurret::SeedEnemiesAlreadyInRange,
			1.0f, /*bLoop=*/true, /*FirstDelay=*/0.15f);
	}
}

// =========================================================
// EndPlay — 델리게이트 정리
// =========================================================

void AResourceUsingObject_AttackTurret::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(PostAttackPauseTimerHandle);
	GetWorldTimerManager().ClearTimer(RescanTimerHandle); // [DetectionRangeV1]

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
// [TurretHP] 사망 시 서버 로직 정지 (베이스 HandleTurretDeath 에서 호출)
// =========================================================

void AResourceUsingObject_AttackTurret::OnTurretDestroyed_StopServerLogic()
{
	GetWorldTimerManager().ClearTimer(PostAttackPauseTimerHandle);
	GetWorldTimerManager().ClearTimer(RescanTimerHandle);

	if (AHellunaEnemyCharacter* OldTarget = Cast<AHellunaEnemyCharacter>(CurrentTarget.Get()))
	{
		UnbindTargetDeathDelegate(OldTarget);
	}
	CurrentTarget = nullptr;
	EnemiesInRange.Empty();
}

// =========================================================
// Tick — 회전 + 조건부 공격
// =========================================================

void AResourceUsingObject_AttackTurret::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// [TurretHP] 파괴된 포탑은 모든 공격/회전 로직 정지 (Tick 비활성화의 방어적 가드)
	if (IsTurretDestroyed())
	{
		return;
	}

	// [BossCinematicFreezeV1] 보스 시네마틱(소환/페이즈2/사망) 중에는 포탑을 완전히 정지시킨다.
	//   Why: 보스가 등장 시네마틱 도중 포탑 사격에 사살되는 버그가 있었음. 데미지는 서버 권한이므로
	//        서버에서 발사를 막으면 충분하고, 추가로 CurrentTarget(복제됨)을 비워 클라 회전까지 멈춘다.
	//   How: 시네마틱 종료 후 Tick 의 IsTargetValid()→SelectClosestTarget 경로로 자동 재타겟팅.
	if (HasAuthority() && IsBossCinematicActive())
	{
		if (IsValid(CurrentTarget.Get()))
		{
			if (AHellunaEnemyCharacter* OldTarget = Cast<AHellunaEnemyCharacter>(CurrentTarget.Get()))
			{
				UnbindTargetDeathDelegate(OldTarget);
			}
			CurrentTarget = nullptr; // 복제 → 모든 클라이언트에서 헤드 회전도 정지
		}
		return;
	}

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

	// 디버그: 공격 범위 시각화
	if (bShowAttackRange)
	{
		const float Radius = DetectionSphere->GetScaledSphereRadius();
		const FVector Center = DetectionSphere->GetComponentLocation();
		const FColor RangeColor = CurrentTarget.Get() ? FColor::Red : FColor::Green;
		DrawDebugCircle(GetWorld(), Center, Radius, 64,
			RangeColor, false, -1.f, 0, 2.f,
			FVector(1, 0, 0), FVector(0, 1, 0), false);
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

	// 발사 사운드 — [TurretSoundV1] 볼륨 낮춤 + 거리 감쇠 적용(멀리서 풀볼륨으로 들리던 문제).
	//   location 기반 재생(총구 위치)은 그대로 — 감쇠가 없어 거리 falloff 가 안 먹던 게 원인이었음.
	if (FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this, FireSound, MuzzleLocation, FRotator::ZeroRotator,
			FireSoundVolume, FireSoundPitch, 0.f, FireSoundAttenuation);
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

// =========================================================
// [InitialOverlapSeedV1] 설치 시점 이미 범위 내 적 시드 (서버)
//   begin-overlap 이벤트에 의존하지 않고 SphereOverlap 으로 현재 범위 내 적을 직접 질의.
//   보스처럼 포탑 설치 전부터 범위 안에 서 있던 적을 즉시 타겟 후보로 등록한다.
// =========================================================
void AResourceUsingObject_AttackTurret::SeedEnemiesAlreadyInRange()
{
	if (!HasAuthority() || !DetectionSphere)
	{
		return;
	}

	const FVector Center = DetectionSphere->GetComponentLocation();
	const float Radius = DetectionSphere->GetScaledSphereRadius();

	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	TArray<AActor*> IgnoreActors;
	IgnoreActors.Add(this);

	TArray<AActor*> FoundActors;
	UKismetSystemLibrary::SphereOverlapActors(
		this, Center, Radius, ObjectTypes,
		AHellunaEnemyCharacter::StaticClass(), IgnoreActors, FoundActors);

	int32 NewlySeeded = 0; // 이번 스캔에서 "새로" 추가된 적만 카운트 (주기 재스캔 로그 스팸 방지)
	for (AActor* Found : FoundActors)
	{
		AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Found);
		if (!Enemy || Enemy->IsActorBeingDestroyed())
		{
			continue;
		}

		const UHellunaHealthComponent* HealthComp = Enemy->FindComponentByClass<UHellunaHealthComponent>();
		if (HealthComp && HealthComp->IsDead())
		{
			continue;
		}

		if (!EnemiesInRange.Contains(Enemy))
		{
			EnemiesInRange.Add(Enemy);
			++NewlySeeded;
		}
	}

	if (NewlySeeded > 0)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[DetectionRangeV1] %s — 범위 재스캔으로 적 %d 마리 신규 등록 (반경 %.0fcm)"),
			*GetName(), NewlySeeded, DetectionRadius);
	}

	// 시드 직후 타겟이 없으면 즉시 가장 가까운 적 선택
	if (!IsTargetValid())
	{
		SelectClosestTarget();
	}
}

bool AResourceUsingObject_AttackTurret::IsBossCinematicActive() const
{
	if (const AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		return GM->IsAnyBossCinematicActive();
	}
	return false;
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
	if (!IsValid(Target) || !TurretHead)
	{
		return false;
	}

	const FVector Direction = (Target->GetActorLocation() - TurretHead->GetComponentLocation()).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return true;
	}

	// TurretHead의 Forward 벡터 기준으로 판정
	const FVector Forward = TurretHead->GetForwardVector();
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
	if (bRotationPaused || !TurretHead)
	{
		return;
	}

	AActor* Target = CurrentTarget.Get();
	if (!IsValid(Target) || Target->IsActorBeingDestroyed())
	{
		return;
	}

	// 헤드 피벗의 월드 위치 기준으로 방향 계산
	const FVector HeadLocation = TurretHead->GetComponentLocation();
	const FVector TargetLocation = Target->GetActorLocation();
	const FVector Direction = (TargetLocation - HeadLocation).GetSafeNormal();

	if (Direction.IsNearlyZero())
	{
		return;
	}

	// TurretHead만 회전 (Yaw만, 베이스/몸체는 고정)
	const FRotator DesiredRotation = FRotator(0.f, Direction.Rotation().Yaw, 0.f);
	const FRotator CurrentRotation = TurretHead->GetComponentRotation();

	const FRotator NewRotation = FMath::RInterpConstantTo(CurrentRotation, DesiredRotation, DeltaTime, TurretRotationSpeed);
	TurretHead->SetWorldRotation(NewRotation);
}

void AResourceUsingObject_AttackTurret::OnPostAttackPauseEnd()
{
	bRotationPaused = false;
}
