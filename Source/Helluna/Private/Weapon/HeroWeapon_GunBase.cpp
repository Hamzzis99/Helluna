// Capstone Project Helluna


#include "Weapon/HeroWeapon_GunBase.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "NiagaraFunctionLibrary.h"			
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"


AHeroWeapon_GunBase::AHeroWeapon_GunBase()
{
	bReplicates = true;
}

bool AHeroWeapon_GunBase::CanFire() const
{
	return CurrentMag > 0;
}

bool AHeroWeapon_GunBase::CanReload() const
{
	// 무한탄: 탄창이 덜 찼으면 언제든 장전 가능
	return CurrentMag < MaxMag;
}

void AHeroWeapon_GunBase::Reload()
{
	// 서버가 최종 탄약 변경
	if (HasAuthority())
	{
		Reload_Internal();
	}
	else
	{
		ServerReload();
	}
}

void AHeroWeapon_GunBase::ServerReload_Implementation()
{
	Reload_Internal();
}

void AHeroWeapon_GunBase::Reload_Internal()
{
	if (!HasAuthority())
		return;

	if (!CanReload())
		return;

	// 무한탄: 그냥 풀 장전
	CurrentMag = MaxMag;

	BroadcastAmmoChanged();
}

void AHeroWeapon_GunBase::OnRep_CurrentAmmoInMag()
{
	BroadcastAmmoChanged();
}

void AHeroWeapon_GunBase::BroadcastAmmoChanged()
{
	OnAmmoChanged.Broadcast(CurrentMag, MaxMag);
}



void AHeroWeapon_GunBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHeroWeapon_GunBase, CurrentMag);
}

void AHeroWeapon_GunBase::Fire(AController* InstigatorController)
{
	if (!InstigatorController)
		return;

	APawn* Pawn = InstigatorController->GetPawn();
	if (!Pawn)
		return;

	FVector ViewLoc;
	FRotator ViewRot;
	InstigatorController->GetPlayerViewPoint(ViewLoc, ViewRot);  //카메라 기준 시점으로 발사

	const FVector TraceStart = ViewLoc;
	const FVector TraceEnd = TraceStart + (ViewRot.Vector() * Range);

	if (HasAuthority())
	{
		CurrentMag = FMath::Max(0, CurrentMag - 1);
		BroadcastAmmoChanged();

		DoLineTraceAndDamage(InstigatorController, TraceStart, TraceEnd);
	}
	else
	{
		ServerFire(InstigatorController, TraceStart, TraceEnd);  //서버에 발사 요청
	}
}

void AHeroWeapon_GunBase::ServerFire_Implementation(AController* InstigatorController, FVector_NetQuantize TraceStart, FVector_NetQuantize TraceEnd)
{
	DoLineTraceAndDamage(InstigatorController, TraceStart, TraceEnd);
}

void AHeroWeapon_GunBase::DoLineTraceAndDamage(AController* InstigatorController, const FVector& TraceStart, const FVector& TraceEnd)
{
	// “실제 히트판정 + 데미지 적용” 핵심 함수

	UWorld* World = GetWorld();
	if (!World || !InstigatorController)
		return;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AR_LineTrace), false);
	Params.AddIgnoredActor(this);

	APawn* Pawn = InstigatorController->GetPawn();
	if (Pawn)
	{
		Params.AddIgnoredActor(Pawn); // 자기 자신(발사자) 맞는 것 방지
	}

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(
		Hit,
		TraceStart,
		TraceEnd,
		TraceChannel,
		Params
	);

	// 맞았으면 히트 위치, 아니면 끝점
	const FVector HitLocation = bHit ? Hit.ImpactPoint : TraceEnd;

	if (bHit)
	{
		AActor* HitActor = Hit.GetActor();
		if (HitActor)
		{
			UGameplayStatics::ApplyPointDamage(
				HitActor,
				Damage,
				(TraceEnd - TraceStart).GetSafeNormal(),
				Hit,
				InstigatorController,
				this,
				UDamageType::StaticClass()
			);
		}
	}

	// 서버에서 FX 동기화 호출 (Unreliable: 총알 FX는 손실돼도 큰 문제 없음)
	MulticastFireFX(TraceStart, TraceEnd, bHit, HitLocation);

}

void AHeroWeapon_GunBase::MulticastFireFX_Implementation(FVector_NetQuantize TraceStart, FVector_NetQuantize TraceEnd, bool bHit, FVector_NetQuantize HitLocation)
{
	// “모든 클라이언트에서 보이는 연출” 전용

	const FVector SpawnLoc = bHit ? (FVector)HitLocation : (FVector)TraceEnd;
	if (ImpactFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this,
			ImpactFX,
			SpawnLoc,
			FRotator::ZeroRotator,
			ImpactFXScale,   // 에디터에서 조절 가능
			true,
			true,
			ENCPoolMethod::AutoRelease
		);
	}
}