// Capstone Project Helluna

#include "BossEvent/TimeDistortionZone.h"

#include "AbilitySystem/EnemyAbility/TimeDistortionOrb.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaHeroCharacter.h"
#include "Components/PostProcessComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/OverlapResult.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"

#define TDZ_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[TimeDistortionZone] " Fmt), ##__VA_ARGS__)

ATimeDistortionZone::ATimeDistortionZone()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	SlowSphere = CreateDefaultSubobject<USphereComponent>(TEXT("SlowSphere"));
	SlowSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SlowSphere->SetCollisionObjectType(ECC_WorldStatic);
	SlowSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	SlowSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SlowSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	SlowSphere->SetGenerateOverlapEvents(true);
	SlowSphere->SetHiddenInGame(true);
	SlowSphere->ShapeColor = FColor::Cyan;
	SetRootComponent(SlowSphere);

	PostProcessComp = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcessComp->SetupAttachment(RootComponent);
	PostProcessComp->bUnbound = true;
	PostProcessComp->bEnabled = false;
}

void ATimeDistortionZone::BeginPlay()
{
	Super::BeginPlay();

	SlowSphere->OnComponentBeginOverlap.AddDynamic(this, &ATimeDistortionZone::OnSlowSphereBeginOverlap);
	SlowSphere->OnComponentEndOverlap.AddDynamic(this, &ATimeDistortionZone::OnSlowSphereEndOverlap);

	// 아직 활성화 전이면 Overlap 이벤트 방지
	SlowSphere->SetGenerateOverlapEvents(false);
}

void ATimeDistortionZone::Destroyed()
{
	RestoreAllSlowedActors();
	ForceRestoreNearbyPlayers(); // [TDSlowReleaseFixV1] 안전망 — Zone 파괴 시도 경로
	DestroyAllOrbs();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DetonationTimerHandle);
		World->GetTimerManager().ClearTimer(ResultVFXTimerHandle);
		World->GetTimerManager().ClearTimer(OrbSpawnTimerHandle);
	}

	Super::Destroyed();
}

// -----------------------------------------------------------------
// ActivateZone
// -----------------------------------------------------------------
void ATimeDistortionZone::ActivateZone()
{
	Super::ActivateZone();

	if (bZoneActive) return;
	bZoneActive = true;
	bPatternBroken = false;

	TDZ_LOG("=== ActivateZone START (Duration=%.2f) ===", PatternDuration);

	// Overlap 활성화
	SlowSphere->SetGenerateOverlapEvents(true);
	SlowSphere->UpdateOverlaps();

	// 슬로우 영역 VFX — 지속형이므로 PersistentVFX 슬롯 사용
	if (SlowAreaVFX && OwnerEnemy)
	{
		OwnerEnemy->Multicast_SpawnPersistentVFX(1, SlowAreaVFX, SlowAreaVFXScale);
	}

	// 슬로우 시작 사운드
	if (SlowStartSound && OwnerEnemy)
	{
		OwnerEnemy->Multicast_PlaySoundAtLocation(SlowStartSound, GetActorLocation());
	}

	// 채도 제거 포스트프로세스
	if (DesaturationPPMaterial)
	{
		Multicast_ActivateDesaturation(GetActorLocation(), SlowSphere->GetScaledSphereRadius(), OwnerEnemy);
	}

	// Orb 순차 스폰
	StartOrbSpawnSequence();

	// 폭발 타이머 (파훼 실패 경로)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			DetonationTimerHandle,
			this,
			&ATimeDistortionZone::BeginDetonation,
			PatternDuration,
			false
		);
	}

	TDZ_LOG("=== ActivateZone END ===");
}

// -----------------------------------------------------------------
// DeactivateZone
// -----------------------------------------------------------------
void ATimeDistortionZone::DeactivateZone()
{
	Super::DeactivateZone();

	TDZ_LOG("=== DeactivateZone ===");

	bZoneActive = false;
	SlowSphere->SetGenerateOverlapEvents(false);

	RestoreAllSlowedActors();
	ForceRestoreNearbyPlayers(); // [TDSlowReleaseFixV1] 안전망
	DestroyAllOrbs();
	Multicast_DeactivateDesaturation();

	// 슬로우 영역 VFX 정리
	if (OwnerEnemy)
	{
		OwnerEnemy->Multicast_StopPersistentVFX(1);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DetonationTimerHandle);
		World->GetTimerManager().ClearTimer(ResultVFXTimerHandle);
		World->GetTimerManager().ClearTimer(OrbSpawnTimerHandle);
	}
}

