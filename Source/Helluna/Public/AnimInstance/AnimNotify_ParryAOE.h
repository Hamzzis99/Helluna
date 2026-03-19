// Capstone Project Helluna - Gun Parry Phase 2

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_ParryAOE.generated.h"

/**
 * [Phase 2] 샷건 패링 광역 Stagger 노티파이
 *
 * 처형 몽타주의 특정 프레임에 배치.
 * 발동 시 주변 Enemy.Type.Humanoid 태그를 가진 적들에게
 * Enemy.State.Staggered 상태를 부여한다.
 *
 * 현재는 빈 껍데기 — 추후 구현 예정.
 */
UCLASS()
class HELLUNA_API UAnimNotify_ParryAOE : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	/** 광역 범위 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ParryAOE",
		meta = (DisplayName = "광역 범위", ClampMin = "100.0", ClampMax = "500.0"))
	float AOERadius = 300.f;
};
