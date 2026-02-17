// Capstone Project Helluna


#include "Weapon/HeroWeapon_Shotgun.h"
#include "Kismet/GameplayStatics.h"

#include "DebugHelper.h"

void AHeroWeapon_Shotgun::Fire(AController* InstigatorController)
{
	// ✅ 서버 전용
	if (!HasAuthority())
	{
		return;
	}

	if (!InstigatorController)
		return;

	APawn* Pawn = InstigatorController->GetPawn();
	if (!Pawn)
		return;

	// ✅ 탄약 체크
	if (!CanFire())
		return;

	// 카메라 기준 발사 시점
	FVector ViewLoc;
	FRotator ViewRot;
	InstigatorController->GetPlayerViewPoint(ViewLoc, ViewRot);

	const FVector TraceStart = ViewLoc;

	// ✅ 샷건은 "한 번 발사에 탄 1발만"
	CurrentMag = FMath::Max(0, CurrentMag - 1);
	BroadcastAmmoChanged();

	// ✅ 펠릿 처리(데미지 + FX 묶음)
	DoLineTraceAndDamage_Shotgun(InstigatorController, TraceStart);
}

// ------------------------------------------------------------
// ✅ 샷건 전용 라인트레이스 + 데미지 + FX 데이터 모으기
// - 펠릿마다 Multicast를 쏘면 Unreliable가 드랍돼서 "2발만 보임" 같은 문제가 생김
// - 그래서 FX는 배열로 모아서 Multicast 1회만 보냄
// ------------------------------------------------------------
void AHeroWeapon_Shotgun::DoLineTraceAndDamage_Shotgun(
	AController* InstigatorController,
	const FVector& TraceStart
)
{
	UWorld* World = GetWorld();
	if (!World || !InstigatorController)
		return;

	// 카메라 기준 Forward 재계산 (TraceStart만 받으니까 여기서 다시 뽑음)
	FVector ViewLoc;
	FRotator ViewRot;
	InstigatorController->GetPlayerViewPoint(ViewLoc, ViewRot);

	const FVector Forward = ViewRot.Vector();
	const float HalfAngleRad = FMath::DegreesToRadians(SpreadHalfAngleDeg);
	const float JitterRad = FMath::DegreesToRadians(JitterDeg);

	// Forward 기준 Right/Up
	FVector Right, Up;
	Forward.FindBestAxisVectors(Right, Up);

	// 골든 앵글(해바라기 패턴)
	static constexpr float GoldenAngle = 2.399963229728653f; // rad

	// FX 묶음 전송용 데이터
	TArray<FVector_NetQuantize> TraceEnds;
	TArray<uint8> HitFlags;
	TArray<FVector_NetQuantize> HitLocations;
	TraceEnds.Reserve(PelletCount);
	HitFlags.Reserve(PelletCount);
	HitLocations.Reserve(PelletCount);

	// 라인트레이스 파라미터(기존 GunBase와 동일 철학)
	FCollisionQueryParams Params(SCENE_QUERY_STAT(Shotgun_LineTrace), false);
	Params.AddIgnoredActor(this);

	if (APawn* Pawn = InstigatorController->GetPawn())
	{
		Params.AddIgnoredActor(Pawn);
	}

	for (int32 i = 0; i < PelletCount; ++i)
	{
		// ----- 패턴 + 약간 랜덤 지터 -----
		const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(PelletCount);
		const float r = FMath::Sqrt(t);

		const float baseTheta = GoldenAngle * static_cast<float>(i);
		const float theta = baseTheta + FMath::FRandRange(-JitterRad, JitterRad);

		const float x = r * FMath::Cos(theta);
		const float y = r * FMath::Sin(theta);

		const float spreadScale = FMath::Tan(HalfAngleRad);

		const FVector ShotDir =
			(Forward + (Right * x + Up * y) * spreadScale).GetSafeNormal();

		const FVector TraceEnd = TraceStart + (ShotDir * Range);

		// ----- 펠릿 라인트레이스 -----
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(
			Hit,
			TraceStart,
			TraceEnd,
			TraceChannel,
			Params
		);

		const FVector HitLocation = bHit ? Hit.ImpactPoint : TraceEnd;

		// ----- 데미지(펠릿당 Damage로 동작) -----
		if (bHit)
		{
			if (AActor* HitActor = Hit.GetActor())
			{
				UGameplayStatics::ApplyPointDamage(
					HitActor,
					Damage,
					ShotDir,
					Hit,
					InstigatorController,
					this,
					UDamageType::StaticClass()
				);
			}
		}

		// ----- FX 데이터 저장 -----
		TraceEnds.Add(TraceEnd);
		HitFlags.Add(bHit ? 1 : 0);
		HitLocations.Add(HitLocation);
	}

	// ✅ FX는 1회 멀티캐스트
	MulticastFireShotgunFX(TraceStart, TraceEnds, HitFlags, HitLocations);
}

// ------------------------------------------------------------
// ✅ 샷건 FX 멀티캐스트 구현
// - 여기서 펠릿마다 "기존 GunBase의 FX 로직"을 그대로 재생해야 동일한 이펙트가 나옴
// - 핵심: Super::MulticastFireFX_Implementation(...) 을 호출하면 네 기존 FX 코드가 그대로 실행됨
//   (이건 네트워크 전송이 아니라 '로컬 실행'이야)
// ------------------------------------------------------------
void AHeroWeapon_Shotgun::MulticastFireShotgunFX_Implementation(
	FVector_NetQuantize TraceStart,
	const TArray<FVector_NetQuantize>& TraceEnds,
	const TArray<uint8>& HitFlags,
	const TArray<FVector_NetQuantize>& HitLocations
)
{
	// ════════════════════════════════════════════
	// [Phase 7.5] 발사 사운드는 1회만 재생
	// ════════════════════════════════════════════
	// Super::MulticastFireFX_Implementation을 루프 안에서 호출하면
	// 펠릿 수만큼 사운드가 중복 재생되는 문제 방지.
	// 사운드 1회 + Niagara FX N회로 분리.
	// ════════════════════════════════════════════
	PlayEquipActorFireSound();

	const int32 Count = TraceEnds.Num();

	for (int32 i = 0; i < Count; ++i)
	{
		const FVector End = TraceEnds[i];
		const bool bHit = HitFlags.IsValidIndex(i) ? (HitFlags[i] != 0) : false;
		const FVector HitLoc = HitLocations.IsValidIndex(i) ? (FVector)HitLocations[i] : End;

		// Niagara 임팩트 FX만 펠릿 수만큼 스폰 (사운드 제외)
		const FVector SpawnLoc = bHit ? HitLoc : End;
		SpawnImpactFX(SpawnLoc);
	}
}