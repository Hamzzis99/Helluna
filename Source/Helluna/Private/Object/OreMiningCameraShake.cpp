#include "Object/OreMiningCameraShake.h"
#include "Shakes/PerlinNoiseCameraShakePattern.h"

UOreMiningCameraShake::UOreMiningCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSingleInstance = true;

	UPerlinNoiseCameraShakePattern* Pattern = CreateDefaultSubobject<UPerlinNoiseCameraShakePattern>(TEXT("Pattern"));

	// ── 타이밍 ──────────────────────────────────────────
	Pattern->Duration    = 0.2f;    // 짧고 묵직한 타격감
	Pattern->BlendInTime  = 0.02f;   // 즉시 시작
	Pattern->BlendOutTime = 0.1f;    // 자연스럽게 감쇠

	// ── 위치 진동 (Location) ────────────────────────────
	// Z축(위아래)을 강조 → 곡괭이 내려치는 충격
	Pattern->LocationAmplitudeMultiplier = 1.f;
	Pattern->LocationFrequencyMultiplier = 1.f;

	Pattern->X.Amplitude = 1.0f;
	Pattern->X.Frequency = 25.f;

	Pattern->Y.Amplitude = 0.5f;
	Pattern->Y.Frequency = 25.f;

	Pattern->Z.Amplitude = 1.5f;   // 가장 강한 축
	Pattern->Z.Frequency = 30.f;

	// ── 회전 진동 (Rotation) ────────────────────────────
	// 약한 회전 → 손에 전달되는 반동 느낌
	Pattern->RotationAmplitudeMultiplier = 1.f;
	Pattern->RotationFrequencyMultiplier = 1.f;

	Pattern->Pitch.Amplitude = 0.5f;
	Pattern->Pitch.Frequency = 20.f;

	Pattern->Yaw.Amplitude   = 0.3f;
	Pattern->Yaw.Frequency   = 15.f;

	Pattern->Roll.Amplitude  = 0.2f;
	Pattern->Roll.Frequency  = 10.f;

	// ── FOV ─────────────────────────────────────────────
	Pattern->FOV.Amplitude = 0.f;  // FOV 변경 없음

	SetRootShakePattern(Pattern);
}
