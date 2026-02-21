// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Weapon/HellunaHeroWeapon.h"
#include "NiagaraSystem.h"
// [Phase 7.5] EquipActor 발사 사운드용
#include "Player/Inv_PlayerController.h"
#include "EquipmentManagement/EquipActor/Inv_EquipActor.h"
#include "HeroWeapon_GunBase.generated.h"

/**
 * 
 */

class AHellunaHeroCharacter;

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats", meta = (DisplayName = "상하 반동"))
	float ReboundUp = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats", meta = (DisplayName = "좌우 반동"))
	float ReboundLeftRight = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats", meta = (DisplayName = "발사 모드"))
	EWeaponFireMode FireMode = EWeaponFireMode::SemiAuto;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats")
	float Range = 20000.f;
	
	UFUNCTION(BlueprintCallable, Category = "Weapon|Fire")
	virtual void Fire(AController* InstigatorController);

	// ===== [ADD] 탄창 최대치
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats", meta = (DisplayName = "탄창"))
	int32 MaxMag = 30;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Recoil", meta = (DisplayName = "반동 복귀 시간"))
	float RecoilReturnDuration = 0.10f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Recoil", meta = (DisplayName = "반동 복귀 강도"))
	float RecoilReturnStrength = 0.3f; // 스나이퍼 0.3 기준

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Recoil", meta = (DisplayName = "반동 틱 간격"))
	float RecoilTickInterval = 0.01f;

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

	virtual void BeginPlay() override;

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

	// (선택) 이펙트/사운드 동기화용
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFireFX(FVector_NetQuantize TraceStart, FVector_NetQuantize TraceEnd, bool bHit, FVector_NetQuantize HitLocation);

	// ════════════════════════════════════════════════════════════════
	// [Phase 7.5] 발사 사운드/FX 헬퍼 (자식 클래스에서도 사용)
	// ════════════════════════════════════════════════════════════════
	// [2026-02-18] 작업자: 김기현
	// ────────────────────────────────────────────────────────────────
	// 사운드와 FX를 분리하여 Shotgun 등 자식 클래스가
	// 사운드는 1회, FX는 펠릿 수만큼 호출 가능하게 함.
	//
	// PlayEquipActorFireSound():
	//   EquipActor의 GetFireSound() → 소음기 여부 자동 분기
	//   소음기 장착 시 → SuppressedFireSound
	//   소음기 미장착 시 → DefaultFireSound
	//   BP에서 변경 불가 — EquipActor의 UPROPERTY는 private
	//   (EquipActor BP 디테일 패널에서 무기별 사운드 에셋 설정)
	//
	// SpawnImpactFX(Location):
	//   기존 Niagara 임팩트 이펙트 로직 분리
	// ════════════════════════════════════════════════════════════════

	// 발사 사운드 1회 재생 (EquipActor 기반, 소음기 자동 분기)
	void PlayEquipActorFireSound();

	// 임팩트 FX 1회 스폰 (Niagara)
	void SpawnImpactFX(const FVector& SpawnLocation);

	// 실제 라인트레이스 + 데미지 적용
	void DoLineTraceAndDamage(AController* InstigatorController, const FVector& TraceStart, const FVector& TraceEnd);



	// 트레이스 채널
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Trace")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	TObjectPtr<UNiagaraSystem> ImpactFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	FVector ImpactFXScale = FVector(1.f, 1.f, 1.f);

// 연사속도 관련 함수
public:
	//다음 발사 시간
	UPROPERTY(Transient)  
	float NextAllowedFireTime = 0.f;

	// 다음 발사시간에 도달했는지 확인
	bool CanFireByRate(float Now, float Interval) const
	{
		return Now >= NextAllowedFireTime;
	}

	// 발사 후 다음 발사시간 갱신
	void ConsumeFireByRate(float Now, float Interval)
	{
		NextAllowedFireTime = Now + FMath::Max(Interval, 0.01f);
	}

	//반동
	UFUNCTION(BlueprintCallable, Category = "Weapon|Recoil")
	void ApplyRecoil(AHellunaHeroCharacter* TargetCharacter);

private:
	// ✅ [ADD] 반동 내부 구현(로컬 전용)
	FTimerHandle RecoilTimerHandle;

	TWeakObjectPtr<AHellunaHeroCharacter> RecoilTarget;

	// 타겟(누적되는 목표 반동 오프셋)
	float RecoilTargetPitch = 0.f;
	float RecoilTargetYaw = 0.f;

	// 현재 적용 중인 오프셋(카메라에 이미 반영된 값)
	float RecoilCurrentPitch = 0.f;
	float RecoilCurrentYaw = 0.f;

	// 이전 프레임 오프셋(Delta 계산용)
	float RecoilPrevPitch = 0.f;
	float RecoilPrevYaw = 0.f;

	// 시작 딜레이(고정 0.03) - 첫 발에만 적용하고 연사 중엔 재딜레이 안 걸리게
	float RecoilStartDelayRemaining = 0.f;
	

	void TickRecoil();
};