// -----------------------------------------------------------------
// Overlap 콜백 — 모든 액터 대상
// -----------------------------------------------------------------
void ATimeDistortionZone::OnSlowSphereBeginOverlap(
	UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bZoneActive) return;
	if (!OtherActor) return;

	ApplySlowToActor(OtherActor);
}

void ATimeDistortionZone::OnSlowSphereEndOverlap(
	UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!OtherActor) return;

	RemoveSlowFromActor(OtherActor);
}

// -----------------------------------------------------------------
// CustomTimeDilation 기반 슬로우 적용/해제
// -----------------------------------------------------------------
void ATimeDistortionZone::ApplySlowToActor(AActor* Actor)
{
	if (!IsValid(Actor)) return;
	if (SlowedActors.Contains(Actor)) return;

	// 자기 자신, 소유 Enemy, 스폰된 Orb는 제외
	if (Actor == this) return;
	if (Actor == OwnerEnemy) return;
	if (Actor->IsA<ATimeDistortionOrb>()) return;

	if (AHellunaHeroCharacter* Player = Cast<AHellunaHeroCharacter>(Actor))
	{
		// 플레이어: 리플리케이션 기반 슬로우 (클라이언트 예측과 호환)
		// [TDJumpV1] 점프/중력도 함께 배율 적용 — 점프 높이 유지, 체공시간 1/M 배 늘어남
		Player->SetMoveSpeedMultiplier(TimeDilationScale);
		Player->SetAnimRateMultiplier(TimeDilationScale);
		Player->SetJumpGravityMultiplier(TimeDilationScale);
		SlowedActors.Add(Actor, 1.f);

		TDZ_LOG("[PLAYER] Slow applied to [%s]: MoveSpeed x%.2f, AnimRate x%.2f, JumpGravity x%.2f",
			*Actor->GetName(), TimeDilationScale, TimeDilationScale, TimeDilationScale);
	}
	else
	{
		// 비플레이어(투사체 등): CustomTimeDilation 기반 슬로우
		const float OriginalDilation = Actor->CustomTimeDilation;
		Actor->CustomTimeDilation = OriginalDilation * TimeDilationScale;
		SlowedActors.Add(Actor, OriginalDilation);

		// LifeSpan 보정: 슬로우로 느려진 만큼 수명 연장
		const float RemainingLife = Actor->GetLifeSpan();
		if (RemainingLife > 0.f)
		{
			Actor->SetLifeSpan(RemainingLife / TimeDilationScale);
			TDZ_LOG("[LifeSpan] %s: %.2f -> %.2f (÷%.2f)",
				*Actor->GetName(), RemainingLife, RemainingLife / TimeDilationScale, TimeDilationScale);
		}

		TDZ_LOG("[ACTOR] Slow applied to [%s] (Class: %s): TimeDilation %.2f -> %.2f",
			*Actor->GetName(), *Actor->GetClass()->GetName(), OriginalDilation, Actor->CustomTimeDilation);
	}
}

void ATimeDistortionZone::RemoveSlowFromActor(AActor* Actor)
{
	if (!IsValid(Actor)) return;

	const float* OriginalDilation = SlowedActors.Find(Actor);
	if (!OriginalDilation) return;

	if (AHellunaHeroCharacter* Player = Cast<AHellunaHeroCharacter>(Actor))
	{
		Player->SetMoveSpeedMultiplier(1.f);
		Player->SetAnimRateMultiplier(1.f);
		Player->SetJumpGravityMultiplier(1.f);
		TDZ_LOG("[PLAYER] Slow removed from [%s] (+Jump/Gravity 복원)", *Actor->GetName());
	}
	else
	{
		Actor->CustomTimeDilation = *OriginalDilation;

		const float RemainingLife = Actor->GetLifeSpan();
		if (RemainingLife > 0.f)
		{
			Actor->SetLifeSpan(RemainingLife * TimeDilationScale);
			TDZ_LOG("[LifeSpan] %s: %.2f -> %.2f (×%.2f)",
				*Actor->GetName(), RemainingLife, RemainingLife * TimeDilationScale, TimeDilationScale);
		}

		TDZ_LOG("[ACTOR] Slow removed from [%s]: TimeDilation restored to %.2f",
			*Actor->GetName(), *OriginalDilation);
	}

	SlowedActors.Remove(Actor);
}

