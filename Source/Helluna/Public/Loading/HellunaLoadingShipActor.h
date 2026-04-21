// ════════════════════════════════════════════════════════════════════════════════
// HellunaLoadingShipActor.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 로딩 씬 우주선 액터 (Reedme/loading/05-loading-scene-3d.md, 08 §핵심결정 1).
// Roll/Pitch/Float를 Tick 기반으로 재생하고, 배리어 해제 시 감쇠한다.
//
// 📌 작성자: Gihyeon (Loading Barrier Stage D)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HellunaLoadingShipActor.generated.h"

class UStaticMeshComponent;

UCLASS()
class HELLUNA_API AHellunaLoadingShipActor : public AActor
{
	GENERATED_BODY()

public:
	AHellunaLoadingShipActor();

	virtual void Tick(float DeltaSeconds) override;

	/** 배리어 해제 시 호출 — ShipDampenDuration 동안 Roll/Pitch/Float를 0으로 감쇠시킨다. */
	UFUNCTION(BlueprintCallable, Category = "Loading Ship")
	void BeginDampenForRelease();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loading Ship")
	TObjectPtr<UStaticMeshComponent> ShipMesh;

	// ────────────────────────────────────────────────────────────────────────────
	// 기본 애니메이션 파라미터
	// ────────────────────────────────────────────────────────────────────────────

	UPROPERTY(EditDefaultsOnly, Category = "Loading Ship|Animation",
		meta = (DisplayName = "Roll Amplitude (deg)", ClampMin = "0.0", ClampMax = "45.0"))
	float RollAmplitudeDeg = 7.f;

	UPROPERTY(EditDefaultsOnly, Category = "Loading Ship|Animation",
		meta = (DisplayName = "Pitch Amplitude (deg)", ClampMin = "0.0", ClampMax = "45.0"))
	float PitchAmplitudeDeg = 3.f;

	UPROPERTY(EditDefaultsOnly, Category = "Loading Ship|Animation",
		meta = (DisplayName = "Roll Speed (rad/s)", ClampMin = "0.0"))
	float RollSpeed = 0.45f;

	UPROPERTY(EditDefaultsOnly, Category = "Loading Ship|Animation",
		meta = (DisplayName = "Pitch Speed (rad/s)", ClampMin = "0.0"))
	float PitchSpeed = 0.30f;

	UPROPERTY(EditDefaultsOnly, Category = "Loading Ship|Animation",
		meta = (DisplayName = "Float Amplitude (cm)", ClampMin = "0.0"))
	float FloatAmplitudeCm = 25.f;

	UPROPERTY(EditDefaultsOnly, Category = "Loading Ship|Animation",
		meta = (DisplayName = "Float Speed (rad/s)", ClampMin = "0.0"))
	float FloatSpeed = 0.7f;

	UPROPERTY(EditDefaultsOnly, Category = "Loading Ship|Release",
		meta = (DisplayName = "Dampen Duration (sec)", ClampMin = "0.1"))
	float ShipDampenDuration = 1.2f;

private:
	FVector BaseLocation = FVector::ZeroVector;
	FRotator BaseRotation = FRotator::ZeroRotator;

	float TimeAccum = 0.f;

	bool bDampening = false;
	float DampenElapsed = 0.f;
};
