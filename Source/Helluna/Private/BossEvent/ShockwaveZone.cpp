// Capstone Project Helluna

#include "BossEvent/ShockwaveZone.h"

#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaHeroCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

#define SWZ_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[ShockwaveZone] " Fmt), ##__VA_ARGS__)

AShockwaveZone::AShockwaveZone()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
}

// -----------------------------------------------------------------
// ActivateZone
// -----------------------------------------------------------------
void AShockwaveZone::ActivateZone()
{
	Super::ActivateZone();

	if (bZoneActive) return;
	bZoneActive = true;
	WavesFired = 0;
	ActiveWaves.Empty();

	SWZ_LOG("=== ActivateZone (WaveCount=%d, Interval=%.2f, Speed=%.0f) ===",
		WaveCount, WaveInterval, WaveSpeed);

	// 첫 파동 즉시 발사
	FireNextWave();

	// 나머지 파동은 타이머로 발사
	if (WaveCount > 1)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				WaveSpawnTimerHandle,
				this,
				&AShockwaveZone::FireNextWave,
				WaveInterval,
				true
			);
		}
	}

	SetActorTickEnabled(true);
}

// -----------------------------------------------------------------
// DeactivateZone
// -----------------------------------------------------------------
void AShockwaveZone::DeactivateZone()
{
	Super::DeactivateZone();

	SWZ_LOG("=== DeactivateZone ===");

	bZoneActive = false;
	SetActorTickEnabled(false);
	ActiveWaves.Empty();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WaveSpawnTimerHandle);
	}
}

// -----------------------------------------------------------------
// FireNextWave — 새 파동 발사
// -----------------------------------------------------------------
void AShockwaveZone::FireNextWave()
{
	if (!bZoneActive) return;

	if (WavesFired >= WaveCount)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(WaveSpawnTimerHandle);
		}
		return;
	}

	SWZ_LOG("Fire wave %d/%d", WavesFired + 1, WaveCount);

	// 새 파동 추가
	FActiveWave NewWave;
	NewWave.CurrentRadius = 0.f;
	ActiveWaves.Add(MoveTemp(NewWave));

	// VFX
	if (WaveVFX && OwnerEnemy)
	{
		OwnerEnemy->MulticastPlayEffect(GetActorLocation(), WaveVFX, WaveVFXScale, false);
	}

	// 사운드
	if (WaveSound && OwnerEnemy)
	{
		OwnerEnemy->Multicast_PlaySoundAtLocation(WaveSound, GetActorLocation());
	}

	WavesFired++;
}

// -----------------------------------------------------------------
// Tick — 파동 확장 + 피격 처리
// -----------------------------------------------------------------
void AShockwaveZone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 게임플레이 로직(데미지/VFX 트리거)은 서버에서만. 클라는 멀티캐스트 수신만.
	if (!HasAuthority()) return;

	if (!bZoneActive && ActiveWaves.Num() == 0)
	{
		SetActorTickEnabled(false);
		return;
	}

	// 뒤에서부터 순회 (중간 삭제 안전)
	for (int32 i = ActiveWaves.Num() - 1; i >= 0; --i)
	{
		FActiveWave& Wave = ActiveWaves[i];

		// 반지름 확장
		Wave.CurrentRadius += WaveSpeed * DeltaTime;

		// 최대 반지름 초과 시 제거
		if (Wave.CurrentRadius > MaxRadius)
		{
			ActiveWaves.RemoveAt(i);
			continue;
		}

		// 피격 판정
		ProcessWaveHits(Wave);
	}

	CheckPatternComplete();
}

// -----------------------------------------------------------------
// ProcessWaveHits — 링 범위 내 플레이어 데미지
// -----------------------------------------------------------------
void AShockwaveZone::ProcessWaveHits(FActiveWave& Wave)
{
	UWorld* World = GetWorld();
	if (!World || !OwnerEnemy) return;

	const FVector Center = GetActorLocation();
	const float OuterRadius = Wave.CurrentRadius + WaveThickness * 0.5f;
	const float InnerRadius = FMath::Max(0.f, Wave.CurrentRadius - WaveThickness * 0.5f);

	// 외부 반지름으로 Overlap 검사
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(OwnerEnemy);

	World->OverlapMultiByObjectType(
		Overlaps,
		Center,
		FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(OuterRadius),
		QueryParams
	);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AHellunaHeroCharacter* Player = Cast<AHellunaHeroCharacter>(Overlap.GetActor());
		if (!IsValid(Player)) continue;

		// 이미 이 파동에 맞은 액터는 스킵
		if (Wave.AlreadyHitActors.Contains(Player)) continue;

		const FVector PlayerLoc = Player->GetActorLocation();

		// 높이 검사
		const float HeightDiff = FMath::Abs(PlayerLoc.Z - Center.Z);
		if (HeightDiff > WaveHeight) continue;

		// 수평 거리로 링 범위 검사
		const float HorizontalDist = FVector::Dist2D(Center, PlayerLoc);
		if (HorizontalDist < InnerRadius || HorizontalDist > OuterRadius) continue;

		// 점프 회피 — 공중에 있고 발밑이 파동보다 높으면 회피 성공
		if (bCanJumpDodge)
		{
			UCharacterMovementComponent* CMC = Player->GetCharacterMovement();
			if (CMC && CMC->IsFalling())
			{
				// 발밑 높이 = 액터 위치 - 캡슐 반높이
				const float FeetZ = PlayerLoc.Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				const float WaveTopZ = Center.Z + JumpDodgeMinHeight;

				if (FeetZ > WaveTopZ)
				{
					SWZ_LOG("Wave DODGED by [%s] (jump, feet=%.0f, waveTop=%.0f)",
						*Player->GetName(), FeetZ, WaveTopZ);
					// 회피 성공 — HitActors에 추가하지 않음 → 다음 프레임에 착지하면 맞을 수 있음
					continue;
				}
			}
		}

		// 피격!
		Wave.AlreadyHitActors.Add(Player);

		UGameplayStatics::ApplyDamage(
			Player,
			WaveDamage,
			OwnerEnemy->GetController(),
			OwnerEnemy,
			UDamageType::StaticClass()
		);

		SWZ_LOG("Wave hit [%s] at dist=%.0f (ring=%.0f~%.0f)",
			*Player->GetName(), HorizontalDist, InnerRadius, OuterRadius);
	}
}

// -----------------------------------------------------------------
// CheckPatternComplete
// -----------------------------------------------------------------
void AShockwaveZone::CheckPatternComplete()
{
	// 모든 파동이 발사되고, 모든 활성 파동이 소멸했으면 패턴 종료
	if (WavesFired >= WaveCount && ActiveWaves.Num() == 0)
	{
		SWZ_LOG("=== All waves complete, pattern finished ===");
		bZoneActive = false;
		SetActorTickEnabled(false);
		NotifyPatternFinished(false);
	}
}

#undef SWZ_LOG