void ATimeDistortionZone::RestoreAllSlowedActors()
{
	TDZ_LOG("RestoreAll: %d actors", SlowedActors.Num());

	TMap<TWeakObjectPtr<AActor>, float> Copy = SlowedActors;
	for (auto& Pair : Copy)
	{
		if (AActor* Actor = Pair.Key.Get())
		{
			RemoveSlowFromActor(Actor);
		}
	}
	SlowedActors.Empty();
}

// -----------------------------------------------------------------
// [TDSlowReleaseFixV1] 안전망: Zone 반경 1.5배 내 모든 HeroCharacter 강제 1.0 배율 복원.
//   맵 기반 복원(RestoreAllSlowedActors) 이후 이중 호출로 누락 케이스 보증.
// -----------------------------------------------------------------
void ATimeDistortionZone::ForceRestoreNearbyPlayers()
{
	UWorld* World = GetWorld();
	if (!World || !SlowSphere) return;

	const float ScanRadius = SlowSphere->GetScaledSphereRadius() * 1.5f;

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Q;
	Q.AddIgnoredActor(this);
	if (OwnerEnemy) Q.AddIgnoredActor(OwnerEnemy);

	World->OverlapMultiByObjectType(
		Overlaps,
		GetActorLocation(),
		FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(ScanRadius),
		Q);

	int32 RestoredCount = 0;
	for (const FOverlapResult& O : Overlaps)
	{
		if (AHellunaHeroCharacter* Player = Cast<AHellunaHeroCharacter>(O.GetActor()))
		{
			Player->SetMoveSpeedMultiplier(1.f);
			Player->SetAnimRateMultiplier(1.f);
			Player->SetJumpGravityMultiplier(1.f);
			++RestoredCount;

			// 맵에서도 정리 (이중 호출 안전)
			SlowedActors.Remove(Player);
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[TDSlowReleaseFixV1] ForceRestoreNearbyPlayers — scanR=%.0f restored=%d"),
		ScanRadius, RestoredCount);
}

// -----------------------------------------------------------------
// BeginDetonation — 패턴 종료 VFX 재생 후 딜레이
// -----------------------------------------------------------------
void ATimeDistortionZone::BeginDetonation()
{
	TDZ_LOG("=== BeginDetonation ===");

	if (bPatternBroken) return;

	bZoneActive = false;
	SlowSphere->SetGenerateOverlapEvents(false);

	DestroyAllOrbs();

	// 슬로우 영역 VFX 정리 (슬로우 자체는 유지 — FinishDetonation에서 해제)
	if (OwnerEnemy)
	{
		OwnerEnemy->Multicast_StopPersistentVFX(1);
	}

	// 패턴 종료 VFX (슬로우 해제 연출)
	if (PatternEndVFX && OwnerEnemy)
	{
		OwnerEnemy->MulticastPlayEffect(GetActorLocation(), PatternEndVFX, PatternEndVFXScale, false);
	}

	// 딜레이 후 실제 폭발 처리
	if (ResultVFXDelay > 0.f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				ResultVFXTimerHandle,
				this,
				&ATimeDistortionZone::FinishDetonation,
				ResultVFXDelay,
				false
			);
		}
	}
	else
	{
		FinishDetonation();
	}
}

// -----------------------------------------------------------------
// FinishDetonation — 폭발 VFX + 데미지
// -----------------------------------------------------------------
void ATimeDistortionZone::FinishDetonation()
{
	TDZ_LOG("=== FinishDetonation ===");

	// 슬로우 해제 + PP 해제
	RestoreAllSlowedActors();
	ForceRestoreNearbyPlayers(); // [TDSlowReleaseFixV1] 안전망
	Multicast_DeactivateDesaturation();

	// 폭발 VFX
	if (DetonationVFX && OwnerEnemy)
	{
		OwnerEnemy->MulticastPlayEffect(GetActorLocation(), DetonationVFX, DetonationVFXScale, false);
	}

	// 폭발 사운드
	if (DetonationSound && OwnerEnemy)
	{
		OwnerEnemy->Multicast_PlaySoundAtLocation(DetonationSound, GetActorLocation());
	}

	// 범위 내 플레이어에게 데미지 (슬로우 해제된 뒤이므로 Sphere Overlap으로 재수집)
	if (DetonationDamage > 0.f && OwnerEnemy)
	{
		UWorld* World = GetWorld();
		if (World)
		{
			TArray<FOverlapResult> Overlaps;
			FCollisionQueryParams QueryParams;
			QueryParams.AddIgnoredActor(this);
			QueryParams.AddIgnoredActor(OwnerEnemy);

			World->OverlapMultiByObjectType(
				Overlaps,
				GetActorLocation(),
				FQuat::Identity,
				FCollisionObjectQueryParams(ECC_Pawn),
				FCollisionShape::MakeSphere(SlowSphere->GetScaledSphereRadius()),
				QueryParams
			);

			TSet<AActor*> DamagedActors;
			for (const FOverlapResult& Overlap : Overlaps)
			{
				AHellunaHeroCharacter* Player = Cast<AHellunaHeroCharacter>(Overlap.GetActor());
				if (!IsValid(Player)) continue;
				if (DamagedActors.Contains(Player)) continue;
				DamagedActors.Add(Player);

				UGameplayStatics::ApplyDamage(
					Player,
					DetonationDamage,
					OwnerEnemy->GetController(),
					OwnerEnemy,
					UDamageType::StaticClass()
				);
				TDZ_LOG("Damage %.1f applied to [%s]", DetonationDamage, *Player->GetName());
			}
		}
	}

	TDZ_LOG("=== FinishDetonation END ===");
	NotifyPatternFinished(false);
}

