#include "Weapon/CS_GunFire_Default.h"
#include "Shakes/PerlinNoiseCameraShakePattern.h"

UCS_GunFire_Default::UCS_GunFire_Default(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSingleInstance = true;

	UPerlinNoiseCameraShakePattern* Pattern =
		CreateDefaultSubobject<UPerlinNoiseCameraShakePattern>(TEXT("Pattern"));

	// -- 타이밍 --
	Pattern->Duration      = 0.15f;   // 짧은 단발 충격
	Pattern->BlendInTime   = 0.01f;   // 즉시 시작
	Pattern->BlendOutTime  = 0.08f;   // 빠른 감쇠

	// -- 위치 진동 --
	Pattern->LocationAmplitudeMultiplier = 1.f;
	Pattern->LocationFrequencyMultiplier = 1.f;

	Pattern->X.Amplitude = 0.3f;
	Pattern->X.Frequency = 20.f;

	Pattern->Y.Amplitude = 0.2f;
	Pattern->Y.Frequency = 18.f;

	Pattern->Z.Amplitude = 0.5f;      // 위아래 약간 강조
	Pattern->Z.Frequency = 25.f;

	// -- 회전 진동 --
	Pattern->RotationAmplitudeMultiplier = 1.f;
	Pattern->RotationFrequencyMultiplier = 1.f;

	Pattern->Pitch.Amplitude = 0.3f;
	Pattern->Pitch.Frequency = 18.f;

	Pattern->Yaw.Amplitude   = 0.15f;
	Pattern->Yaw.Frequency   = 15.f;

	Pattern->Roll.Amplitude  = 0.05f;
	Pattern->Roll.Frequency  = 10.f;

	// -- FOV --
	Pattern->FOV.Amplitude = 0.f;

	SetRootShakePattern(Pattern);
}
