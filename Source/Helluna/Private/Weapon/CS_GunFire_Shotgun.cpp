#include "Weapon/CS_GunFire_Shotgun.h"
#include "Shakes/PerlinNoiseCameraShakePattern.h"

UCS_GunFire_Shotgun::UCS_GunFire_Shotgun(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSingleInstance = true;

	UPerlinNoiseCameraShakePattern* Pattern =
		CreateDefaultSubobject<UPerlinNoiseCameraShakePattern>(TEXT("Pattern"));

	// -- 타이밍 --
	// 단발 — 강하고 묵직한 충격, 빠르게 감쇠
	Pattern->Duration      = 0.25f;
	Pattern->BlendInTime   = 0.01f;
	Pattern->BlendOutTime  = 0.15f;

	// -- 위치 진동 --
	// 강한 반동 느낌, Z축(위아래) 대폭 강조
	Pattern->LocationAmplitudeMultiplier = 1.f;
	Pattern->LocationFrequencyMultiplier = 1.f;

	Pattern->X.Amplitude = 0.8f;
	Pattern->X.Frequency = 20.f;

	Pattern->Y.Amplitude = 0.6f;
	Pattern->Y.Frequency = 18.f;

	Pattern->Z.Amplitude = 1.5f;      // 가장 강한 축 — 위로 튀는 느낌
	Pattern->Z.Frequency = 25.f;

	// -- 회전 진동 --
	// 묵직한 회전 반동
	Pattern->RotationAmplitudeMultiplier = 1.f;
	Pattern->RotationFrequencyMultiplier = 1.f;

	Pattern->Pitch.Amplitude = 0.7f;
	Pattern->Pitch.Frequency = 18.f;

	Pattern->Yaw.Amplitude   = 0.4f;
	Pattern->Yaw.Frequency   = 15.f;

	Pattern->Roll.Amplitude  = 0.15f;
	Pattern->Roll.Frequency  = 12.f;

	// -- FOV --
	// 살짝 FOV 펀치로 충격감 강화
	Pattern->FOV.Amplitude = 1.0f;
	Pattern->FOV.Frequency = 15.f;

	SetRootShakePattern(Pattern);
}
