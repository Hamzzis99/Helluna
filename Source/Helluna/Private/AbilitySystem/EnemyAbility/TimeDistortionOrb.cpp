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

	// 콜리전: ECC_Pawn으로 설정 → 플레이어 MeleeTraceComponent의
	// SweepSingleByObjectType(ECC_Pawn) 에 감지됨
	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->InitSphereRadius(80.f);
	CollisionSphere->SetCollisionObjectType(ECC_Pawn);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	// 무기/트레이스 채널에도 응답하도록
	CollisionSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CollisionSphere->SetGenerateOverlapEvents(false);
	SetRootComponent(CollisionSphere);
}

// ─────────────────────────────────────────────────────────────
// 리플리케이션
// ─────────────────────────────────────────────────────────────
void ATimeDistortionOrb::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATimeDistortionOrb, bIsKeyOrb);
	DOREPLIFETIME(ATimeDistortionOrb, RepOrbVFXSystem);
	DOREPLIFETIME(ATimeDistortionOrb, RepOrbVFXScale);
}

// ─────────────────────────────────────────────────────────────
// InitOrb (서버에서 호출)
// ─────────────────────────────────────────────────────────────
void ATimeDistortionOrb::InitOrb(UNiagaraSystem* InVFX, float InVFXScale, bool bInIsKeyOrb)
{
	bIsKeyOrb = bInIsKeyOrb;
	RepOrbVFXSystem = InVFX;
	RepOrbVFXScale = InVFXScale;

	ORB_LOG("InitOrb: bIsKey=%s, VFX=%s, Scale=%.2f",
		bIsKeyOrb ? TEXT("TRUE") : TEXT("FALSE"),
		InVFX ? *InVFX->GetName() : TEXT("NULL"),
		InVFXScale);

	// 서버에서도 VFX 스폰 (리슨 서버용)
	SpawnOrbVFX();
}

// ─────────────────────────────────────────────────────────────
// OnRep_OrbVFXData — 클라이언트에서 VFX 스폰
// ─────────────────────────────────────────────────────────────
void ATimeDistortionOrb::OnRep_OrbVFXData()
{
	ORB_LOG("OnRep_OrbVFXData: VFX=%s, Scale=%.2f",
		RepOrbVFXSystem ? *RepOrbVFXSystem->GetName() : TEXT("NULL"),
		RepOrbVFXScale);

	SpawnOrbVFX();
}

// ─────────────────────────────────────────────────────────────
// SpawnOrbVFX — VFX 스폰 공통 로직
// ─────────────────────────────────────────────────────────────
void ATimeDistortionOrb::SpawnOrbVFX()
{
	if (!RepOrbVFXSystem) return;

	// 이미 스폰된 VFX가 있으면 스킵
	if (OrbVFXComp && OrbVFXComp->IsActive()) return;

	OrbVFXComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
		RepOrbVFXSystem,
		GetRootComponent(),
		NAME_None,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		FVector(RepOrbVFXScale),
		EAttachLocation::KeepRelativeOffset,
		true,
		ENCPoolMethod::None,
		true
	);

	ORB_LOG("SpawnOrbVFX: Component=%s", OrbVFXComp ? TEXT("OK") : TEXT("FAILED"));
}

// ─────────────────────────────────────────────────────────────
// TakeDamage — 플레이어 공격 피격 시
// ─────────────────────────────────────────────────────────────
float ATimeDistortionOrb::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	ORB_LOG("TakeDamage: %.1f from %s, bIsKey=%s",
		ActualDamage,
		DamageCauser ? *DamageCauser->GetName() : TEXT("NULL"),
		bIsKeyOrb ? TEXT("TRUE") : TEXT("FALSE"));

	// 파괴 VFX 멀티캐스트 (모든 클라이언트에서 재생)
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

	// 델리게이트 발동 후 제거
	OnOrbDestroyed.Broadcast(this);
	Destroy();

	return ActualDamage;
}

// ─────────────────────────────────────────────────────────────
// Multicast_PlayDestroyVFX
// ─────────────────────────────────────────────────────────────
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

	ORB_LOG("DestroyVFX played at %s", *Location.ToString());
}

#undef ORB_LOG
