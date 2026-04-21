// ════════════════════════════════════════════════════════════════════════════════
// HellunaLoadingShipActor.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 로딩 씬 우주선 액터 (Reedme/loading/05-loading-scene-3d.md, 08 §핵심결정 1).
// Roll/Pitch/Float를 Tick 기반으로 재생하고, 두 종류의 감쇠를 지원한다.
//   - BeginDampenForRelease (1.2s) : C구간 → 페이드 진입 전 감쇠 (v1)
//   - BeginSettleForTravel (0.5s)  : A→B 전환 전 감쇠 + OnSettleComplete (v2.1)
// C구간 진입 시 ApplyPoseSnapshot으로 TimeAccum/AmpScale 위상 복원 + 재가속.
//
// 📌 작성자: Gihyeon (Loading Barrier Stage D / §13 v2.1)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HellunaLoadingShipActor.generated.h"

class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoadingShipSettleComplete);

UCLASS()
class HELLUNA_API AHellunaLoadingShipActor : public AActor
{
	GENERATED_BODY()

public:
	AHellunaLoadingShipActor();

	virtual void Tick(float DeltaSeconds) override;

	// ─────────────────────────────────────────────────────────────────────
	// 두 개의 감쇠 API (v2.1 공존)
	// ─────────────────────────────────────────────────────────────────────

	/** [v1 기존] C구간 → 페이드 진입 전 감쇠 (ShipDampenDuration, 1.2s). */
	UFUNCTION(BlueprintCallable, Category = "Loading Ship")
	void BeginDampenForRelease();

	/** [v2.1 신규] A→B 전환 전 감쇠 (ShipSettleDuration, 0.5s).
	 *  완료 시 OnSettleComplete 브로드캐스트. */
	UFUNCTION(BlueprintCallable, Category = "Loading Ship")
	void BeginSettleForTravel();

	/** §13 §3.1.3 (Q6) — A구간 마지막 Tick의 TimeAccum 값. */
	UFUNCTION(BlueprintCallable, Category = "Loading Ship")
	float GetCurrentTimeAccum() const { return TimeAccum; }

	/** §13 §3.1.3 (Q6) — A구간 마지막 Tick의 AmpScale 값 (settle 직후 ≈0). */
	UFUNCTION(BlueprintCallable, Category = "Loading Ship")
	float GetCurrentAmpScale() const;

	/** §13 §3.3.3 Phase 2b — 저장된 TimeAccum + AmpScale 복원 후 점진 재가속(ease-in). */
	UFUNCTION(BlueprintCallable, Category = "Loading Ship")
	void ApplyPoseSnapshot(float InTimeAccum, float InAmpScale);

	/** Settle 완료 시 브로드캐스트 (LobbyController가 구독). */
	UPROPERTY(BlueprintAssignable, Category = "Loading Ship")
	FOnLoadingShipSettleComplete OnSettleComplete;

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

	/** [v1] C → 페이드 전 감속 시간. */
	UPROPERTY(EditDefaultsOnly, Category = "Loading Ship|Release",
		meta = (DisplayName = "Dampen Duration (sec)", ClampMin = "0.1"))
	float ShipDampenDuration = 1.2f;

	/** [v2.1] A → B 전 감속 시간. */
	UPROPERTY(EditDefaultsOnly, Category = "Loading Ship|Release",
		meta = (DisplayName = "Settle Duration (sec)", ClampMin = "0.1"))
	float ShipSettleDuration = 0.5f;

	/** [v2.1] C 재진입 시 재가속 시간. */
	UPROPERTY(EditDefaultsOnly, Category = "Loading Ship|Release",
		meta = (DisplayName = "Restore Duration (sec)", ClampMin = "0.1"))
	float ShipRestoreDuration = 0.8f;

private:
	FVector BaseLocation = FVector::ZeroVector;
	FRotator BaseRotation = FRotator::ZeroRotator;

	float TimeAccum = 0.f;

	// 감쇠 상태 (BeginDampenForRelease / BeginSettleForTravel 공용 — ActiveDuration으로 구분)
	bool bDampening = false;
	float DampenElapsed = 0.f;
	float ActiveDuration = 1.2f;

	// 재가속 상태 (v2.1 신규 — ApplyPoseSnapshot 직후)
	bool bRestoring = false;
	float RestoreElapsed = 0.f;
	float RestoreStartAmp = 0.f;

	// settle 완료 브로드캐스트 가드 (Dampen은 false→안 쏨 / Settle은 true→쏨)
	bool bSettleCompleteFired = false;
};
