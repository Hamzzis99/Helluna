// Capstone Project Helluna - Gun Parry Execution Fire Notify

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_ParryExecutionFire.generated.h"

/**
 * 건패링 처형 몽타주의 총 발사 프레임.
 * Hero ASC에 Event.Parry.Fire GameplayEvent를 전송해서
 * GA_GunParry가 정확한 프레임에 킬 처리와 카메라 셰이크를 수행하게 한다.
 */
UCLASS()
class HELLUNA_API UAnimNotify_ParryExecutionFire : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
