// Capstone Project Helluna

#include "BossEvent/TimeDistortionZone.h"

#include "AbilitySystem/EnemyAbility/TimeDistortionOrb.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaHeroCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Engine/OverlapResult.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"

#define TDZ_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[TimeDistortionZone] " Fmt), ##__VA_ARGS__)

ATimeDistortionZone::ATimeDistortionZone()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	// [ZoneVisualLocFixV1] 존은 burst 시 SetActorLocation 으로 이동하는데, movement replicate 가
	//   꺼져 있으면 클라에선 그 이동이 안 와 돔/라이트가 최초 스폰 위치(보스)에 남는다. → 이동 복제 ON.
	SetReplicateMovement(true);

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

	// [TimeSalvoBloomV1] 스폰 시점엔 존 비주얼(돔 메시/라이트)을 숨긴다 — 구체가 착탄(ActivateZone)할 때
	//   비로소 reveal + bloom 되어 "퍼지듯" 생성된다. 전 머신(서버/클라)에서 BeginPlay 호출되므로 여기서 숨김.
	CacheAndHideZoneVisuals();
}

// -----------------------------------------------------------------
// [TimeSalvoBloomV1] 개화 reveal — 스폰 시 숨김 / 착탄 시 멀티캐스트 reveal + bloom
// -----------------------------------------------------------------
void ATimeDistortionZone::CacheAndHideZoneVisuals()
{
	RevealMeshes.Reset();
	RevealMeshTargetScales.Reset();
	RevealLights.Reset();

	TArray<UStaticMeshComponent*> Meshes;
	GetComponents<UStaticMeshComponent>(Meshes);
	for (UStaticMeshComponent* M : Meshes)
	{
		if (!M) continue;
		RevealMeshes.Add(M);
		RevealMeshTargetScales.Add(M->GetRelativeScale3D());
		M->SetHiddenInGame(true);
		M->SetVisibility(false, true);
	}

	TArray<ULightComponent*> Lights;
	GetComponents<ULightComponent>(Lights);
	for (ULightComponent* L : Lights)
	{
		if (!L) continue;
		RevealLights.Add(L);
		L->SetVisibility(false, true);
	}

	TDZ_LOG("CacheAndHideZoneVisuals — 숨김 mesh=%d light=%d (스폰 시 비표시, ActivateZone 에 개화)",
		RevealMeshes.Num(), RevealLights.Num());
}

void ATimeDistortionZone::Multicast_RevealZoneVisuals_Implementation(FVector ZoneWorldLoc)
{
	// [ZoneVisualLocFixV1] reveal 직전, 서버가 알려준 존 월드 위치로 강제 이동 — 모든 머신에서
	//   돔/라이트(존 자식 컴포넌트)의 중심을 burst 위치에 맞춘다. (클라는 movement replicate 가
	//   늦거나 누락돼 보스 위치에 비주얼이 남는 버그가 있었음.)
	SetActorLocation(ZoneWorldLoc);

	// 라이트는 즉시 full reveal.
	for (ULightComponent* L : RevealLights)
	{
		if (L) L->SetVisibility(true, true);
	}

	// 돔 메시 reveal. bloom 사용 시 스케일 0 에서 시작.
	const bool bBloom = (ZoneBloomDuration > KINDA_SMALL_NUMBER) && (RevealMeshes.Num() > 0);
	for (int32 i = 0; i < RevealMeshes.Num(); ++i)
	{
		UStaticMeshComponent* M = RevealMeshes[i];
		if (!M) continue;
		M->SetHiddenInGame(false);
		M->SetVisibility(true, true);
		if (bBloom)
		{
			M->SetRelativeScale3D(FVector::ZeroVector);
		}
	}

	if (bBloom)
	{
		BloomElapsed = 0.f;
		GetWorldTimerManager().SetTimer(BloomTimerHandle, this, &ATimeDistortionZone::TickBloom, 0.016f, true);
		TDZ_LOG("Multicast_RevealZoneVisuals — bloom 시작 (%.2fs)", ZoneBloomDuration);
	}
	else
	{
		TDZ_LOG("Multicast_RevealZoneVisuals — 즉시 reveal (bloom off)");
	}
}

