// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Weapon/HeroWeapon_GunBase.h"
#include "HeroWeapon_Shotgun.generated.h"

/**
 * 
 */
UCLASS()
class HELLUNA_API AHeroWeapon_Shotgun : public AHeroWeapon_GunBase
{
	GENERATED_BODY()
	
private:
	// 펠릿 개수 (한 발에 몇 줄 쏘는지)
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Stats", meta = (DisplayName = "총알 개수"))
	int32 PelletCount = 10;

	// 퍼짐 반각(도). 예: 6도면 꽤 퍼짐
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Stats", meta = (DisplayName = "탄 퍼짐 각도"))
	float SpreadHalfAngleDeg = 6.f;

	// ✅ 패턴이 너무 고정처럼 보일 때 “약간 랜덤” (0.3~1.0 추천)
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Stats", meta = (DisplayName = "탄 퍼짐 랜덤성"))
	float JitterDeg = 0.5f;

public:
	virtual void Fire(AController* InstigatorController) override;
	
	void DoLineTraceAndDamage_Shotgun(
		AController* InstigatorController,
		const FVector& TraceStart
	);

protected:
	// [ADD] 펠릿 FX를 한 번에 보내기 (RPC 1회)
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFireShotgunFX(
		FVector_NetQuantize TraceStart,
		const TArray<FVector_NetQuantize>& TraceEnds,
		const TArray<uint8>& HitFlags,
		const TArray<FVector_NetQuantize>& HitLocations
	);
};
