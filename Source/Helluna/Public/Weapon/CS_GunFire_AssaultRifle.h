/**
 * CS_GunFire_AssaultRifle.h
 *
 * 돌격소총 발사 카메라 쉐이크.
 * 짧고 빠른 진동 — 연사 시 지속적인 흔들림.
 */

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "CS_GunFire_AssaultRifle.generated.h"

UCLASS()
class HELLUNA_API UCS_GunFire_AssaultRifle : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UCS_GunFire_AssaultRifle(const FObjectInitializer& ObjectInitializer);
};