// -----------------------------------------------------------------
// Orb 스폰
// -----------------------------------------------------------------
void ATimeDistortionZone::StartOrbSpawnSequence()
{
	UWorld* World = GetWorld();
	if (!World) return;

	OrbSpawnTotalCount = DecoyOrbCount + 1;
	OrbSpawnKeyIndex = FMath::RandRange(0, OrbSpawnTotalCount - 1);
	OrbSpawnCurrentIndex = 0;

	TDZ_LOG("StartOrbSpawnSequence: Total=%d, KeyIndex=%d", OrbSpawnTotalCount, OrbSpawnKeyIndex);

	SpawnNextOrb();

	if (OrbSpawnCurrentIndex < OrbSpawnTotalCount)
	{
		World->GetTimerManager().SetTimer(
			OrbSpawnTimerHandle,
			this,
			&ATimeDistortionZone::SpawnNextOrb,
			OrbSpawnInterval,
			true
		);
	}
}

void ATimeDistortionZone::SpawnNextOrb()
{
	if (bPatternBroken)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(OrbSpawnTimerHandle);
		}
		return;
	}

	if (OrbSpawnCurrentIndex >= OrbSpawnTotalCount)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(OrbSpawnTimerHandle);
		}
		TDZ_LOG("All orbs spawned: %d total", SpawnedOrbs.Num());
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	const FVector CenterLocation = GetActorLocation();
	const int32 i = OrbSpawnCurrentIndex;
	const bool bIsKey = (i == OrbSpawnKeyIndex);

	// 키/데코에 맞는 BP 클래스 선택
	TSubclassOf<ATimeDistortionOrb> OrbClass = bIsKey ? KeyOrbClass : DecoyOrbClass;
	if (!OrbClass)
	{
		TDZ_LOG("WARNING: %s OrbClass is null, skipping index %d",
			bIsKey ? TEXT("Key") : TEXT("Decoy"), i);
		OrbSpawnCurrentIndex++;
		return;
	}

	const float BaseAngleDeg = 360.f * static_cast<float>(i) / static_cast<float>(OrbSpawnTotalCount);

	// 지터 + 충돌 검사: 슬롯 주변에서 여러 샘플을 시도하여 벽/지형에 박히지 않는 위치를 찾는다.
	FCollisionQueryParams ProbeParams(SCENE_QUERY_STAT(TDZ_OrbSpawn), false);
	ProbeParams.AddIgnoredActor(this);
	if (OwnerEnemy) ProbeParams.AddIgnoredActor(OwnerEnemy);
	for (ATimeDistortionOrb* Existing : SpawnedOrbs)
	{
		if (IsValid(Existing)) ProbeParams.AddIgnoredActor(Existing);
	}

	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	const FCollisionShape ProbeShape = FCollisionShape::MakeSphere(OrbCollisionProbeRadius);
	const int32 MaxAttempts = FMath::Max(1, OrbSpawnMaxAttempts);

	FVector SpawnLocation = FVector::ZeroVector;
	bool bFoundClearLocation = false;

	for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
	{
		// 랜덤은 오직 거리/각도/높이 세 값에만 적용
		const float AngleJitter = FMath::FRandRange(-OrbAngleJitterDeg, OrbAngleJitterDeg);
		const float RadiusScale = 1.f + FMath::FRandRange(-OrbRadiusJitterRatio, OrbRadiusJitterRatio);
		const float HeightJitter = FMath::FRandRange(-OrbHeightJitter, OrbHeightJitter);

		const float AngleRad = FMath::DegreesToRadians(BaseAngleDeg + AngleJitter);
		const float Radius = OrbSpawnRadius * RadiusScale;
		const FVector Candidate = CenterLocation + FVector(
			FMath::Cos(AngleRad) * Radius,
			FMath::Sin(AngleRad) * Radius,
			OrbHeightOffset + HeightJitter
		);

		const bool bBlocked = World->OverlapAnyTestByObjectType(
			Candidate, FQuat::Identity, ObjParams, ProbeShape, ProbeParams);

		if (!bBlocked)
		{
			SpawnLocation = Candidate;
			bFoundClearLocation = true;
			break;
		}
	}

	if (!bFoundClearLocation)
	{
		// 모든 샘플이 막혔으면 기본 슬롯 위치로 폴백 (경고 로그)
		const float AngleRad = FMath::DegreesToRadians(BaseAngleDeg);
		SpawnLocation = CenterLocation + FVector(
			FMath::Cos(AngleRad) * OrbSpawnRadius,
			FMath::Sin(AngleRad) * OrbSpawnRadius,
			OrbHeightOffset
		);
		TDZ_LOG("WARNING: Orb %d: no clear spawn after %d attempts, falling back to base slot", i, MaxAttempts);
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerEnemy;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	ATimeDistortionOrb* Orb = World->SpawnActor<ATimeDistortionOrb>(
		OrbClass,
		SpawnLocation,
		FRotator::ZeroRotator,
		SpawnParams
	);

	if (!Orb)
	{
		TDZ_LOG("WARNING: Failed to spawn Orb at index %d", i);
		OrbSpawnCurrentIndex++;
		return;
	}

	Orb->InitOrb(bIsKey);
	Orb->OnOrbDestroyed.AddDynamic(this, &ATimeDistortionZone::OnOrbDestroyed);
	SpawnedOrbs.Add(Orb);

	// PP 채도 제거에서 제외
	SetActorCustomDepth(Orb, true);

	OrbSpawnCurrentIndex++;
}

