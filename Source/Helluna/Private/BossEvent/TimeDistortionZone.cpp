// Capstone Project Helluna

#include "BossEvent/TimeDistortionZone.h"

#include "AbilitySystem/EnemyAbility/TimeDistortionOrb.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaHeroCharacter.h"
#include "Components/SphereComponent.h"
#include "Engine/OverlapResult.h"
#include "Kismet/GameplayStatics.h"

#define TDZ_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[TimeDistortionZone] " Fmt), ##__VA_ARGS__)

ATimeDistortionZone::ATimeDistortionZone()
{
	PrimaryActorTick.bCanEverTick = false;

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
	DestroyAllOrbs();

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
		// CustomTimeDilation은 사용하지 않음 → 점프 높이 정상 유지
		Player->SetMoveSpeedMultiplier(TimeDilationScale);
		Player->SetAnimRateMultiplier(TimeDilationScale);
		SlowedActors.Add(Actor, 1.f);

		TDZ_LOG("[PLAYER] Slow applied to [%s]: MoveSpeed x%.2f, AnimRate x%.2f",
			*Actor->GetName(), TimeDilationScale, TimeDilationScale);
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
		TDZ_LOG("[PLAYER] Slow removed from [%s]", *Actor->GetName());
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

	// 슬로우 해제
	RestoreAllSlowedActors();

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

	const float AngleRad = FMath::DegreesToRadians(360.f * static_cast<float>(i) / static_cast<float>(OrbSpawnTotalCount));
	const FVector Offset(
		FMath::Cos(AngleRad) * OrbSpawnRadius,
		FMath::Sin(AngleRad) * OrbSpawnRadius,
		OrbHeightOffset
	);
	const FVector SpawnLocation = CenterLocation + Offset;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerEnemy;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

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

	// 슬로우 해제
	RestoreAllSlowedActors();

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

#undef TDZ_LOG
