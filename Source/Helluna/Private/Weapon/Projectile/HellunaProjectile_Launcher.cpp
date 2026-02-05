// Capstone Project Helluna


#include "Weapon/Projectile/HellunaProjectile_Launcher.h"

#include "Components/BoxComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

#include "DebugHelper.h"
#include "DrawDebugHelpers.h"

AHellunaProjectile_Launcher::AHellunaProjectile_Launcher()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicateMovement(true);

	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	RootComponent = CollisionBox;

	// ✅ QueryOnly + Overlap
	CollisionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionBox->SetGenerateOverlapEvents(true);
	CollisionBox->SetCollisionObjectType(ECC_WorldDynamic);

	// 기본: 필요한 채널만 오버랩(프로젝트에서 필요 시 조정)
	CollisionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionBox->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	CollisionBox->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	CollisionBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	CollisionBox->OnComponentBeginOverlap.AddDynamic(this, &AHellunaProjectile_Launcher::OnBeginOverlap);


	// ✅ 중력 영향 제거했음, 로켓/에너지탄 계열로 직진형 궤도를 의도했음
	MoveComp = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("MoveComp"));
	MoveComp->bRotationFollowsVelocity = true;
	MoveComp->bShouldBounce = false;
	MoveComp->ProjectileGravityScale = 0.f;

	TrailFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("TrailFX"));
	TrailFX->SetupAttachment(RootComponent);
	TrailFX->bAutoActivate = true;
}

void AHellunaProjectile_Launcher::BeginPlay()
{
	Super::BeginPlay();

	// 박스 크기 적용
	if (CollisionBox)
	{
		CollisionBox->SetBoxExtent(OverlapBoxExtent);
	}

	// 발사자/인스티게이터 무시 - 스폰 직후 겹침으로 바로 폭발하는 상황 방지했음
	if (AActor* OwnerActor = GetOwner())
	{
		CollisionBox->IgnoreActorWhenMoving(OwnerActor, true);
	}
	if (APawn* InstigatorPawn = GetInstigator())
	{
		CollisionBox->IgnoreActorWhenMoving(InstigatorPawn, true);
	}
}

void AHellunaProjectile_Launcher::InitProjectile(
	float InDamage,
	float InRadius,
	const FVector& InVelocity,
	float InLifeSeconds
)
{
	// ✅ 데미지/반경은 무기에서만 세팅 -> 총알은 주입값만 사용
	Damage = InDamage;
	Radius = InRadius;

	if (MoveComp)
	{
		const float Speed = InVelocity.Size();
		MoveComp->Velocity = InVelocity;
		MoveComp->InitialSpeed = Speed;
		MoveComp->MaxSpeed = Speed;
	}

	// ✅ 최대거리 도달(수명 만료) 시 폭발
	SetLifeSpan(FMath::Max(InLifeSeconds, 0.01f));
}

void AHellunaProjectile_Launcher::OnBeginOverlap(
	UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult
)
{
	// ✅ 서버에서만 판정/데미지/파괴
	if (!HasAuthority())
		return;

	if (bExploded)
		return;

	if (!OtherActor || OtherActor == this || OtherActor == GetOwner() || OtherActor == GetInstigator())
		return;

	FVector HitLoc = GetActorLocation();
	if (bFromSweep)
	{
		HitLoc = SweepResult.ImpactPoint;
	}
	// - 폭발 지점을 표면 법선 방향으로 살짝 띄워 ApplyRadialDamage를 바닥이 가리는 문제 완화했음
	HitLoc += SweepResult.ImpactNormal * 10.f;

	Explode(HitLoc);
}

void AHellunaProjectile_Launcher::LifeSpanExpired()
{
	

	// ✅ 사거리 끝 도달 = 폭발
	if (HasAuthority() && !bExploded)
	{
		Explode(GetActorLocation());
		return;
	}

	Super::LifeSpanExpired();
}

void AHellunaProjectile_Launcher::Explode(const FVector& ExplosionLocation)
{
	if (!HasAuthority() || bExploded)
		return;

	bExploded = true;


	// ✅ 폭발 데미지는 서버에서만 처리했음
	TArray<AActor*> Ignore;
	Ignore.Add(this);
	if (AActor* OwnerActor = GetOwner()) Ignore.Add(OwnerActor);
	if (APawn* InstigatorPawn = GetInstigator()) Ignore.Add(InstigatorPawn);

	UGameplayStatics::ApplyRadialDamage(
		this,
		Damage,
		ExplosionLocation,
		Radius,
		UDamageType::StaticClass(),
		Ignore,
		this,
		GetInstigatorController(),
		true
	);

	//디버그 용 구체
	if (bDebugDrawRadialDamage)
	{
		if (UWorld* World = GetWorld())
		{
			constexpr int32 Segments = 24;
			constexpr float LifeTime = 1.0f;
			constexpr float Thickness = 1.5f;

			DrawDebugSphere(
				World,
				ExplosionLocation,
				Radius,
				Segments,
				FColor::Red,
				false,
				LifeTime,
				0,
				Thickness
			);

			DrawDebugPoint(World, ExplosionLocation, 12.f, FColor::Yellow, false, LifeTime);
		}
	}

	// FX는 모두에게
	Multicast_SpawnExplosionFX(FVector_NetQuantize(ExplosionLocation));


	// - 즉시 Destroy 시 RPC 누락 가능하여, 아래에서 Hide + 짧은 LifeSpan 방식 사용했음
	if (CollisionBox)
		CollisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (MoveComp)
		MoveComp->StopMovementImmediately();

	SetActorHiddenInGame(true);

	// 0.1~0.25 사이 추천 (너 프로젝트에선 0.2가 무난)
	SetLifeSpan(0.2f);
}

void AHellunaProjectile_Launcher::Multicast_SpawnExplosionFX_Implementation(FVector_NetQuantize ExplosionLocation)
{

	if (TrailFX)
	{
		TrailFX->Deactivate();
	}

	if (ExplosionFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this,
			ExplosionFX,
			(FVector)ExplosionLocation,
			FRotator::ZeroRotator,
			ExplosionFXScale,
			true,
			true,
			ENCPoolMethod::AutoRelease
		);
	}
}