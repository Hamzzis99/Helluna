/**
 * CS_OreMining.h
 *
 * 광석 채굴 시 재생할 카메라 쉐이크.
 * OreMiningEffectComponent의 MiningCameraShake에 이 클래스를 설정하면 된다.
 *
 * 곡괭이로 돌을 내려치는 짧고 묵직한 충격감.
 * Z축(위아래) 진동을 강조하고, 회전은 약하게 줘서 반동만 느끼게 한다.
 */

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "CS_OreMining.generated.h"

UCLASS()
class HELLUNA_API UCS_OreMining : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UCS_OreMining(const FObjectInitializer& ObjectInitializer);
};
