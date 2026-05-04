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
