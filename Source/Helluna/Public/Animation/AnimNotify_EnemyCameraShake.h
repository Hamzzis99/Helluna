/**
 * AnimNotify_EnemyCameraShake.h
 *
 * 보스 내려찍기 등 임팩트 프레임에 월드 카메라 쉐이크를 발생시키는 노티.
 *
 * 동작:
 *  - 몽타주는 서버+클라 모두에서 재생되므로 이 노티도 각 머신에서 로컬 발화.
 *  - 각 머신이 자기 로컬 PlayerController 카메라에 대해 UGameplayStatics::PlayWorldCameraShake 호출.
 *  - 네트워크 RPC 불필요 — 각자 자기 카메라 기준으로 Origin / 반지름 / Falloff 로 판정.
 *  - 데디케이티드 서버에서는 렌더 카메라 없으므로 스킵.
 *
 * 사용 예: 보스 내려찍기 몽타주의 "쾅" 프레임에 추가 →
 *         주변 플레이어만 거리에 비례한 쉐이크를 받음.
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_EnemyCameraShake.generated.h"

class UCameraShakeBase;

UCLASS(meta = (DisplayName = "Enemy: World Camera Shake"))
class HELLUNA_API UAnimNotify_EnemyCameraShake : public UAnimNotify
{
	GENERATED_BODY()

public:
	/** 재생할 CameraShake 클래스 (BP). */
	UPROPERTY(EditAnywhere, Category = "카메라쉐이크",
		meta = (DisplayName = "쉐이크 클래스"))
	TSubclassOf<UCameraShakeBase> ShakeClass;

	/**
	 * Origin 기준 본/소켓 이름.
	 * 비어있으면 액터 루트 위치 사용. 보통 "foot_l" / "foot_r" / "Pelvis" 가 자연스러움.
	 */
	UPROPERTY(EditAnywhere, Category = "카메라쉐이크",
		meta = (DisplayName = "기준 소켓/본 이름"))
	FName SocketName = NAME_None;

	/** Origin 에서의 월드 공간 오프셋 (cm). */
	UPROPERTY(EditAnywhere, Category = "카메라쉐이크",
		meta = (DisplayName = "위치 오프셋 (cm)"))
	FVector LocationOffset = FVector::ZeroVector;

	/**
	 * 이 거리 안쪽은 풀 강도 쉐이크.
	 * 예: 300cm → 보스 근처 3m 안에서 가장 강하게 흔들림.
	 */
	UPROPERTY(EditAnywhere, Category = "카메라쉐이크",
		meta = (DisplayName = "Inner Radius (cm)", ClampMin = "0.0"))
	float InnerRadius = 300.f;

	/**
	 * 이 거리 바깥은 쉐이크 없음.
	 * Inner ~ Outer 사이에서 Falloff 곡선으로 감쇠.
	 */
	UPROPERTY(EditAnywhere, Category = "카메라쉐이크",
		meta = (DisplayName = "Outer Radius (cm)", ClampMin = "0.0"))
	float OuterRadius = 2000.f;

	/**
	 * 거리별 감쇠 지수. 1.0 = 선형, >1 = 가까울수록 집중, <1 = 넓게 퍼짐.
	 */
	UPROPERTY(EditAnywhere, Category = "카메라쉐이크",
		meta = (DisplayName = "Falloff", ClampMin = "0.1", ClampMax = "10.0"))
	float Falloff = 1.f;

	/** 서버가 Multicast 대신 각 머신 로컬 발화 — 화면이 있는 머신만 발동. */
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
};
