// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/TimeDistortionOrb.h"

#include "Components/SphereComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

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
// InitOrb
// ─────────────────────────────────────────────────────────────
void ATimeDistortionOrb::InitOrb(UNiagaraSystem* InVFX, float InVFXScale, bool bInIsKeyOrb)
{
	bIsKeyOrb = bInIsKeyOrb;

	if (InVFX)
	{
		OrbVFXComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
			InVFX,
			GetRootComponent(),
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			FVector(InVFXScale),
			EAttachLocation::KeepRelativeOffset,
			true,
			ENCPoolMethod::None,
			true
		);
	}
}

// ─────────────────────────────────────────────────────────────
// TakeDamage — 플레이어 공격 피격 시
// ─────────────────────────────────────────────────────────────
float ATimeDistortionOrb::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	// 파괴 VFX 재생
	if (DestroyVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			DestroyVFX,
			GetActorLocation(),
			FRotator::ZeroRotator,
			FVector(DestroyVFXScale),
			true
		);
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
