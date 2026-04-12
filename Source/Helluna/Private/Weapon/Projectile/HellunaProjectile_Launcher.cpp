// Capstone Project Helluna


#include "Weapon/Projectile/HellunaProjectile_Launcher.h"

#include "Components/BoxComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

#include "BossEvent/BossPatternZoneBase.h"
#include "DebugHelper.h"
#include "DrawDebugHelpers.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaHeroCharacter.h"

AHellunaProjectile_Launcher::AHellunaProjectile_Launcher()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicateMovement(true);

	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	RootComponent = CollisionBox;

	CollisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CollisionBox->SetGenerateOverlapEvents(false);
	CollisionBox->SetCollisionObjectType(ECC_WorldDynamic);

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
	MoveComp->bInitialVelocityInLocalSpace = false;

	TrailFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("TrailFX"));
	TrailFX->SetupAttachment(RootComponent);
	TrailFX->bAutoActivate = true;
}

void AHellunaProjectile_Launcher::SetProjectileCollisionEnabled(bool bEnabled)
{
	if (!CollisionBox)
		return;

	CollisionBox->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	CollisionBox->SetGenerateOverlapEvents(bEnabled);
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

	SetProjectileCollisionEnabled(bInitialized);

	UE_LOG(LogTemp, Warning,
		TEXT("[LauncherFlight][BeginPlay] Projectile=%s Loc=%s Rot=%s Forward=%s Velocity=%s TrailWorldRot=%s TrailRelativeRot=%s Initialized=%s"),
		*GetNameSafe(this),
		*GetActorLocation().ToCompactString(),
		*GetActorRotation().ToCompactString(),
		*GetActorForwardVector().ToCompactString(),
		MoveComp ? *MoveComp->Velocity.ToCompactString() : TEXT("None"),
		TrailFX ? *TrailFX->GetComponentRotation().ToCompactString() : TEXT("None"),
		TrailFX ? *TrailFX->GetRelativeRotation().ToCompactString() : TEXT("None"),
		bInitialized ? TEXT("Y") : TEXT("N"));
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
	bInitialized = true;

	if (MoveComp)
	{
		const float Speed = InVelocity.Size();
		MoveComp->Velocity = InVelocity;
		MoveComp->InitialSpeed = Speed;
		MoveComp->MaxSpeed = Speed;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[LauncherFlight][Init] Projectile=%s Loc=%s Rot=%s Forward=%s InVelocity=%s Speed=%.1f TrailWorldRot=%s TrailRelativeRot=%s"),
		*GetNameSafe(this),
		*GetActorLocation().ToCompactString(),
		*GetActorRotation().ToCompactString(),
		*GetActorForwardVector().ToCompactString(),
		*InVelocity.ToCompactString(),
		InVelocity.Size(),
		TrailFX ? *TrailFX->GetComponentRotation().ToCompactString() : TEXT("None"),
		TrailFX ? *TrailFX->GetRelativeRotation().ToCompactString() : TEXT("None"));

	if (CollisionBox)
	{
		CollisionBox->SetBoxExtent(OverlapBoxExtent);
		if (AActor* OwnerActor = GetOwner())
		{
			CollisionBox->IgnoreActorWhenMoving(OwnerActor, true);
		}
		if (APawn* InstigatorPawn = GetInstigator())
		{
			CollisionBox->IgnoreActorWhenMoving(InstigatorPawn, true);
		}
	}

	SetProjectileCollisionEnabled(true);

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
	if (!HasAuthority())
		return;

	if (!bInitialized)
		return;

	if (bExploded)
		return;

	if (!OtherActor || OtherActor == this || OtherActor == GetOwner() || OtherActor == GetInstigator())
		return;

	// SlowZone(BossPatternZoneBase) 계열은 폭발하지 않고 통과
	if (OtherActor->IsA<ABossPatternZoneBase>())
		return;

	FVector HitLoc = GetActorLocation();
	if (bFromSweep)
	{
		HitLoc = SweepResult.ImpactPoint;
	}
	HitLoc += SweepResult.ImpactNormal * 10.f;

	UE_LOG(LogTemp, Warning,
		TEXT("[ORBHIT] LauncherOverlap: Projectile=%s Other=%s OtherClass=%s Comp=%s HitLoc=%s"),
		*GetNameSafe(this),
		*GetNameSafe(OtherActor),
		*OtherActor->GetClass()->GetName(),
		*GetNameSafe(OtherComp),
		*HitLoc.ToString());

	Explode(HitLoc);
}

