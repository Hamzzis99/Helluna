// Capstone Project Helluna - Melee Kick Impact Notify

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_KickImpact.generated.h"

/**
 * 발차기 몽타주의 임팩트 프레임.
 * Hero ASC에 Event.Kick.Impact GameplayEvent를 전송해서
 * GA_MeleeKick이 정확한 프레임에 데미지 + AOE 넉백을 수행하게 한다.
 */
UCLASS()
class HELLUNA_API UAnimNotify_KickImpact : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
