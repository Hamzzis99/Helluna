// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BossDeathCinematicTrigger.generated.h"

class APawn;
class ACameraActor;
class UCameraShakeBase;

/**
 * [BossDeathCinematicV1] 보스 사망 시네마틱 트리거.
 *
 * BossPhase2CinematicTrigger 와 동일 패턴 — 보스 사망 시점에 spawn + TryActivate.
 * 페이즈2 시네마틱 첫 컷(얼굴 클로즈업)과 동일 구도, 단 카메라가 보스의 head 본을
 * 매 Tick 추적하여 사망 모션(쓰러짐/리액션)에 따라 얼굴을 따라간다.
 *
 * 단일 카메라 — face close-up hold during TotalCinematicDuration. lerp 없음.
 */
UCLASS(Blueprintable)
class HELLUNA_API ABossDeathCinematicTrigger : public AActor
{
	GENERATED_BODY()

public:
	ABossDeathCinematicTrigger();

	// =========================================================================================
	// 카메라 위치 / LookAt — Phase2 첫 컷 (FaceOffset / FaceLookHeight) 과 같은 기본값
	// =========================================================================================

	/**
	 * 카메라 오프셋 — 보스 로컬 축 기준 (X 정면, Y 우측, Z 위).
	 * Phase2 단계1a 와 동일 기본값.
	 *
	 * 앵커는 ActorLocation 이 아니라 추적 본(HeadBoneName) 의 월드 좌표.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossDeath|Camera",
		meta = (DisplayName = "카메라 오프셋 (보스 로컬, 머리 기준)"))
	FVector FaceOffset = FVector(50.f, -50.f, 0.f);

	/**
	 * LookAt 보정 — 추적 본 위치에 더하는 월드 Z 오프셋 (cm).
	 *  0 이면 head 본 직시. +값이면 약간 위쪽, -값이면 약간 아래쪽.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossDeath|Camera",
		meta = (DisplayName = "LookAt Z 오프셋 (cm)", ClampMin = "-200.0", ClampMax = "200.0"))
	float LookAtZOffset = 0.f;

	/** 추적할 본 / 소켓 이름 (UE5 Manny: "head"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossDeath|Camera",
		meta = (DisplayName = "추적 본 / 소켓 이름"))
	FName HeadBoneName = FName(TEXT("head"));

	/**
	 * [HeadFollowV1] 카메라가 head 본을 얼마나 따라가는지 (0~1).
	 *  1.0 : head 본 위치를 100% 추적 (사망 모션에 카메라가 함께 흔들림).
	 *  0.5 : head 본과 ActorLocation+90 의 중간 추적 (절반만 따라감 — 부드럽게).
	 *  0.0 : 추적 안 함 (ActorLocation 기준 고정 — Phase2 첫 컷과 완전히 동일 거동).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossDeath|Camera",
		meta = (DisplayName = "Head 추적 강도 (0=정지, 1=완전 추적)", ClampMin = "0.0", ClampMax = "1.0"))
	float HeadFollowAlpha = 0.6f;

	/** 카메라 viewport 진입 블렌드 시간. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossDeath|Camera",
		meta = (DisplayName = "카메라 블렌드 인 (초)", ClampMin = "0.0", ClampMax = "3.0"))
	float CameraBlendIn = 0.5f;

	/** 카메라 viewport 종료 블렌드 시간. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossDeath|Camera",
		meta = (DisplayName = "카메라 블렌드 아웃 (초)", ClampMin = "0.0", ClampMax = "3.0"))
	float CameraBlendOut = 0.6f;

	/** 시네마틱 총 길이 (초). 이 시간 후 ExitBossCinematic + 카메라 destroy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossDeath|Timing",
		meta = (DisplayName = "시네마틱 총 길이 (초)", ClampMin = "0.5", ClampMax = "30.0"))
	float TotalCinematicDuration = 5.f;

	// =========================================================================================
	// 공개 API
	// =========================================================================================

	/** 시네마틱 활성화 (서버 권한 필요). 보스 사망 시 호출. */
	UFUNCTION(BlueprintCallable, Category = "BossDeath")
	bool TryActivate(APawn* InTargetBoss);

	/** Multicast — 모든 머신에서 카메라 시작. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StartCinematic(APawn* Boss);

	/** Multicast — 모든 머신에서 시네마틱 종료. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_EndCinematic();

	/**
	 * [BPDefaultSyncV1] BeginPlay 시 BP CDO 의 Edit-가능 property 를 instance 에 강제 sync.
	 *   placement instance override 가 BP CDO 변경을 가리는 문제 fix.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossDeath|Sync",
		meta = (DisplayName = "BP CDO 자동 동기화"))
	bool bSyncFromBPDefault = true;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** [BPDefaultSyncV1] BP CDO 값을 instance 에 복사. */
	void SyncFromBPDefault();

private:
	/** 클라 로컬 카메라 액터. */
	UPROPERTY()
	TWeakObjectPtr<ACameraActor> LocalCameraActor;

	/** 시네마틱 대상 보스. */
	UPROPERTY()
	TWeakObjectPtr<APawn> ActiveBoss;

	/** 종료 timer. */
	FTimerHandle EndCinematicTimer;
};
