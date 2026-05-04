// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_ActorTimeDilation.generated.h"

/**
 * 액터별 슬로우 모션 — Notify 시작/끝 시간 사이에만 액터의 CustomTimeDilation 적용.
 *
 * 동작:
 *  - NotifyBegin → MeshComp 의 Owner.CustomTimeDilation = TimeDilation
 *  - NotifyEnd → 1.0 복원
 *  - 글로벌 시간 안 건드림 → 카메라/환경 정상 속도, 액터만 슬로우.
 *
 * 사용 예: 보스 광폭화 시네마틱의 anim_hit 부분에 drag 해서 정확한 frame 범위만 슬로우.
 */
UCLASS(meta = (DisplayName = "Time: Actor Slow-Mo (Range)"))
class HELLUNA_API UAnimNotifyState_ActorTimeDilation : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	/** 적용할 액터 CustomTimeDilation (1.0=정상, 0.3=강한 슬로우). */
	UPROPERTY(EditAnywhere, Category = "Slow-Mo",
		meta = (DisplayName = "타임 딜레이션", ClampMin = "0.05", ClampMax = "1.0"))
	float TimeDilation = 0.3f;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
};
