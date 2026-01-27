// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Weapon/HellunaHeroWeapon.h"
#include "NiagaraSystem.h"
#include "HeroWeapon_GunBase.generated.h"

/**
 * 
 */
UENUM(BlueprintType)
enum class EWeaponFireMode : uint8
{
	SemiAuto UMETA(DisplayName = "단발"),
	FullAuto UMETA(DisplayName = "연발")
};

USTRUCT(BlueprintType)
struct FGunAnimationSet
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (DisplayName = "장전 애니메이션"))
	UAnimMontage* Reload = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAmmoChanged, int32, CurrentAmmo, int32, MaxAmmo);

UCLASS()
class HELLUNA_API AHeroWeapon_GunBase : public AHellunaHeroWeapon
{
	GENERATED_BODY()
	
public:
	AHeroWeapon_GunBase();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats", meta = (DisplayName = "상하 반동"))
	float ReboundUp = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats", meta = (DisplayName = "좌우 반동"))
	float ReboundLeftRight = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire", meta = (DisplayName = "발사 모드"))
	EWeaponFireMode FireMode = EWeaponFireMode::SemiAuto;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats")
	float Range = 20000.f;
	
	UFUNCTION(BlueprintCallable, Category = "Weapon|Fire")
	virtual void Fire(AController* InstigatorController);

	// ===== [ADD] 탄창 최대치
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats", meta = (DisplayName = "탄창"))
	int32 MaxMag = 30;

	// ===== [ADD] 탄창 현재치 (복제)
	UPROPERTY(ReplicatedUsing = OnRep_CurrentAmmoInMag, BlueprintReadOnly, Category = "Weapon|Stats")
	int32 CurrentMag = 30;

	// ===== [ADD] UI에 뿌릴 이벤트
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Ammo")
	FOnAmmoChanged OnAmmoChanged;

	// 발사 가능?
	UFUNCTION(BlueprintPure, Category = "Weapon|Ammo")
	bool CanFire() const;

	// 장전 가능? 
	UFUNCTION(BlueprintPure, Category = "Weapon|Ammo")
	bool CanReload() const;

	// 장전(클라 호출 가능, 서버에서 최종 처리)
	UFUNCTION(BlueprintCallable, Category = "Weapon|Ammo")
	void Reload();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Anim", meta = (DisplayName = "총기 전용 애니메이션"))
	FGunAnimationSet GunAnimSet;

	const FGunAnimationSet& GetAnimSet() const { return GunAnimSet; }


protected:
	UFUNCTION(Server, Reliable)
	void ServerReload();

	void Reload_Internal();

	UFUNCTION()
	void OnRep_CurrentAmmoInMag();

	void BroadcastAmmoChanged();

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	// 서버에서 실제 히트판정/데미지 수행
	UFUNCTION(Server, Reliable)
	void ServerFire(AController* InstigatorController, FVector_NetQuantize TraceStart, FVector_NetQuantize TraceEnd);

	// (선택) 이펙트/사운드 동기화용
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFireFX(FVector_NetQuantize TraceStart, FVector_NetQuantize TraceEnd, bool bHit, FVector_NetQuantize HitLocation);

	// 실제 라인트레이스 + 데미지 적용
	void DoLineTraceAndDamage(AController* InstigatorController, const FVector& TraceStart, const FVector& TraceEnd);



	// 트레이스 채널
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Trace")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	TObjectPtr<UNiagaraSystem> ImpactFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	FVector ImpactFXScale = FVector(1.f, 1.f, 1.f);
};
