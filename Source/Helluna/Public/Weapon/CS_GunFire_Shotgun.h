/**
 * CS_GunFire_Shotgun.h
 *
 * 샷건 발사 카메라 쉐이크.
 * 강하고 묵직한 단발 충격 — 큰 진폭, 빠른 감쇠.
 */

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "CS_GunFire_Shotgun.generated.h"

UCLASS()
class HELLUNA_API UCS_GunFire_Shotgun : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UCS_GunFire_Shotgun(const FObjectInitializer& ObjectInitializer);
};
