// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BossPhase2CinematicTrigger.generated.h"

class APawn;
class ACameraActor;
class UBossDialogueWidget;
class UCameraShakeBase;

/**
 * [BossPhase2CinematicV1] 보스 광폭화(2페이즈) 진입 시네마틱 트리거.
 *
 * BossSummonCinematicTrigger 와 동일 패턴 — 보스 페이즈2 진입 시 spawn(또는 월드 배치) + TryActivate.
 * 트리거가 카메라 단계 시퀀스 (Face → Front → Top → Front) + 대사 + 시네마틱 종료 처리.
 * 보스 자체는 광폭화 모션, 갑옷 분리, 본체 swap, VFX 등 비주얼 처리만 담당.
 *
 * 단계 시퀀스 (총 InvulnerabilityDuration):
 *   1a (FaceDuration):     얼굴 클로즈업 고정 — 히트 임팩트
 *   1b (StunDuration - FaceDuration): Face → Front lerp
 *   2  (RiseDuration):     Front → Top lerp (위로)
 *   3  (자동 = Invuln - 다른 단계 합): Top hold (VFX 펼쳐짐)
 *   4  (DescentDuration):  Top → Front lerp (아래로)
 *   EndHold (자동 = Invuln - Stage4 끝): Front 정지 (전신 보임)
 */
UCLASS(Blueprintable)
class HELLUNA_API ABossPhase2CinematicTrigger : public AActor
{
	GENERATED_BODY()

public:
	ABossPhase2CinematicTrigger();

	// =========================================================================================
	// 카메라 위치 / LookAt
	// =========================================================================================

	/** 단계1a 카메라 — 얼굴 클로즈업 (보스 로컬: X 정면, Y 우측, Z 위). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Camera",
		meta = (DisplayName = "단계1a 카메라 (얼굴 클로즈업)"))
	FVector FaceOffset = FVector(50.f, -50.f, 90.f);

	/** 단계1a LookAt 보스 Z 오프셋. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Camera",
		meta = (DisplayName = "단계1a LookAt 높이 (cm)", ClampMin = "0.0", ClampMax = "300.0"))
	float FaceLookHeight = 90.f;

	/** 단계1b 카메라 — 전신 정면. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Camera",
		meta = (DisplayName = "단계1b 카메라 (전신 정면)"))
	FVector FrontOffset = FVector(550.f, 250.f, 150.f);

	/** 단계1b LookAt 보스 Z 오프셋 (90 = 허리). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Camera",
		meta = (DisplayName = "단계1b LookAt 높이 (cm)", ClampMin = "0.0", ClampMax = "300.0"))
	float FrontLookHeight = 90.f;

	/**
	 * [Phase2Stage4ReturnV1] 단계4 (Top→Front 하강) 종료 위치 + EndHold 위치 — Stage 1b 의 FrontOffset 과 분리.
	 *   레이저 강하 후 너무 가까이서 비춰 눈이 아픈 케이스 — 더 멀리 거리 둠 (X 더 크게).
	 *   기본 (550, 0, 100): 정면, Y=0 (좌우 중앙), Z=100.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Camera",
		meta = (DisplayName = "단계4 복귀 카메라 (멀리, 강하 후)"))
	FVector Stage4ReturnOffset = FVector(550.f, 0.f, 100.f);

	/** 단계4 복귀 LookAt 높이. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Camera",
		meta = (DisplayName = "단계4 복귀 LookAt 높이 (cm)", ClampMin = "0.0", ClampMax = "300.0"))
	float Stage4ReturnLookHeight = 90.f;

	/** 단계2 끝 카메라 — 위쪽 (하늘 비춤). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Camera",
		meta = (DisplayName = "단계2 카메라 (위)"))
	FVector TopOffset = FVector(500.f, 500.f, 700.f);

	/** 단계2 LookAt 보스 Z 오프셋 (큰 값 = 카메라가 위로 향함). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Camera",
		meta = (DisplayName = "단계2 LookAt 높이 (cm)", ClampMin = "0.0", ClampMax = "5000.0"))
	float TopLookHeight = 2500.f;

	/** 카메라 viewport 진입 블렌드 시간 (작을수록 즉시 전환). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Camera",
		meta = (DisplayName = "카메라 블렌드 인 (초)", ClampMin = "0.0", ClampMax = "3.0"))
	float CameraBlendIn = 0.1f;

	/** 카메라 viewport 종료 블렌드 시간. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Camera",
		meta = (DisplayName = "카메라 블렌드 아웃 (초)", ClampMin = "0.0", ClampMax = "3.0"))
	float CameraBlendOut = 0.45f;

	// =========================================================================================
	// 단계 시간
	// =========================================================================================

	/** 단계1a Face 고정 시간 (히트 임팩트 동안). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Timing",
		meta = (DisplayName = "단계1a Face 고정 시간(초)", ClampMin = "0.0", ClampMax = "10.0"))
	float FaceDuration = 1.f;

	/** 단계1 전체 (1a + 1b) — Face 고정 후 Front lerp 까지. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Timing",
		meta = (DisplayName = "단계1 기절 시간(초)", ClampMin = "1.0", ClampMax = "15.0"))
	float StunDuration = 6.f;

	/** 단계2 Rise (Front → Top) 시간. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Timing",
		meta = (DisplayName = "단계2 카메라 상승 시간(초)", ClampMin = "0.5", ClampMax = "10.0"))
	float CameraRiseDuration = 5.f;

	/**
	 * [Phase2TopHoldV1] 단계 2 → 4 사이 Top 위치 정지 시간 (초).
	 *   카메라가 위에 도달한 후 정지하면서 강하 VFX 가 펼쳐지는 시간을 보장.
	 *   이 값이 클수록 위에서 보는 hold 가 길어지고 그만큼 Stage 4 시작이 늦어짐.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Timing",
		meta = (DisplayName = "단계3 Top 정지 시간(초)", ClampMin = "0.0", ClampMax = "20.0"))
	float TopHoldDuration = 2.f;

	/** 단계4 Descent (Top → Front) 시간. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Timing",
		meta = (DisplayName = "단계4 카메라 하강 시간(초)", ClampMin = "0.5", ClampMax = "10.0"))
	float CameraDescentDuration = 5.f;

	/** 단계4 끝 후 hold 시간 (전신 보임). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Timing",
		meta = (DisplayName = "단계4 끝 hold 시간(초)", ClampMin = "0.0", ClampMax = "5.0"))
	float EndHoldDuration = 2.f;

	/** 시네마틱 총 길이 (= 보스 무적 시간). 보스의 InvulnerabilityDuration 과 동일하게 set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Timing",
		meta = (DisplayName = "시네마틱 총 길이 (초)", ClampMin = "1.0", ClampMax = "30.0"))
	float TotalCinematicDuration = 19.f;

	/**
	 * [Phase2DescentVFXKillModeV1] 강하 VFX 종료 방식 (cinematic_end + 0.5s).
	 *   true  : DeactivateImmediate — spawn + 기존 particles 즉시 kill (한순간에 사라짐).
	 *   false : Deactivate — spawn 만 정지, 기존 particles 가 lifetime 따라 점진 fade.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|VFX",
		meta = (DisplayName = "강하 VFX 즉시 kill (true=즉시, false=점진 fade)"))
	bool bDescentVFXKillImmediate = true;

	/**
	 * [Phase2DescentFadeSpeedV1] 점진 fade 모드 (bDescentVFXKillImmediate=false) 일 때만 적용.
	 *   Deactivate 직전 NC 의 CustomTimeDilation 을 이 값으로 set → 기존 particles 가
	 *   N배 빠르게 lifetime 진행 → fade 완료 시간 1/N 로 단축. 자연스러운 fade 유지하면서 빠르게.
	 *   1.0 = 원본 (느린 자연 fade)
	 *   3.0 = 3배 빠름 (default)
	 *   5.0+ = 매우 빠름
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|VFX",
		meta = (DisplayName = "강하 VFX fade 속도 배율 (점진 fade 모드 only)", ClampMin = "0.5", ClampMax = "20.0"))
	float DescentVFXFadeSpeed = 3.0f;

	// =========================================================================================
	// 대사
	// =========================================================================================

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Dialogue",
		meta = (DisplayName = "대사 위젯 클래스"))
	TSubclassOf<UBossDialogueWidget> DialogueWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Dialogue",
		meta = (DisplayName = "화자 이름"))
	FText SpeakerName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Dialogue",
		meta = (DisplayName = "1번째 대사 (단계1a)", MultiLine = true))
	FText DialogueLine;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Dialogue",
		meta = (DisplayName = "1번째 대사 표시 시간(초)", ClampMin = "0.5", ClampMax = "15.0"))
	float DialogueDuration = 6.f;

	/** 두 번째 대사 — Stage1b 시점 (FaceDuration 후) 표시. 비우면 표시 안 함. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Dialogue",
		meta = (DisplayName = "2번째 대사 (단계1b 이후)", MultiLine = true))
	FText DialogueLine2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase2|Dialogue",
		meta = (DisplayName = "2번째 대사 표시 시간(초)", ClampMin = "0.5", ClampMax = "15.0"))
	float DialogueLine2Duration = 6.f;

	// =========================================================================================
	// 공개 API
	// =========================================================================================

	/** 시네마틱 활성화 (서버 권한 필요). 보스 페이즈2 진입 시 호출. */
	UFUNCTION(BlueprintCallable, Category = "Phase2")
	bool TryActivate(APawn* InTargetBoss);

