// Capstone Project Helluna

#include "Weapon/Projectile/HellunaProjectile_Enemy.h"

#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "DrawDebugHelpers.h"

// 에너미의 HitNiagaraEffect / MulticastPlayEffect 호출용
#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaHeroCharacter.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "GameFramework/Pawn.h"

AHellunaProjectile_Enemy::AHellunaProjectile_Enemy()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicateMovement(true);

	// ── 충돌 구체 ────────────────────────────────────────────
	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	RootComponent   = CollisionSphere;

	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetGenerateOverlapEvents(true);
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap); // 우주선 DynamicMesh (WorldStatic)
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);         // 플레이어
	CollisionSphere->OnComponentBeginOverlap.AddDynamic(this, &AHellunaProjectile_Enemy::OnBeginOverlap);

	// ── 이동 컴포넌트 (중력 0 = 직진형) ─────────────────────
	MoveComp = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("MoveComp"));
	MoveComp->bRotationFollowsVelocity = true;
	MoveComp->bShouldBounce            = false;
	MoveComp->ProjectileGravityScale   = 0.f;

	// 비행 트레일 FX
	TrailFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("TrailFX"));
	TrailFX->SetupAttachment(RootComponent);
	TrailFX->bAutoActivate = true;
}

void AHellunaProjectile_Enemy::BeginPlay()
{
	Super::BeginPlay();

	// 구체 반경 적용
	if (CollisionSphere)
	{
		CollisionSphere->SetSphereRadius(CollisionRadius);
	}

	// 발사자/인스티게이터를 무시 목록에 추가 — 스폰 직후 자기 충돌 방지
	if (AActor* OwnerActor = GetOwner())
	{
		CollisionSphere->IgnoreActorWhenMoving(OwnerActor, true);
	}
	if (APawn* InstigatorPawn = GetInstigator())
	{
		CollisionSphere->IgnoreActorWhenMoving(InstigatorPawn, true);
	}
}

// ============================================================
// InitProjectile — GA 에서 스폰 직후 호출
// ============================================================
void AHellunaProjectile_Enemy::InitProjectile(
	float InDamage,
	const FVector& InDirection,
	float InSpeed,
	float InLifeSeconds,
	AHellunaEnemyCharacter* InOwnerEnemy)
{
	Damage      = InDamage;
	OwnerEnemy  = InOwnerEnemy; // 에너미 참조 저장 (TWeakObjectPtr)

	if (MoveComp)
	{
		const FVector Velocity = InDirection.GetSafeNormal() * InSpeed;
		MoveComp->Velocity     = Velocity;
		MoveComp->InitialSpeed = InSpeed;
		MoveComp->MaxSpeed     = InSpeed;
	}

	SetLifeSpan(FMath::Max(InLifeSeconds, 0.01f));
}

// ============================================================
// OnBeginOverlap — 충돌 감지 (서버만 판정)
// ============================================================
void AHellunaProjectile_Enemy::OnBeginOverlap(
	UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (!HasAuthority()) return;
	if (bHit)            return;
	if (!OtherActor || OtherActor == this || OtherActor == GetOwner() || OtherActor == GetInstigator()) return;

	UE_LOG(LogTemp, Warning, TEXT("[EnemyProjectile] Overlap: %s | Comp: %s"),
		*OtherActor->GetName(), *GetNameSafe(OtherComp));

	// [FriendlyFireV1] 플레이어(HeroCharacter) 또는 우주선만 데미지 적용.
	// Why: 기존엔 `APawn` 으로 필터링 → 같은 에너미(Pawn 상속)도 타깃이 되어 몬스터끼리
	//      맞으면 데미지 + 피격 VFX 가 발생하는 프렌들리 파이어 버그가 있었음.
	// How: 에너미/가디언 등 비플레이어 Pawn 은 명시적으로 제외. HeroCharacter 와 우주선만 허용.
	const bool bIsSpaceShip = (Cast<AResourceUsingObject_SpaceShip>(OtherActor) != nullptr);
	const bool bIsHero      = (Cast<AHellunaHeroCharacter>(OtherActor) != nullptr);
	if (!bIsSpaceShip && !bIsHero) return;

	const FVector HitLocation = bFromSweep ? FVector(SweepResult.ImpactPoint) : GetActorLocation();
	HitTarget(OtherActor, HitLocation);
}

// ============================================================
// HitTarget — 데미지 적용 + FX 재생 후 소멸
// ============================================================
void AHellunaProjectile_Enemy::HitTarget(AActor* HitActor, const FVector& HitLocation)
{
	if (!HasAuthority() || bHit) return;
	bHit = true;

	// 단일 직격 데미지
	UGameplayStatics::ApplyDamage(
		HitActor,
		Damage,
		GetInstigatorController(),
		this,
		UDamageType::StaticClass()
	);

	if (bDebugDraw)
	{
		DrawDebugSphere(GetWorld(), HitLocation, CollisionRadius, 12, FColor::Red, false, 1.f, 0, 1.5f);
	}

	// 모든 클라이언트에 충돌 FX 전파
	Multicast_SpawnHitFX(FVector_NetQuantize(HitLocation));

	// 즉시 Destroy 하면 RPC 가 누락될 수 있으므로 숨김 처리 후 짧은 딜레이로 제거
	if (CollisionSphere) CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (MoveComp)        MoveComp->StopMovementImmediately();
	SetActorHiddenInGame(true);
	SetLifeSpan(0.2f);
}

// ============================================================
// LifeSpanExpired — 사거리 초과 시 조용히 소멸
// ============================================================
void AHellunaProjectile_Enemy::LifeSpanExpired()
{
	if (HasAuthority() && !bHit)
	{
		Multicast_SpawnHitFX(FVector_NetQuantize(GetActorLocation()));
	}

	Super::LifeSpanExpired();
}

// ============================================================
// Multicast_SpawnHitFX — 충돌/소멸 FX (모든 클라이언트)
//
// 재생 순서:
//  1. 트레일 FX 중단
//  2. 투사체 자체 HitFX 재생 (설정된 경우)
//  3. 에너미의 HitNiagaraEffect 를 맞은 위치에 재생 (OwnerEnemy 가 유효한 경우)
//     → 근거리/원거리 모두 같은 피격 이펙트를 사용하게 됨
// ============================================================
void AHellunaProjectile_Enemy::Multicast_SpawnHitFX_Implementation(FVector_NetQuantize HitLocation)
{
	// 트레일 FX 중단
	if (TrailFX)
	{
		TrailFX->Deactivate();
	}

	// 투사체 자체 HitFX 재생
	if (HitFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, HitFX, (FVector)HitLocation,
			FRotator::ZeroRotator, HitFXScale,
			true, true, ENCPoolMethod::AutoRelease
		);
	}

	// 에너미의 HitNiagaraEffect 를 맞은 위치에 재생
	// OwnerEnemy 가 살아있을 때만 호출 (TWeakObjectPtr 로 안전하게 접근)
	if (OwnerEnemy.IsValid())
	{
		OwnerEnemy->MulticastPlayEffect(
			(FVector)HitLocation,
			OwnerEnemy->HitNiagaraEffect,
			OwnerEnemy->HitEffectScale,
			false
		);

		// [HitSoundV1] 사운드는 발사한 공격 GA 가 캐싱한 HitSound 사용.
		OwnerEnemy->TryPlayCachedHitSound((FVector)HitLocation);
	}
}