void ATimeDistortionZone::DestroyAllOrbs()
{
	TDZ_LOG("DestroyAllOrbs: %d orbs", SpawnedOrbs.Num());

	for (ATimeDistortionOrb* Orb : SpawnedOrbs)
	{
		if (IsValid(Orb))
		{
			Orb->OnOrbDestroyed.RemoveDynamic(this, &ATimeDistortionZone::OnOrbDestroyed);
			Orb->Destroy();
		}
	}
	SpawnedOrbs.Empty();
}

// -----------------------------------------------------------------
// Orb 파괴 콜백
// -----------------------------------------------------------------
void ATimeDistortionZone::OnOrbDestroyed(ATimeDistortionOrb* DestroyedOrb)
{
	if (!DestroyedOrb) return;

	SpawnedOrbs.Remove(DestroyedOrb);

	if (DestroyedOrb->bIsKeyOrb && !bPatternBroken)
	{
		TDZ_LOG(">>> KEY ORB destroyed! Pattern broken!");
		OnPatternBroken();
	}
}

// -----------------------------------------------------------------
// 파훼 성공 — 패턴 종료 VFX → 딜레이 → 파훼 VFX
// -----------------------------------------------------------------
void ATimeDistortionZone::OnPatternBroken()
{
	TDZ_LOG("=== OnPatternBroken ===");
	bPatternBroken = true;
	bZoneActive = false;

	SlowSphere->SetGenerateOverlapEvents(false);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DetonationTimerHandle);
		World->GetTimerManager().ClearTimer(OrbSpawnTimerHandle);
	}

	// 슬로우 영역 VFX 정리 (슬로우 자체는 유지 — FinishPatternBroken에서 해제)
	if (OwnerEnemy)
	{
		OwnerEnemy->Multicast_StopPersistentVFX(1);
	}

	DestroyAllOrbs();

	BeginPatternBroken();
}

void ATimeDistortionZone::BeginPatternBroken()
{
	TDZ_LOG("=== BeginPatternBroken ===");

	// 패턴 종료 VFX
	if (PatternEndVFX && OwnerEnemy)
	{
		OwnerEnemy->MulticastPlayEffect(GetActorLocation(), PatternEndVFX, PatternEndVFXScale, false);
	}

	// 딜레이 후 파훼 VFX
	if (ResultVFXDelay > 0.f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				ResultVFXTimerHandle,
				this,
				&ATimeDistortionZone::FinishPatternBroken,
				ResultVFXDelay,
				false
			);
		}
	}
	else
	{
		FinishPatternBroken();
	}
}

