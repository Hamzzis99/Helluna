/**
 * BossSlamWaveActor.h
 *
 * 보스 내려찍기 등의 임팩트 표현용 "퍼져나가는 파동" 액터.
 *
 * 동작:
 *  - BeginPlay 시 LifeTime 만큼 자기 수명 설정 (자동 Destroy).
 *  - Tick 으로 StartScale → EndScale 보간 (선형 또는 ease-out).
 *  - 머터리얼(M_BossSlamWave 권장) 의 Intensity 파라미터를 Lifetime 동안 fade out.
 *  - [WaveSelfDamageV1] 서버 인스턴스에서는 매 틱 링 범위 내 플레이어에게 데미지 적용 (시각/데미지 싱크 보장).
 *
 * BP 사용:
 *  - 이 클래스를 부모로 BP 생성 (예: BP_BossSlamWave).
 *  - WaveMesh 컴포넌트의 StaticMesh = Plane, Material = M_BossSlamWave 셋업.
 *  - 디자이너가 StartScale / EndScale / LifeTime / FadeIntensity / 데미지 값 조정.
 *
 * 데미지/충돌:
 *  - 서버 액터(ShockwaveZone 직접 스폰) 만 데미지 판정. Multicast 로 복제된 클라 시각 액터는 렌더만.
 *  - Zone 이 SpawnActorDeferred 로 스폰 후 Damage/RingThickness/VerticalThickness/JumpDodge/Instigator 오버라이드 가능.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BossSlamWaveActor.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class AHellunaEnemyCharacter;

UCLASS()
class HELLUNA_API ABossSlamWaveActor : public AActor
{
	GENERATED_BODY()

public:
	ABossSlamWaveActor();

	/** 시작 스케일 (XY = 작은 디스크). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "파동")
	FVector StartScale = FVector(0.5f, 0.5f, 0.5f);

	/** 끝 스케일 (XY = 펼쳐진 큰 디스크). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "파동")
	FVector EndScale = FVector(8.f, 8.f, 8.f);

	/** 파동 지속 시간 (초). 이 시간 후 Destroy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "파동",
		meta = (ClampMin = "0.05", ClampMax = "10.0"))
	float LifeTime = 1.2f;

	/**
	 * 시간에 따른 스케일 보간 곡선.
	 * 1.0 = 선형. >1 = 빠르게 시작 후 느려짐 (ease-out).  <1 = 느리게 시작 후 가속 (ease-in).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "파동",
		meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float ScaleEasing = 0.6f;

	/** true 면 머터리얼의 Intensity 파라미터를 fade out. M_BossSlamWave 와 호환. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "파동")
	bool bFadeIntensity = true;

	/** Fade 시작 시 사용할 머터리얼 Intensity (0 까지 보간). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "파동",
		meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float StartIntensity = 5.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "파동")
	TObjectPtr<UStaticMeshComponent> WaveMesh;

	/** 스케일 1.0 기준 메쉬의 수평 반지름 (cm). BeginPlay 에서 mesh bounds 로 자동 감지. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "파동|Debug")
	float BaseMeshRadius = 50.f;

	// =========================================================
	// [WaveSelfDamageV1] 데미지 판정 (서버 인스턴스 Tick 에서 실행)
	// =========================================================

	/** 링 한 번 피격 시 적용할 데미지 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "파동|데미지",
		meta = (DisplayName = "피격 데미지", ClampMin = "0.0"))
	float Damage = 30.f;

	/** 링의 수평 두께 (cm) — 현재 반지름 ± (RingThickness/2) 안에 있으면 피격 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "파동|데미지",
		meta = (DisplayName = "링 두께 (cm)", ClampMin = "10.0", ClampMax = "1000.0"))
	float RingThickness = 150.f;

	/**
	 * 파동의 수직 두께 (cm). 파동 중심 Z ± (VerticalThickness/2) 구간이 "파동 Z 범위".
	 * 플레이어 캡슐 Z 범위와 이 구간이 겹쳐야 피격 통과.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "파동|데미지",
		meta = (DisplayName = "수직 두께 (cm)", ClampMin = "10.0", ClampMax = "2000.0"))
	float VerticalThickness = 300.f;

	/** true 면 공중에 떠 있는 플레이어는 파동을 회피할 수 있다 (스킬 표현). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "파동|데미지",
		meta = (DisplayName = "점프 회피 가능"))
	bool bAllowJumpDodge = true;

	/** 점프 회피에 필요한 최소 공중 높이 (cm) — 발밑이 이 높이 이상이면 회피 성공. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "파동|데미지",
		meta = (DisplayName = "점프 회피 최소 높이 (cm)", ClampMin = "10.0", ClampMax = "300.0",
			EditCondition = "bAllowJumpDodge"))
	float JumpDodgeMinHeight = 80.f;

	/**
	 * 데미지 인스티게이터 (보스). Zone이 SpawnActorDeferred 후 세팅.
	 * 없으면 Damage 이벤트는 Instigator null 로 발사.
	 */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "파동|데미지")
	TObjectPtr<AHellunaEnemyCharacter> DamageInstigator = nullptr;

	/**
	 * 현재 링의 유효 반지름 (cm) — 데미지 판정 + 외부 싱크용.
	 */
	UFUNCTION(BlueprintCallable, Category = "파동")
	float GetCurrentRingRadius() const;

	FORCEINLINE float GetLifeTime()    const { return LifeTime; }
	FORCEINLINE float GetScaleEasing() const { return ScaleEasing; }
	FORCEINLINE float GetElapsedTime() const { return ElapsedTime; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** [WaveSelfDamageV1] 서버 인스턴스 Tick 에서 호출 — 링 범위 내 플레이어 데미지 */
	void ProcessRingDamage();

private:
	float ElapsedTime = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WaveMID = nullptr;

	/** 이 파동 인스턴스에서 이미 맞은 액터 — 중복 피격 방지 */
	UPROPERTY(Transient)
	TSet<TObjectPtr<AActor>> AlreadyHitActors;

	/** [WaveSelfDamageV1] 데미지 판정 활성 여부 — 서버 스폰만 true 로 켜짐 (BeginPlay 이후 판정) */
	bool bDamageActive = false;
};
