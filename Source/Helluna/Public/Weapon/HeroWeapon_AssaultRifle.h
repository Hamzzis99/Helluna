// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Weapon/HellunaHeroWeapon.h"
#include "NiagaraSystem.h"
#include "HeroWeapon_AssaultRifle.generated.h"


/**
 * 
 */
UCLASS()
class HELLUNA_API AHeroWeapon_AssaultRifle : public AHellunaHeroWeapon
{
	GENERATED_BODY()

public:
	AHeroWeapon_AssaultRifle();

	// GA(또는 캐릭터)에서 호출하는 “클라 진입점”
	virtual void Fire(AController* InstigatorController) override;

protected:
	// 서버에서 실제 히트판정/데미지 수행
	UFUNCTION(Server, Reliable)
	void ServerFire(AController* InstigatorController, FVector_NetQuantize TraceStart, FVector_NetQuantize TraceEnd);

	// (선택) 이펙트/사운드 동기화용
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFireFX(FVector_NetQuantize TraceStart, FVector_NetQuantize TraceEnd, bool bHit, FVector_NetQuantize HitLocation);

	// 실제 라인트레이스 + 데미지 적용
	void DoLineTraceAndDamage(AController* InstigatorController, const FVector& TraceStart, const FVector& TraceEnd);

protected:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats")
	float Range = 20000.f;

	// 트레이스 채널
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Trace")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	TObjectPtr<UNiagaraSystem> ImpactFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	FVector ImpactFXScale = FVector(1.f, 1.f, 1.f);
};
	
	
