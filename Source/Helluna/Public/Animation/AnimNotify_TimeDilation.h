/**
 * AnimNotify_TimeDilation.h
 *
 * 보스 사망 등 임팩트 프레임에 일시적인 슬로우 모션을 발생시키는 AnimNotify.
 *
 * 동작:
 *  - 사망 몽타주는 Multicast_PlayDeath 로 모든 머신에서 재생되므로 노티도 각 머신에서 로컬 발화.
 *  - 각 머신이 SetGlobalTimeDilation 으로 자기 로컬 월드를 슬로우.
 *  - 데디 서버에서는 서버 시뮬 자체를 늦춰 모든 액터의 시뮬 속도가 동기화됨.
 *  - 지정한 Duration(언슬로우 기준) 뒤 타이머로 1.0 으로 자동 복구.
 *  - 복구는 머신 로컬 타이머를 사용하므로 슬로우 시간만큼 실시간으로 길어진다.
 *
 * 사용 예: 보스 사망 몽타주의 "최후의 일격" 프레임에 추가 → 짧게 시간이 늘어졌다 복구.
 */

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_TimeDilation.generated.h"

UCLASS(meta = (DisplayName = "Time: Global Time Dilation Burst"))
class HELLUNA_API UAnimNotify_TimeDilation : public UAnimNotify
{
	GENERATED_BODY()

public:
	/** 적용할 글로벌 타임 딜레이션 (1.0=정상, 0.2=5배 슬로우). */
	UPROPERTY(EditAnywhere, Category = "타임 딜레이션",
		meta = (DisplayName = "딜레이션 값", ClampMin = "0.05", ClampMax = "1.0"))
	float Dilation = 0.2f;

	/**
	 * 슬로우가 유지되는 시간 (초, "언슬로우 시간" 기준 = 슬로우 적용된 실시간이 아닌
	 * Real-time 으로 측정). 끝나면 1.0 으로 자동 복구.
	 */
	UPROPERTY(EditAnywhere, Category = "타임 딜레이션",
		meta = (DisplayName = "지속 시간 (초)", ClampMin = "0.05", ClampMax = "5.0"))
	float Duration = 0.6f;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
};
