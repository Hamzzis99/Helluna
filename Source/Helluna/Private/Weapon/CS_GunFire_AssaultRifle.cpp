#include "Weapon/CS_GunFire_AssaultRifle.h"
#include "Shakes/PerlinNoiseCameraShakePattern.h"

UCS_GunFire_AssaultRifle::UCS_GunFire_AssaultRifle(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSingleInstance = true;

	UPerlinNoiseCameraShakePattern* Pattern =
		CreateDefaultSubobject<UPerlinNoiseCameraShakePattern>(TEXT("Pattern"));

	// -- 타이밍 --
	// 연사에 맞게 짧고 빠르게 — 연속 발사 시 자연스럽게 겹침
	Pattern->Duration      = 0.1f;
	Pattern->BlendInTime   = 0.01f;
	Pattern->BlendOutTime  = 0.05f;

	// -- 위치 진동 --
	// 가볍지만 빠른 떨림
	Pattern->LocationAmplitudeMultiplier = 1.f;
	Pattern->LocationFrequencyMultiplier = 1.f;

	Pattern->X.Amplitude = 0.2f;
	Pattern->X.Frequency = 30.f;

	Pattern->Y.Amplitude = 0.15f;
	Pattern->Y.Frequency = 25.f;

	Pattern->Z.Amplitude = 0.4f;
	Pattern->Z.Frequency = 35.f;

	// -- 회전 진동 --
	// 연사 시 미세한 흔들림
	Pattern->RotationAmplitudeMultiplier = 1.f;
	Pattern->RotationFrequencyMultiplier = 1.f;

	Pattern->Pitch.Amplitude = 0.25f;
	Pattern->Pitch.Frequency = 25.f;

	Pattern->Yaw.Amplitude   = 0.1f;
	Pattern->Yaw.Frequency   = 20.f;

	Pattern->Roll.Amplitude  = 0.03f;
	Pattern->Roll.Frequency  = 15.f;

	// -- FOV --
	Pattern->FOV.Amplitude = 0.f;

	SetRootShakePattern(Pattern);
}
