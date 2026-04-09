/**
 * CS_GunFire_Default.h
 *
 * 총기 발사 시 기본 카메라 쉐이크.
 * GunBase의 FireCameraShake에 설정하면 발사마다 재생된다.
 * 범용으로 쓸 수 있는 가벼운 쉐이크.
 */

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "CS_GunFire_Default.generated.h"

UCLASS()
class HELLUNA_API UCS_GunFire_Default : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UCS_GunFire_Default(const FObjectInitializer& ObjectInitializer);
};
