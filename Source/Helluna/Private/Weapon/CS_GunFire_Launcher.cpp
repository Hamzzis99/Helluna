#include "Weapon/CS_GunFire_Launcher.h"
#include "Shakes/PerlinNoiseCameraShakePattern.h"

UCS_GunFire_Launcher::UCS_GunFire_Launcher(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSingleInstance = true;

	UPerlinNoiseCameraShakePattern* Pattern =
		CreateDefaultSubobject<UPerlinNoiseCameraShakePattern>(TEXT("Pattern"));

	// -- 타이밍 --
	// 가장 긴 지속시간 — 폭발 무기의 강력한 반동
	Pattern->Duration      = 0.35f;
	Pattern->BlendInTime   = 0.01f;
	Pattern->BlendOutTime  = 0.2f;

	// -- 위치 진동 --
	// 전체적으로 크고 무거운 흔들림
	Pattern->LocationAmplitudeMultiplier = 1.f;
	Pattern->LocationFrequencyMultiplier = 1.f;

	Pattern->X.Amplitude = 1.2f;
	Pattern->X.Frequency = 15.f;      // 낮은 주파수 — 무거운 느낌

	Pattern->Y.Amplitude = 0.8f;
	Pattern->Y.Frequency = 12.f;

	Pattern->Z.Amplitude = 2.0f;      // 최대 진폭
	Pattern->Z.Frequency = 20.f;

	// -- 회전 진동 --
	// 강한 회전 — 발사 후 흔들리는 느낌
	Pattern->RotationAmplitudeMultiplier = 1.f;
	Pattern->RotationFrequencyMultiplier = 1.f;

	Pattern->Pitch.Amplitude = 1.0f;
	Pattern->Pitch.Frequency = 15.f;

	Pattern->Yaw.Amplitude   = 0.5f;
	Pattern->Yaw.Frequency   = 12.f;

	Pattern->Roll.Amplitude  = 0.2f;
	Pattern->Roll.Frequency  = 10.f;

	// -- FOV --
	// FOV 펀치로 폭발 충격감
	Pattern->FOV.Amplitude = 1.5f;
	Pattern->FOV.Frequency = 12.f;

	SetRootShakePattern(Pattern);
}
