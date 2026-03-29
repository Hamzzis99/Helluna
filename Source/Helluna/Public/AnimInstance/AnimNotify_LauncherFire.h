// Capstone Project Helluna - Launcher Fire Notify

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_LauncherFire.generated.h"

/**
 * 런처 발사 몽타주의 투사체 생성 프레임.
 * 서버 권한이 있는 경우에만 Weapon->Fire()를 호출한다.
 */
UCLASS()
class HELLUNA_API UAnimNotify_LauncherFire : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
