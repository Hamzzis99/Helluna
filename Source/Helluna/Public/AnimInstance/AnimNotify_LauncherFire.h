// Capstone Project Helluna - Launcher Fire Notify

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_LauncherFire.generated.h"

/**
 * 발사 몽타주의 실제 발사 프레임.
 * 서버: GunBase의 FireWithAimPoint(라인트레이스) 호출.
 * 로컬: 반동(Recoil) 적용.
 * 모든 GunBase 계열 무기에서 공용으로 사용 가능.
 */
UCLASS()
class HELLUNA_API UAnimNotify_LauncherFire : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
