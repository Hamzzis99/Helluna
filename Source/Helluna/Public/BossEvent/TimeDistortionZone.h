// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "BossEvent/BossPatternZoneBase.h"
#include "TimeDistortionZone.generated.h"

class UNiagaraSystem;
class USphereComponent;
class AHellunaHeroCharacter;
class ATimeDistortionOrb;

/**
 * ATimeDistortionZone
 *
 * 시간 왜곡 패턴의 실제 로직을 담당하는 Zone Actor.
 * GA가 이 Actor를 스폰하면, Overlap 기반으로:
 *  - 범위 진입 시 슬로우 적용
 *  - 범위 이탈 시 슬로우 해제
 *  - Orb 스폰/관리 + 파훼 처리
 *  - 파훼 실패 시 폭발 데미지
 *
 * BP에서 상속하여 VFX 에셋을 설정할 수 있다.
 */
UCLASS(Blueprintable)
class HELLUNA_API ATimeDistortionZone : public ABossPatternZoneBase
{
	GENERATED_BODY()

public:
	ATimeDistortionZone();

	// =========================================================
	// ABossPatternZoneBase 인터페이스
	// =========================================================
	virtual void ActivateZone() override;
	virtual void DeactivateZone() override;

	// =========================================================
	// 시간 왜곡 설정
	// =========================================================

	/** 슬로우 적용 시 플레이어의 시간 배율. 0.1 = 원래 속도의 10%. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|효과",
		meta = (DisplayName = "시간 감속 배율", ClampMin = "0.05", ClampMax = "0.9"))
	float TimeDilationScale = 0.3f;

	/** 시간 복원 시 범위 내 플레이어에게 적용할 데미지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|효과",
		meta = (DisplayName = "폭발 데미지", ClampMin = "0.0"))
	float DetonationDamage = 50.f;

	// =========================================================
	// Orb (파훼 구체) 설정
	// =========================================================

	/** 데코 Orb BP 클래스 (VFX, 충돌 반경은 BP에서 설정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "데코 Orb BP 클래스"))
	TSubclassOf<ATimeDistortionOrb> DecoyOrbClass;

	/** 키 Orb BP 클래스 (파훼 대상 — 색/VFX가 다른 특수 Orb) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "키 Orb BP 클래스"))
	TSubclassOf<ATimeDistortionOrb> KeyOrbClass;

	/** 스폰할 데코 Orb 개수 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "데코 Orb 개수", ClampMin = "2", ClampMax = "20"))
	int32 DecoyOrbCount = 5;

	/** Orb가 Zone 중심으로부터 스폰되는 거리 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "Orb 스폰 거리 (cm)", ClampMin = "100.0", ClampMax = "3000.0"))
	float OrbSpawnRadius = 600.f;

	/** Orb 스폰 높이 오프셋 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "Orb 높이 오프셋 (cm)", ClampMin = "0.0", ClampMax = "500.0"))
	float OrbHeightOffset = 150.f;

	/** Orb 스폰 간격 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "Orb 스폰 간격 (초)", ClampMin = "0.05", ClampMax = "1.0"))
	float OrbSpawnInterval = 0.3f;

	/** 파훼 성공 시 재생할 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "파훼 성공 사운드"))
	TObjectPtr<USoundBase> BreakSuccessSound = nullptr;

	// =========================================================
	// VFX 설정
	// =========================================================

	/** 슬로우 영역 지속 VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "슬로우 영역 VFX"))
	TObjectPtr<UNiagaraSystem> SlowAreaVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "슬로우 영역 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float SlowAreaVFXScale = 1.f;

	/** 패턴 종료 VFX (슬로우 해제 시 먼저 재생, 이후 폭발/파훼 VFX 재생) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "패턴 종료 VFX"))
	TObjectPtr<UNiagaraSystem> PatternEndVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "패턴 종료 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float PatternEndVFXScale = 1.f;

	/** 패턴 종료 VFX 재생 후 결과 VFX(폭발/파훼)까지의 딜레이 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "결과 VFX 딜레이 (초)", ClampMin = "0.0", ClampMax = "5.0"))
	float ResultVFXDelay = 0.5f;

	/** 폭발 VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "폭발 VFX"))
	TObjectPtr<UNiagaraSystem> DetonationVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "폭발 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float DetonationVFXScale = 1.f;

	/** 파훼 성공 VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "파훼 성공 VFX"))
	TObjectPtr<UNiagaraSystem> BreakSuccessVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "파훼 성공 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float BreakSuccessVFXScale = 1.f;

	// =========================================================
	// 사운드 설정
	// =========================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|사운드",
		meta = (DisplayName = "슬로우 시작 사운드"))
	TObjectPtr<USoundBase> SlowStartSound = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|사운드",
		meta = (DisplayName = "폭발 사운드"))
	TObjectPtr<USoundBase> DetonationSound = nullptr;

protected:
	virtual void BeginPlay() override;
	virtual void Destroyed() override;

private:
	// =========================================================
	// 콜리전 컴포넌트
	// =========================================================

	UPROPERTY(VisibleAnywhere, Category = "시간 왜곡")
	TObjectPtr<USphereComponent> SlowSphere = nullptr;

	// =========================================================
	// Overlap 콜백
	// =========================================================

	UFUNCTION()
	void OnSlowSphereBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnSlowSphereEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	// =========================================================
	// 슬로우 적용/해제 (CustomTimeDilation 기반)
	// =========================================================

	/** 존 안에 들어온 액터에게 CustomTimeDilation 적용 (자기 자신과 소유 Enemy 제외) */
	void ApplySlowToActor(AActor* Actor);
	void RemoveSlowFromActor(AActor* Actor);
	void RestoreAllSlowedActors();

	/** 슬로우 적용 전 원본 CustomTimeDilation 저장 */
	TMap<TWeakObjectPtr<AActor>, float> SlowedActors;

	// =========================================================
	// 폭발 (파훼 실패)
	// =========================================================

	/** 패턴 종료 VFX 재생 후 실제 폭발 처리 */
	void BeginDetonation();
	void FinishDetonation();

	/** 패턴 종료 VFX 재생 후 실제 파훼 처리 */
	void BeginPatternBroken();
	void FinishPatternBroken();

	FTimerHandle DetonationTimerHandle;
	FTimerHandle ResultVFXTimerHandle;

	// =========================================================
	// Orb 관련
	// =========================================================

	void StartOrbSpawnSequence();
	void SpawnNextOrb();
	void DestroyAllOrbs();

	UFUNCTION()
	void OnOrbDestroyed(ATimeDistortionOrb* DestroyedOrb);

	void OnPatternBroken();

	UPROPERTY(Transient)
	TArray<TObjectPtr<ATimeDistortionOrb>> SpawnedOrbs;

	bool bPatternBroken = false;
	bool bZoneActive = false;

	int32 OrbSpawnCurrentIndex = 0;
	int32 OrbSpawnTotalCount = 0;
	int32 OrbSpawnKeyIndex = 0;
	FTimerHandle OrbSpawnTimerHandle;

};
