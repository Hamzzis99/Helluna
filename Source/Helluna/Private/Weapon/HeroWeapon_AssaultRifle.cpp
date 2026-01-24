// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/HeroWeapon_AssaultRifle.h"

#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "NiagaraFunctionLibrary.h"			
#include "NiagaraSystem.h"

#include "DebugHelper.h"	

	void AHeroWeapon_AssaultRifle::Fire(AController* InstigatorController)
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
			DoLineTraceAndDamage(InstigatorController, TraceStart, TraceEnd);
		}	
		else
		{
			ServerFire(InstigatorController, TraceStart, TraceEnd);  //서버에 발사 요청
		}
	}

	void AHeroWeapon_AssaultRifle::ServerFire_Implementation(AController* InstigatorController, FVector_NetQuantize TraceStart, FVector_NetQuantize TraceEnd)
	{
		DoLineTraceAndDamage(InstigatorController, TraceStart, TraceEnd);
	}

	void AHeroWeapon_AssaultRifle::DoLineTraceAndDamage(AController* InstigatorController, const FVector& TraceStart, const FVector& TraceEnd)
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

	void AHeroWeapon_AssaultRifle::MulticastFireFX_Implementation(FVector_NetQuantize TraceStart, FVector_NetQuantize TraceEnd, bool bHit, FVector_NetQuantize HitLocation)
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

