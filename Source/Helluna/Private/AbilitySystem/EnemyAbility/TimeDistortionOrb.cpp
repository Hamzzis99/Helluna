// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/TimeDistortionOrb.h"

#include "Components/SphereComponent.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

#define ORB_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[TimeDistortionOrb] " Fmt), ##__VA_ARGS__)

ATimeDistortionOrb::ATimeDistortionOrb()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->InitSphereRadius(OrbCollisionRadius);
	CollisionSphere->SetCollisionObjectType(ECC_Pawn);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);  // 투사체 감지
	CollisionSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);       // 히트스캔 감지
	CollisionSphere->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CollisionSphere->SetGenerateOverlapEvents(true);
	CollisionSphere->SetHiddenInGame(true);
	CollisionSphere->ShapeColor = FColor::Magenta;
	SetRootComponent(CollisionSphere);
}

void ATimeDistortionOrb::BeginPlay()
{
	Super::BeginPlay();

	// BP에서 설정한 반경 적용
	CollisionSphere->SetSphereRadius(OrbCollisionRadius);

	// VFX 스폰
	SpawnOrbVFX();

	ORB_LOG("[COLLISION] %s: ObjectType=%d, Enabled=%d, OverlapEvents=%d, "
		"Pawn=%d, WorldDynamic=%d, WorldStatic=%d, Visibility=%d, Radius=%.1f",
		*GetName(),
		(int32)CollisionSphere->GetCollisionObjectType(),
		(int32)CollisionSphere->GetCollisionEnabled(),
		CollisionSphere->GetGenerateOverlapEvents() ? 1 : 0,
		(int32)CollisionSphere->GetCollisionResponseToChannel(ECC_Pawn),
		(int32)CollisionSphere->GetCollisionResponseToChannel(ECC_WorldDynamic),
		(int32)CollisionSphere->GetCollisionResponseToChannel(ECC_WorldStatic),
		(int32)CollisionSphere->GetCollisionResponseToChannel(ECC_Visibility),
		CollisionSphere->GetScaledSphereRadius());
}

// -----------------------------------------------------------------
// 리플리케이션
// -----------------------------------------------------------------
void ATimeDistortionOrb::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATimeDistortionOrb, bIsKeyOrb);
}

// -----------------------------------------------------------------
// InitOrb (서버에서 호출)
// -----------------------------------------------------------------
void ATimeDistortionOrb::InitOrb(bool bInIsKeyOrb)
{
	bIsKeyOrb = bInIsKeyOrb;

	ORB_LOG("InitOrb: bIsKey=%s", bIsKeyOrb ? TEXT("TRUE") : TEXT("FALSE"));
}

// -----------------------------------------------------------------
// SpawnOrbVFX
// -----------------------------------------------------------------
void ATimeDistortionOrb::SpawnOrbVFX()
{
	if (!OrbVFX) return;
	if (OrbVFXComp && OrbVFXComp->IsActive()) return;

	OrbVFXComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
		OrbVFX,
		GetRootComponent(),
		NAME_None,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		FVector(OrbVFXScale),
		EAttachLocation::KeepRelativeOffset,
		true,
		ENCPoolMethod::None,
		true
	);

	ORB_LOG("SpawnOrbVFX: Component=%s", OrbVFXComp ? TEXT("OK") : TEXT("FAILED"));
}

// -----------------------------------------------------------------
// TakeDamage
// -----------------------------------------------------------------
float ATimeDistortionOrb::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	ORB_LOG("TakeDamage: %.1f from %s, bIsKey=%s",
		ActualDamage,
		DamageCauser ? *DamageCauser->GetName() : TEXT("NULL"),
		bIsKeyOrb ? TEXT("TRUE") : TEXT("FALSE"));

	// 파괴 VFX 멀티캐스트
	if (DestroyVFX)
	{
		Multicast_PlayDestroyVFX(DestroyVFX, DestroyVFXScale, GetActorLocation());
	}

	// VFX 끄기
	if (OrbVFXComp)
	{
		OrbVFXComp->DeactivateImmediate();
		OrbVFXComp = nullptr;
	}

	OnOrbDestroyed.Broadcast(this);
	Destroy();

	return ActualDamage;
}

// -----------------------------------------------------------------
// Multicast_PlayDestroyVFX
// -----------------------------------------------------------------
void ATimeDistortionOrb::Multicast_PlayDestroyVFX_Implementation(
	UNiagaraSystem* Effect, float Scale, FVector Location)
{
	if (!Effect) return;

	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		Effect,
		Location,
		FRotator::ZeroRotator,
		FVector(Scale),
		true
	);
}

#undef ORB_LOG