void ATimeDistortionZone::TickBloom()
{
	BloomElapsed += 0.016f;
	const float Alpha = FMath::Clamp(BloomElapsed / FMath::Max(0.01f, ZoneBloomDuration), 0.f, 1.f);
	// ease-out cubic — 빠르게 퍼졌다가 부드럽게 안착.
	const float Eased = 1.f - FMath::Pow(1.f - Alpha, 3.f);
	for (int32 i = 0; i < RevealMeshes.Num(); ++i)
	{
		if (RevealMeshes[i] && RevealMeshTargetScales.IsValidIndex(i))
		{
			RevealMeshes[i]->SetRelativeScale3D(RevealMeshTargetScales[i] * Eased);
		}
	}
	if (Alpha >= 1.f)
	{
		GetWorldTimerManager().ClearTimer(BloomTimerHandle);
	}
}

void ATimeDistortionZone::Destroyed()
{
	RestoreAllSlowedActors();
	ForceRestoreNearbyPlayers(); // [TDSlowReleaseFixV1] 안전망 — Zone 파괴 시도 경로
	RestoreOwnerBossSlow();      // [BossSlowV1] 보스 감속 복원 (최종 안전망)
	DestroyAllOrbs();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DetonationTimerHandle);
		World->GetTimerManager().ClearTimer(ResultVFXTimerHandle);
		World->GetTimerManager().ClearTimer(OrbSpawnTimerHandle);
		World->GetTimerManager().ClearTimer(BloomTimerHandle); // [TimeSalvoBloomV1]
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

	// [TimeSalvoBloomV1] 구체 착탄 시점 — 숨겨둔 돔/라이트를 전 클라에서 reveal + bloom.
	//   [ZoneVisualLocFixV1] 서버 존 위치(=burst 위치)를 함께 넘겨 클라 비주얼 중심을 맞춘다.
	Multicast_RevealZoneVisuals(GetActorLocation());

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

	// [BossSlowV1] 시전 보스도 함께 감속 (존이 멀리 생겨 overlap 으로는 안 닿으므로 직접 적용).
	ApplyOwnerBossSlow();

	// [OrbBloomSyncV1] 파훼 구체는 돔이 다 피어난(bloom 완료) 뒤에 스폰한다.
	//   bloom 동안엔 돔 스케일이 0→full 이라, 즉시 스폰하면 먼저 나온 구체가 "아직 작은 존" 밖에
	//   떠 있는 것처럼 보임("구체가 존 바깥" 증상). bloom 시간만큼 지연해 항상 full 사이즈 존 안에 생성.
	if (UWorld* World = GetWorld())
	{
		const float OrbStartDelay = FMath::Max(0.f, ZoneBloomDuration);
		if (OrbStartDelay > KINDA_SMALL_NUMBER)
		{
			World->GetTimerManager().SetTimer(
				OrbSpawnTimerHandle, this,
				&ATimeDistortionZone::StartOrbSpawnSequence, OrbStartDelay, false);
		}
		else
		{
			StartOrbSpawnSequence();
		}
	}

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
	RestoreOwnerBossSlow();      // [BossSlowV1] 보스 감속 복원
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
		// [TDJumpFixV1] 점프 시작 직후 존 진입하면 상승 Z velocity는 그대로인데 중력만 줄어 점프 높이가
		//   h = v²/(2g) 관계에 따라 1/Scale 배로 튀어오르는 버그.
		//   공중이면 현재 Z velocity도 TimeDilationScale로 스케일해 물리적 일관성 유지.
		if (UCharacterMovementComponent* CMC = Player->GetCharacterMovement())
		{
			if (CMC->IsFalling())
			{
				FVector Vel = CMC->Velocity;
				const float OrigZ = Vel.Z;
				Vel.Z *= TimeDilationScale;
				CMC->Velocity = Vel;
				TDZ_LOG("[PLAYER][JumpFix] Airborne entry — Z vel %.1f -> %.1f (scale=%.2f)",
					OrigZ, Vel.Z, TimeDilationScale);
			}
		}

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
		// [TDJumpFixV1] Apply에서 스케일 다운한 Z velocity 복원 — 공중이면 역스케일.
		if (UCharacterMovementComponent* CMC = Player->GetCharacterMovement())
		{
			if (CMC->IsFalling())
			{
				FVector Vel = CMC->Velocity;
				const float OrigZ = Vel.Z;
				const float SafeScale = FMath::Max(TimeDilationScale, 0.01f);
				Vel.Z /= SafeScale;
				CMC->Velocity = Vel;
				TDZ_LOG("[PLAYER][JumpFix] Airborne exit — Z vel %.1f -> %.1f (÷%.2f)",
					OrigZ, Vel.Z, SafeScale);
			}
		}

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
// [BossSlowV1] 시전 보스 감속 — 존이 멀리 생겨도 보스를 직접 감속
//   전방 발사형 TimeSalvo 는 존이 플레이어 쪽(보스에서 먼 곳)에 피어나므로 overlap 슬로우가
//   보스에 닿지 않는다. 존 활성 동안 보스의 CustomTimeDilation 을 전 클라에서 직접 설정한다.
//   (CustomTimeDilation 은 리플리케이트되지 않으므로 멀티캐스트로 각 머신에서 set.
//    클라에선 OwnerEnemy 가 Transient=null 이라 보스 액터를 인자로 전달.)
// -----------------------------------------------------------------
void ATimeDistortionZone::ApplyOwnerBossSlow()
{
	if (!bSlowOwnerBoss) return;
	if (bOwnerBossSlowed) return;
	if (!OwnerEnemy) return;

	bOwnerBossSlowed = true;
	Multicast_SetOwnerBossTimeDilation(OwnerEnemy, TimeDilationScale);
	TDZ_LOG("[BossSlowV1] OwnerBoss slow applied (scale=%.2f) on %s",
		TimeDilationScale, *GetNameSafe(OwnerEnemy));
}