	/** Multicast — 모든 머신에서 카메라 + 대사 시작. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StartCinematic(APawn* Boss);

	/** Multicast — 모든 머신에서 시네마틱 종료. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_EndCinematic();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	/** 클라 로컬 카메라 액터. */
	UPROPERTY()
	TWeakObjectPtr<ACameraActor> LocalCameraActor;

	/** 클라 로컬 대사 위젯. */
	UPROPERTY()
	TObjectPtr<UBossDialogueWidget> LocalDialogueWidget;

	/** 시네마틱 대상 보스 (서버/클라 모두 보존). */
	UPROPERTY()
	TWeakObjectPtr<APawn> ActiveBoss;

	/** 카메라 lerp 진행 중 여부. */
	bool bCameraInterpolating = false;

	/** 단계1a 동안 true — 카메라가 보스 추적 안 하고 spawn 위치 그대로 (가만히). */
	bool bCameraHoldStaticDuringFace = true;

	/** 카메라 lerp 누적 시간. */
	float CamInterpElapsed = 0.f;

	/** 카메라 lerp from/to/duration. */
	FVector CamLerpFromOffset = FVector::ZeroVector;
	FVector CamLerpToOffset = FVector::ZeroVector;
	float CamLerpFromLookH = 0.f;
	float CamLerpToLookH = 0.f;
	float CamLerpDuration = 1.f;

	/** 단계 timer 핸들. */
	FTimerHandle Stage1bTimer;
	FTimerHandle Stage2Timer;
	FTimerHandle Stage4Timer;
	FTimerHandle EndCinematicTimer;
	FTimerHandle DialogueHideTimer;
};