void ATimeDistortionZone::FinishPatternBroken()
{
	TDZ_LOG("=== FinishPatternBroken ===");

	// 슬로우 해제 + PP 해제
	RestoreAllSlowedActors();
	ForceRestoreNearbyPlayers(); // [TDSlowReleaseFixV1] 안전망
	Multicast_DeactivateDesaturation();

	// 파훼 성공 VFX
	if (BreakSuccessVFX && OwnerEnemy)
	{
		OwnerEnemy->MulticastPlayEffect(GetActorLocation(), BreakSuccessVFX, BreakSuccessVFXScale, false);
	}

	// 파훼 성공 사운드
	if (BreakSuccessSound && OwnerEnemy)
	{
		OwnerEnemy->Multicast_PlaySoundAtLocation(BreakSuccessSound, GetActorLocation());
	}

	TDZ_LOG("=== FinishPatternBroken END ===");
	NotifyPatternFinished(true);
}

// -----------------------------------------------------------------
// 포스트프로세스 — 채도 제거
// -----------------------------------------------------------------
void ATimeDistortionZone::Multicast_ActivateDesaturation_Implementation(FVector Center, float Radius, AActor* BossActor)
{
	if (!DesaturationPPMaterial || !PostProcessComp) return;

	DesaturationMID = UMaterialInstanceDynamic::Create(DesaturationPPMaterial, this);
	if (!DesaturationMID) return;

	DesaturationMID->SetVectorParameterValue(FName("ZoneCenter"), FLinearColor(Center.X, Center.Y, Center.Z));
	DesaturationMID->SetScalarParameterValue(FName("ZoneRadius"), Radius);
	DesaturationMID->SetScalarParameterValue(FName("Strength"), DesaturationStrength);
	DesaturationMID->SetScalarParameterValue(FName("EdgeSoftness"), EdgeSoftness);

	// PP 설정 초기화 — 기본 비네팅/그레인 등이 적용되지 않도록 깨끗한 상태로
	PostProcessComp->Settings = FPostProcessSettings();
	PostProcessComp->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, DesaturationMID));
	PostProcessComp->bEnabled = true;

	// Custom Depth 설정 — PP에서 제외할 액터들 (보스만, Orb는 SpawnNextOrb에서 별도 마킹)
	// 플레이어는 일부러 제외하지 않음 — 존 안에서 같이 회색이 되어야 함
	// 클라이언트에서는 OwnerEnemy가 Transient이라 null. Multicast 인자로 받아온 BossActor 사용
	SetActorCustomDepth(BossActor, true);

	TDZ_LOG("[PP] Desaturation activated: Center=%s, Radius=%.0f, Strength=%.2f",
		*Center.ToString(), Radius, DesaturationStrength);
}

void ATimeDistortionZone::Multicast_DeactivateDesaturation_Implementation()
{
	if (PostProcessComp)
	{
		PostProcessComp->bEnabled = false;
		PostProcessComp->Settings.WeightedBlendables.Array.Empty();
	}
	DesaturationMID = nullptr;

	// Custom Depth 해제
	for (const TWeakObjectPtr<AActor>& WeakActor : CustomDepthActors)
	{
		if (AActor* Actor = WeakActor.Get())
		{
			TArray<UPrimitiveComponent*> Prims;
			Actor->GetComponents<UPrimitiveComponent>(Prims);
			for (UPrimitiveComponent* Prim : Prims)
			{
				Prim->SetRenderCustomDepth(false);
			}
		}
	}
	CustomDepthActors.Empty();

	TDZ_LOG("[PP] Desaturation deactivated");
}

void ATimeDistortionZone::SetActorCustomDepth(AActor* Actor, bool bEnable)
{
	if (!Actor) return;

	TArray<UPrimitiveComponent*> Prims;
	Actor->GetComponents<UPrimitiveComponent>(Prims);

	for (UPrimitiveComponent* Prim : Prims)
	{
		Prim->SetRenderCustomDepth(bEnable);
		if (bEnable)
		{
			Prim->SetCustomDepthStencilValue(1);
		}
	}

	if (bEnable)
	{
		CustomDepthActors.AddUnique(Actor);
	}
	else
	{
		CustomDepthActors.Remove(Actor);
	}
}

#undef TDZ_LOG