void ATimeDistortionZone::RestoreOwnerBossSlow()
{
	if (!bOwnerBossSlowed) return;
	bOwnerBossSlowed = false;

	if (OwnerEnemy)
	{
		Multicast_SetOwnerBossTimeDilation(OwnerEnemy, 1.f);
		TDZ_LOG("[BossSlowV1] OwnerBoss slow restored on %s", *GetNameSafe(OwnerEnemy));
	}
}

void ATimeDistortionZone::Multicast_SetOwnerBossTimeDilation_Implementation(AActor* Boss, float NewDilation)
{
	if (!IsValid(Boss)) return;
	// 절대값 set — RPC 가 한쪽(apply/restore)만 도달하는 경우에도 안전(누적 곱 imbalance 방지).
	Boss->CustomTimeDilation = FMath::Max(0.01f, NewDilation);
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
	RestoreOwnerBossSlow();      // [BossSlowV1] 보스 감속 복원
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

				const FVector HitFromDir = (Player->GetActorLocation() - GetActorLocation()).GetSafeNormal();
				UGameplayStatics::ApplyPointDamage(
					Player,
					DetonationDamage,
					HitFromDir,
					FHitResult(),
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

	// [OrbCenterDiagV1] 존 중심 vs 가장 가까운 플레이어 vs 존 반경 진단 — "구체가 존 바깥/플레이어 기준" 체감 확인용.
	{
		const FVector ZoneCenter = GetActorLocation();
		const float ZoneR = SlowSphere ? SlowSphere->GetScaledSphereRadius() : -1.f;
		float NearestPlayerDist = -1.f;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get())
			{
				if (APawn* P = PC->GetPawn())
				{
					const float D = FVector::Dist(P->GetActorLocation(), ZoneCenter);
					if (NearestPlayerDist < 0.f || D < NearestPlayerDist) NearestPlayerDist = D;
				}
			}
		}
		TDZ_LOG("[OrbCenterDiagV1] ZoneCenter=(%.0f,%.0f,%.0f) SlowR=%.0f EffSpawnR<=%.0f NearestPlayerDist=%.0f",
			ZoneCenter.X, ZoneCenter.Y, ZoneCenter.Z, ZoneR,
			FMath::Min(OrbSpawnRadius, (ZoneR > 0.f ? ZoneR * 0.85f : OrbSpawnRadius)), NearestPlayerDist);
	}

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

	// [OrbInsideZoneV1] 스폰 반경을 슬로우 구체 반경 안쪽으로 제한 — 존보다 밖에 구체가 생기지 않도록.
	//   지터(±RadiusJitter)까지 포함해도 존 안에 머물게 0.85 마진. 존 스케일이 바뀌어도 자동 추종.
	const float ZoneRadius = SlowSphere ? SlowSphere->GetScaledSphereRadius() : OrbSpawnRadius;
	const float EffectiveSpawnRadius = FMath::Min(OrbSpawnRadius, ZoneRadius * 0.85f);

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
	ObjParams.AddObjectTypesToQuery(ECC_Pawn);   // [OrbSpawnAvoidPawnV1] 플레이어/적/우주선 등 액터 위에 겹쳐 생성 방지(못 부수는 문제)

	const FCollisionShape ProbeShape = FCollisionShape::MakeSphere(OrbCollisionProbeRadius);
	const int32 MaxAttempts = FMath::Max(1, OrbSpawnMaxAttempts);

	FVector SpawnLocation = FVector::ZeroVector;
	bool bFoundClearLocation = false;

	// LOS 체크용 — 가장 가까운 player 위치 찾기. 플레이어 → orb 까지 가시선이 막히면 총알도 막힘.
	//   이 트레이스로 우주선 안/절벽 뒤 같은 위치를 거른다.
	APawn* RefPlayer = nullptr;
	{
		float BestDistSq = TNumericLimits<float>::Max();
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (!PC) continue;
			APawn* P = PC->GetPawn();
			if (!P) continue;
			const float D = FVector::DistSquared(P->GetActorLocation(), CenterLocation);
			if (D < BestDistSq) { BestDistSq = D; RefPlayer = P; }
		}
	}
	const FVector PlayerEye = RefPlayer
		? RefPlayer->GetActorLocation() + FVector(0, 0, 80.f)  // 대략 카메라 높이
		: CenterLocation;

	for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
	{
		// 랜덤은 오직 거리/각도/높이 세 값에만 적용
		const float AngleJitter = FMath::FRandRange(-OrbAngleJitterDeg, OrbAngleJitterDeg);
		const float RadiusScale = 1.f + FMath::FRandRange(-OrbRadiusJitterRatio, OrbRadiusJitterRatio);
		const float HeightJitter = FMath::FRandRange(-OrbHeightJitter, OrbHeightJitter);

		const float AngleRad = FMath::DegreesToRadians(BaseAngleDeg + AngleJitter);
		const float Radius = EffectiveSpawnRadius * RadiusScale;
		const FVector Candidate = CenterLocation + FVector(
			FMath::Cos(AngleRad) * Radius,
			FMath::Sin(AngleRad) * Radius,
			OrbHeightOffset + HeightJitter
		);

		const bool bBlocked = World->OverlapAnyTestByObjectType(
			Candidate, FQuat::Identity, ObjParams, ProbeShape, ProbeParams);
		if (bBlocked) continue;

		// LOS — player 시점에서 orb 까지 visibility line trace. 차단되면 총알도 막힘 → 슬롯 거부.
		if (RefPlayer)
		{
			FHitResult LosHit;
			FCollisionQueryParams LosParams(SCENE_QUERY_STAT(TDZ_OrbLOS), false);
			LosParams.AddIgnoredActor(this);
			if (OwnerEnemy) LosParams.AddIgnoredActor(OwnerEnemy);
			LosParams.AddIgnoredActor(RefPlayer);
			for (ATimeDistortionOrb* Existing : SpawnedOrbs)
			{
				if (IsValid(Existing)) LosParams.AddIgnoredActor(Existing);
			}
			const bool bLosBlocked = World->LineTraceSingleByChannel(
				LosHit, PlayerEye, Candidate, ECC_Visibility, LosParams);
			if (bLosBlocked) continue;  // 가시선 막힘 — 다음 시도
		}

		SpawnLocation = Candidate;
		bFoundClearLocation = true;
		break;
	}

	if (!bFoundClearLocation)
	{
		// [OrbSpawnLiftV1] 수평 샘플이 다 막혔으면(액터/지형이 슬롯 점유) 기본 슬롯에서 '위로' 올려가며 빈 곳을 찾는다.
		//   기존엔 막힌 기본 슬롯에 강제 스폰 → 액터와 겹쳐 못 부숨. 대신 액터 위로 띄워 부술 수 있게.
		const float AngleRad = FMath::DegreesToRadians(BaseAngleDeg);
		const FVector SlotXY = CenterLocation + FVector(
			FMath::Cos(AngleRad) * EffectiveSpawnRadius,
			FMath::Sin(AngleRad) * EffectiveSpawnRadius,
			0.f);
		const float LiftStep = 60.f;
		const int32 MaxLiftSteps = 12;   // 최대 +720cm 까지 위로 탐색
		SpawnLocation = SlotXY + FVector(0.f, 0.f, OrbHeightOffset);   // 못 찾으면 기본(최소한 시도)
		for (int32 L = 0; L <= MaxLiftSteps; ++L)
		{
			const FVector LiftCandidate = SlotXY + FVector(0.f, 0.f, OrbHeightOffset + LiftStep * L);
			if (!World->OverlapAnyTestByObjectType(LiftCandidate, FQuat::Identity, ObjParams, ProbeShape, ProbeParams))
			{
				SpawnLocation = LiftCandidate;
				bFoundClearLocation = true;
				break;
			}
		}
		TDZ_LOG("WARNING: Orb %d: no clear slot after %d attempts → lifted to Z=%.0f (clear=%d)",
			i, MaxAttempts, SpawnLocation.Z, bFoundClearLocation ? 1 : 0);
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
	RestoreOwnerBossSlow();      // [BossSlowV1] 보스 감속 복원
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
