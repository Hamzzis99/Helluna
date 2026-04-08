/**
 * CS_GunFire_Launcher.h
 *
 * 런처(유탄발사기) 발사 카메라 쉐이크.
 * 가장 강력한 반동 — 넓은 진폭, 긴 지속, 깊은 FOV 펀치.
 */

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "CS_GunFire_Launcher.generated.h"

UCLASS()
class HELLUNA_API UCS_GunFire_Launcher : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UCS_GunFire_Launcher(const FObjectInitializer& ObjectInitializer);
};