void AHellunaProjectile_Launcher::LifeSpanExpired()
{
	

	// ✅ 사거리 끝 도달 = 폭발
	if (HasAuthority() && bInitialized && !bExploded)
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
	SetProjectileCollisionEnabled(false);
	UE_LOG(LogTemp, Warning,
		TEXT("[DamageDiag][LauncherExplode] Projectile=%s Location=%s Radius=%.1f Damage=%.1f Owner=%s Instigator=%s Velocity=%s Rot=%s Forward=%s TrailWorldRot=%s"),
		*GetNameSafe(this),
		*ExplosionLocation.ToString(),
		Radius,
		Damage,
		*GetNameSafe(GetOwner()),
		*GetNameSafe(GetInstigator()),
		MoveComp ? *MoveComp->Velocity.ToCompactString() : TEXT("None"),
		*GetActorRotation().ToCompactString(),
		*GetActorForwardVector().ToCompactString(),
		TrailFX ? *TrailFX->GetComponentRotation().ToCompactString() : TEXT("None"));


	// ✅ 폭발 데미지는 서버에서만 처리했음
	TArray<AActor*> Ignore;
	Ignore.Add(this);
	if (AActor* OwnerActor = GetOwner()) Ignore.Add(OwnerActor);
	if (APawn* InstigatorPawn = GetInstigator()) Ignore.Add(InstigatorPawn);

	// 폭발 범위 내 Pawn 수집 후, WorldStatic에 막히지 않은 대상에게만 데미지
	if (UWorld* World = GetWorld())
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActors(Ignore);

		// Pawn 채널만 수집 — 몬스터/플레이어만 대상
		World->OverlapMultiByObjectType(
			Overlaps,
			ExplosionLocation,
			FQuat::Identity,
			FCollisionObjectQueryParams(ECC_Pawn),
			FCollisionShape::MakeSphere(Radius),
			QueryParams
		);
		UE_LOG(LogTemp, Warning,
			TEXT("[ORBHIT] ExplosionOverlap: Projectile=%s Count=%d Radius=%.1f"),
			*GetNameSafe(this),
			Overlaps.Num(),
			Radius);
		for (const FOverlapResult& OL : Overlaps)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[ORBHIT]   Found: %s (Class=%s)"),
				*GetNameSafe(OL.GetActor()),
				OL.GetActor() ? *OL.GetActor()->GetClass()->GetName() : TEXT("NULL"));
		}

		TSet<AActor*> DamagedActors;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* Victim = Overlap.OverlapObjectHandle.FetchActor();
			if (!Victim)
			{
				UE_LOG(LogTemp, Warning, TEXT("[DamageDiag][LauncherSkip] Reason=NullVictim"));
				continue;
			}
			if (DamagedActors.Contains(Victim))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[DamageDiag][LauncherSkip] Victim=%s Reason=AlreadyProcessed"),
					*GetNameSafe(Victim));
				continue;
			}
			if (!Victim->CanBeDamaged())
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[DamageDiag][LauncherSkip] Victim=%s Reason=CanBeDamagedFalse"),
					*GetNameSafe(Victim));
				continue;
			}

			// 아군(플레이어)은 데미지 제외 — 적, Orb 등 나머지 대상에게만 적용
			if (Cast<AHellunaHeroCharacter>(Victim))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[DamageDiag][LauncherSkip] Victim=%s Reason=FriendlyPlayer"),
					*GetNameSafe(Victim));
				continue;
			}

			// bIgnoreWorldStatic=false일 때만 WorldStatic 차단 체크
			if (!bIgnoreWorldStatic)
			{
				FHitResult Hit;
				FCollisionQueryParams LineParams;
				LineParams.AddIgnoredActors(Ignore);
				LineParams.AddIgnoredActor(Victim);

				const bool bBlocked = World->LineTraceSingleByObjectType(
					Hit,
					ExplosionLocation,
					Victim->GetActorLocation(),
					FCollisionObjectQueryParams(ECC_WorldStatic),
					LineParams
				);

				if (bBlocked)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[DamageDiag][LauncherSkip] Victim=%s Reason=BlockedByWorldStatic Blocker=%s BlockLoc=%s"),
						*GetNameSafe(Victim),
						*GetNameSafe(Hit.GetActor()),
						*Hit.ImpactPoint.ToString());
					continue;
				}
			}

			DamagedActors.Add(Victim);
			UE_LOG(LogTemp, Warning,
				TEXT("[DamageDiag][LauncherApplyDamage] Victim=%s Damage=%.1f Distance=%.1f"),
				*GetNameSafe(Victim),
				Damage,
				FVector::Dist(ExplosionLocation, Victim->GetActorLocation()));
			UGameplayStatics::ApplyPointDamage(
				Victim,
				Damage,
				(Victim->GetActorLocation() - ExplosionLocation).GetSafeNormal(),
				FHitResult(),
				GetInstigatorController(),
				this,
				UDamageType::StaticClass()
			);
		}
	}

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
